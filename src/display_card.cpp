#include "display_card.h"

#if HAS_CARD_SKIN

#include <time.h>
#include <algorithm>
#include <new>
#include "layout_card.h"
#include "display_ui.h"      // tft, markFrameDirty, stateBadgeText/Color, formatFinishClock
#include "settings.h"        // dispSettings, netSettings, dpSettings
#include "fonts.h"           // loadFontInto, FontID
#include "icons.h"           // drawIcon16, icon_lightning
#include "bambu_mqtt.h"      // getActiveConnCount, isPrinterConfigured
#include "hms_lookup.h"      // printerWasCanceled, ERROR_BADGE_TEXT
#include "tasmota.h"         // plug watts / kWh
#include "battery.h"
#include "thumb_fetch.h"

// ---------------------------------------------------------------------------
//  Frame snapshot. Everything the card shows, captured once per tick; the
//  scene is drawn from this alone.
//  Zeroed with memset before filling, so a whole-struct memcmp is safe.
// ---------------------------------------------------------------------------
enum CardKind : uint8_t { CK_PRINTING = 0, CK_FINISHED = 1, CK_IDLE = 2 };

struct CardTray  { uint16_t color; char type[12]; uint8_t present; uint8_t active; };
struct CardCell  { char lbl[10]; char lblShort[7]; int16_t val; uint8_t unit; uint8_t accent; uint16_t vclr; };
struct CardChip  { uint16_t color; char type[12]; uint8_t known; uint8_t active; int8_t remain; };

enum : uint8_t { CU_DEG = 0, CU_PCT = 1, CU_OF5 = 2 };

struct CardFrame {
  uint8_t  kind, slot, rotation, left, bottom;
  // palette
  uint16_t bg, txt, dim, track, bar, pname, accent, finish, nozAccent, etaClr;
  // header
  char     name[24];
  uint8_t  dotCount, dotActive;
  uint8_t  batPct, batShow, batCharging;
  char     watts[10];
  uint8_t  doorShow, doorOpen;
  uint16_t doorClr;
  char     pill[12];
  uint16_t pillColor;
  // hero
  char     job[64];
  uint8_t  pct, printing;
  uint16_t remainMin;
  char     eta[16];
  char     etaDay[8];  // "+1".."+6" when the print ends on a later day, else ""
  char     stage[28];
  uint16_t layer, layers;
  uint8_t  showActiveFil;
  CardChip activeFil;
  // plate thumbnail (left column landscape, beside the percent portrait)
  uint8_t  thumbShow;
  uint32_t thumbGen;
  // AMS column / strip; amsInBand = square: the slots replace the 2nd temperature row
  uint8_t  amsShow, amsInBand;
  char     amsLabel[12];
  char     amsHum[8];
  uint8_t  trayCount;
  CardTray trays[4];
  uint8_t  extShow;
  CardTray ext;
  // bottom band
  uint8_t  bandFilaments;
  uint8_t  cellCount;
  CardCell cells[LY_CARD_TEMP_MAX];
  uint8_t  chipCount, chipMore;
  CardChip chips[PRINT_MAP_MAX];
  // finished / idle
  char     head[24];
  uint16_t headColor;
  char     doneClock[16];
  char     kwh[16];
  uint8_t  doorWait;
  char     clock[8];
  char     ampm[4];
  uint8_t  swCount;
  CardTray sw[AMS_MAX_TRAYS + AMS_TRAY_UNITS];   // trays + unit markers
};

static CardFrame g_cur, g_last;
static bool      g_lastValid = false;
static const uint16_t* g_thumbPx = nullptr;   // pixels behind f.thumbShow (swapped RGB565)
static int16_t g_marqueeOff = 0;              // job-name scroll offset (px), see tickCardMarquee


static const CardGeo& geo() {
  const int32_t w = tft.width(), h = tft.height();
  if (w == h) return CARD_GEO_SQ;
  if (w >= 480) return CARD_GEO_LAND_L;
  if (h >= 480) return CARD_GEO_PORT_L;
  return (w > h) ? CARD_GEO_LAND : CARD_GEO_PORT;
}

// ---------------------------------------------------------------------------
//  Off-screen target: one PSRAM full frame, kept and re-created on a rotation
//  change. Card is PSRAM-only (HAS_CARD_SKIN).
// ---------------------------------------------------------------------------
static lgfx::LGFX_Sprite* g_full = nullptr;

static lgfx::LGFX_Sprite* allocFull(int16_t w, int16_t h) {
#if defined(BOARD_HAS_PSRAM)
  if (g_full && g_full->width() == w && g_full->height() == h) return g_full;
  if (!g_full) {
    g_full = new lgfx::LGFX_Sprite(&tft);
    g_full->setPsram(true);
    g_full->setColorDepth(16);
  }
  g_full->deleteSprite();
  if (g_full->createSprite(w, h)) return g_full;
  delete g_full;
  g_full = nullptr;
#else
  (void)w; (void)h;
#endif
  return nullptr;
}

// Draw context: the scene draws in screen coordinates; (ox, oy) shift them into
// the current target (shimmer strip, scratch sprite). Font state is tracked here, never
// through setFont(), whose cache follows the panel only.
struct Cv {
  lgfx::LGFX_Sprite* g;
  int16_t ox, oy;
  FontID  font;
  int16_t k;                                // CardGeo::k - 150 = the 1.5x tier
  bool    noTier = false;                   // draw the named face as-is (job name on 1.5x)

  Cv(lgfx::LGFX_Sprite* g_, int16_t ox_, int16_t oy_, FontID f_, int16_t k_ = 100)
      : g(g_), ox(ox_), oy(oy_), font(f_), k(k_) {}

  // Fixed sizes (radii, gaps, chip boxes) scaled to the geometry.
  int16_t S(int16_t v) const { return (int16_t)((v * k + 50) / 100); }
  // The scene names the 1x faces; the 1.5x tier swaps in its own.
  FontID tier(FontID id) const {
    if (k <= 100) return id;
    switch (id) {
      case FONT_CARD_NUM: return FONT_CARD_NUM_L;
      case FONT_CARD_LBL: return FONT_CARD_LBL_L;
      case FONT_BODY:     return FONT_SMALL_2X;
      case FONT_LARGE:    return FONT_XLARGE;
      default:            return id;
    }
  }
  void useFont(FontID id) {
    if (!noTier) id = tier(id);
    if (id == font) return;
    if (!loadFontInto(*g, id)) { g->unloadFont(); g->setTextFont(id == FONT_CARD_LBL ? 1 : 2); }
    font = id;
  }
  int16_t width(const char* s) { return (int16_t)g->textWidth(s); }
  void text(const char* s, int16_t x, int16_t y, FontID f, uint16_t c,
            lgfx::textdatum_t d) {
    useFont(f);
    g->setTextDatum(d);
    g->setTextColor(c);                     // transparent: blends with sprite pixels
    g->drawString(s, x - ox, y - oy);
  }
  void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { g->fillRect(x - ox, y - oy, w, h, c); }
  void rrect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) { g->fillSmoothRoundRect(x - ox, y - oy, w, h, r, c); }
  void rrectOutline(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) { g->drawRoundRect(x - ox, y - oy, w, h, r, c); }
  void hline(int16_t x, int16_t y, int16_t w, uint16_t c) { g->drawFastHLine(x - ox, y - oy, w, c); }
  void vline(int16_t x, int16_t y, int16_t h, uint16_t c) { g->drawFastVLine(x - ox, y - oy, h, c); }
  void dot(int16_t x, int16_t y, int16_t r, uint16_t c) { g->fillSmoothCircle(x - ox, y - oy, r, c); }
  void ring(int16_t x, int16_t y, int16_t r, uint16_t c) { g->drawCircle(x - ox, y - oy, r, c); }
  void icon(int16_t x, int16_t y, const uint8_t* ic, uint16_t c) { drawIcon16(*g, x - ox, y - oy, ic, c); }
};

// Copy into dst, cutting with ".." until it fits maxW in the current font.
static void fitText(Cv& cv, char* dst, size_t n, const char* src, int16_t maxW) {
  strlcpy(dst, src, n);
  if (cv.width(dst) <= maxW) return;
  size_t len = strlen(dst);
  while (len > 0) {
    len--;
    while (len > 0 && ((uint8_t)dst[len] & 0xC0) == 0x80) len--;  // UTF-8 boundary
    if (len + 3 > n) continue;
    dst[len] = '\0';
    strlcat(dst, "..", n);
    if (cv.width(dst) <= maxW) return;
    dst[len] = '\0';
  }
  dst[0] = '\0';
}

// ---------------------------------------------------------------------------
//  Snapshot helpers
// ---------------------------------------------------------------------------
static const char* pillWord(const BambuState& s) {
  const char* raw = stateBadgeText(s);
  if (raw != s.gcodeState) return raw;          // ERR / CANCELED overrides
  switch (s.gcodeStateId) {
    case GCODE_RUNNING: return "PRINTING";
    case GCODE_PREPARE: return "PREPARING";
    case GCODE_PAUSE:   return "PAUSED";
    case GCODE_FINISH:  return "FINISHED";
    case GCODE_FAILED:  return "FAILED";
    case GCODE_IDLE:    return "IDLE";
    default:            return raw[0] ? raw : "--";
  }
}

static void formatClock(time_t t, char* buf, size_t n, char* ampm, size_t an) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  if (ampm) ampm[0] = '\0';
  if (netSettings.use24h) {
    snprintf(buf, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  } else {
    int h = tmv.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(buf, n, "%d:%02d", h, tmv.tm_min);
    if (ampm) strlcpy(ampm, tmv.tm_hour < 12 ? "AM" : "PM", an);
  }
}

// Unit the column / AMS cells describe: the one feeding, else the first present.
static int8_t displayAmsUnit(const AmsState& a) {
  if (!a.present || a.unitCount == 0) return -1;
  if (a.activeTray < AMS_MAX_TRAYS) {
    uint8_t u = a.activeTray / AMS_TRAYS_PER_UNIT;
    if (u < a.unitCount && a.units[u].present) return (int8_t)u;
  }
  if (a.activeTray == AMS_TRAY_OVERFLOW) {
    for (uint8_t i = 0; i < a.unitCount && i < AMS_MAX_UNITS; i++)
      if (a.units[i].present && a.units[i].id == a.ovUnitId) return (int8_t)i;
  }
  for (uint8_t i = 0; i < a.unitCount && i < AMS_MAX_UNITS; i++)
    if (a.units[i].present) return (int8_t)i;
  return -1;
}

static void copyType(char* dst, size_t n, const char* type) {
  strlcpy(dst, (type && type[0]) ? type : "--", n);
}

static void fillTray(CardTray& t, const AmsTray& src, bool active) {
  t.present = src.present ? 1 : 0;
  t.color   = src.colorRgb565;
  copyType(t.type, sizeof(t.type), src.present ? src.type : "");
  t.active  = active ? 1 : 0;
}

// Resolve one print.mapping entry, (unitId << 8) | trayId, against live AMS data.
static void resolveMapEntry(const BambuState& s, uint16_t raw, CardChip& c) {
  const AmsState& a = s.ams;
  const uint8_t uid = (uint8_t)(raw >> 8), tid = (uint8_t)(raw & 0xFF);
  c.known = 0; c.active = 0; c.color = dispSettings.textDimColor; c.remain = -1;
  strlcpy(c.type, "?", sizeof(c.type));
  for (uint8_t i = 0; i < a.unitCount && i < AMS_MAX_UNITS; i++) {
    if (!a.units[i].present || a.units[i].id != uid) continue;
    if (i < AMS_TRAY_UNITS && tid < AMS_TRAYS_PER_UNIT) {
      const AmsTray& t = a.trays[i * AMS_TRAYS_PER_UNIT + tid];
      if (!t.present) return;
      c.known = 1; c.color = t.colorRgb565; c.remain = t.remain;
      copyType(c.type, sizeof(c.type), t.type);
      c.active = (a.activeTray == i * AMS_TRAYS_PER_UNIT + tid) ? 1 : 0;
      return;
    }
    break;
  }
  if (a.ovUnitId == uid && a.ovTrayId == tid && a.ovTray.present) {
    c.known = 1; c.color = a.ovTray.colorRgb565; c.remain = a.ovTray.remain;
    copyType(c.type, sizeof(c.type), a.ovTray.type);
    c.active = (a.activeTray == AMS_TRAY_OVERFLOW) ? 1 : 0;
  }
}

static void activeFilament(const BambuState& s, CardChip& c) {
  const AmsState& a = s.ams;
  c.known = 0; c.active = 1; c.color = dispSettings.textDimColor; c.type[0] = '\0';
  const AmsTray* t = nullptr;
  if (a.activeTray < AMS_MAX_TRAYS)          t = &a.trays[a.activeTray];
  else if (a.activeTray == AMS_TRAY_OVERFLOW) t = &a.ovTray;
  if (t && t->present) {
    c.known = 1; c.color = t->colorRgb565; copyType(c.type, sizeof(c.type), t->type);
  } else if (a.activeTray == 254 && a.vtPresent) {
    c.known = 1; c.color = a.vtColorRgb565; copyType(c.type, sizeof(c.type), a.vtType);
  }
}

static void addCell(CardFrame& f, const char* lbl, const char* shortLbl, float v,
                    uint8_t unit, uint16_t vclr, bool accent = false) {
  if (f.cellCount >= LY_CARD_TEMP_MAX) return;
  CardCell& c = f.cells[f.cellCount++];
  strlcpy(c.lbl, lbl, sizeof(c.lbl));
  strlcpy(c.lblShort, shortLbl, sizeof(c.lblShort));
  c.val = (int16_t)lroundf(v);
  c.unit = unit;
  c.accent = accent ? 1 : 0;
  c.vclr = vclr;
}

// P1P/P1S/A1/A1 mini have no chamber sensor; they still send a placeholder
// chamber_temper (5 on a live P1S), so the cell is gated by model, not value.
static bool hasChamberSensor(const char* serial) {
  if (!serial || strlen(serial) < 3) return true;
  return !(strncmp(serial, "01P", 3) == 0 || strncmp(serial, "01S", 3) == 0 ||
           strncmp(serial, "039", 3) == 0 || strncmp(serial, "030", 3) == 0);
}

static void snapBottom(CardFrame& f, const BambuState& s, const char* serial) {
  // Filaments of this print, when asked for and the printer sent a map.
  if (dispSettings.cardBottom == 1 && s.printMapCount > 0) {
    f.bandFilaments = 1;
    for (uint8_t i = 0; i < s.printMapCount && f.chipCount < PRINT_MAP_MAX; i++) {
      bool dup = false;
      for (uint8_t j = 0; j < i; j++) if (s.printMap[j] == s.printMap[i]) { dup = true; break; }
      if (dup) continue;
      resolveMapEntry(s, s.printMap[i], f.chips[f.chipCount++]);
    }
    if (s.printMapTotal > s.printMapCount) f.chipMore = s.printMapTotal - s.printMapCount;
    return;
  }
  // Temperatures: only what this printer reports. Nozzles first, then bed and
  // chamber - the square's AMS row keeps just the first three.
  if (s.dualNozzle) {
    addCell(f, "NOZZLE L", "NOZ L", s.nozzleTempN[1], CU_DEG, dispSettings.nozzle.value, s.activeNozzle == 1);
    addCell(f, "NOZZLE R", "NOZ R", s.nozzleTempN[0], CU_DEG, dispSettings.nozzle.value, s.activeNozzle == 0);
  } else {
    addCell(f, "NOZZLE", "NOZ", s.nozzleTemp, CU_DEG, dispSettings.nozzle.value);
  }
  addCell(f, "BED", "BED", s.bedTemp, CU_DEG, dispSettings.bed.value);
  if (hasChamberSensor(serial) && s.chamberTemp > 0.5f) addCell(f, "CHAMBER", "CHMB", s.chamberTemp, CU_DEG, dispSettings.chamberTemp.value);
  int8_t u = displayAmsUnit(s.ams);
  if (u >= 0) {
    const AmsUnit& au = s.ams.units[u];
    if (au.temp > 0.5f) addCell(f, "AMS", "AMS", au.temp, CU_DEG, dispSettings.textColor);
    if (au.humidityRaw > 0)    addCell(f, "HUMIDITY", "HUM", au.humidityRaw, CU_PCT, dispSettings.textColor);
    else if (au.humidity > 0)  addCell(f, "HUMIDITY", "HUM", au.humidity, CU_OF5, dispSettings.textColor);
  }
}

static void snapAmsColumn(CardFrame& f, const BambuState& s) {
  if (dispSettings.cardLeft == 1) return;
  const AmsState& a = s.ams;
  int8_t u = displayAmsUnit(a);
  if (u < 0 || u >= AMS_TRAY_UNITS) {
    // No tray-owning unit to list; an external spool alone still earns the column.
    if (!a.vtPresent) return;
  }
  f.amsShow = 1;
  if (u >= 0 && u < AMS_TRAY_UNITS) {
    const AmsUnit& au = a.units[u];
    if (au.id >= 128) snprintf(f.amsLabel, sizeof(f.amsLabel), "AMS HT");
    else formatAmsNumberLabel(f.amsLabel, sizeof(f.amsLabel), (uint8_t)u);
    if (au.humidityRaw > 0)   snprintf(f.amsHum, sizeof(f.amsHum), "%u%%", au.humidityRaw);
    else if (au.humidity > 0) snprintf(f.amsHum, sizeof(f.amsHum), "%u/5", au.humidity);
    uint8_t n = au.trayCount ? au.trayCount : AMS_TRAYS_PER_UNIT;
    if (n > 4) n = 4;
    f.trayCount = n;
    for (uint8_t t = 0; t < n; t++) {
      uint8_t idx = u * AMS_TRAYS_PER_UNIT + t;
      fillTray(f.trays[t], a.trays[idx], a.activeTray == idx);
    }
  } else {
    strlcpy(f.amsLabel, "SPOOL", sizeof(f.amsLabel));
  }
  if (a.vtPresent) {
    f.extShow = 1;
    f.ext.present = 1;
    f.ext.color = a.vtColorRgb565;
    snprintf(f.ext.type, sizeof(f.ext.type), "EXT %s", a.vtType);
    f.ext.active = (a.activeTray == 254) ? 1 : 0;
  }
}

// Copy UTF-8 text keeping only code points the VLW pack has (ASCII, Latin-1,
// Latin Extended-A, Romanian S/T comma, euro); others would draw as boxes.
static void copyRenderable(char* dst, size_t n, const char* src) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (*p && o + 1 < n) {
    uint32_t cp; int len;
    if (*p < 0x80)              { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; len = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; len = 3; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; len = 4; }
    else { p++; continue; }
    int k = 1;
    for (; k < len && (p[k] & 0xC0) == 0x80; k++) cp = (cp << 6) | (p[k] & 0x3F);
    if (k < len) { p += k; continue; }       // truncated sequence
    bool ok = (cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp < 0x180) ||
              (cp >= 0x218 && cp <= 0x21B) || cp == 0x20AC;
    if (ok) {
      if (o + len >= n) break;
      memcpy(dst + o, p, len);
      o += len;
    }
    p += len;
  }
  dst[o] = '\0';
}

static void snapCommon(CardFrame& f, CardKind kind, PrinterSlot& p) {
  memset(&f, 0, sizeof(f));
  const BambuState& s = p.state;
  f.kind = kind;
  f.slot = rotState.displayIndex;
  f.rotation = dispSettings.rotation;
  f.left = dispSettings.cardLeft;
  f.bottom = dispSettings.cardBottom;
  f.bg = dispSettings.bgColor;
  f.txt = dispSettings.textColor;
  f.dim = dispSettings.textDimColor;
  f.track = dispSettings.trackColor;
  f.bar = dispSettings.progressBarColor;       // "Progress Bar" picker
  f.etaClr = dispSettings.etaColor;               // "Finish time" picker: ETA + REMAINING
  f.pname = dispSettings.printerNameColor;
  f.accent = dispSettings.statusOkColor;
  f.finish = dispSettings.finishColor;
  f.nozAccent = dispSettings.nozzle.arc;

  copyRenderable(f.name, sizeof(f.name), p.config.name[0] ? p.config.name : "Printer");
  if (getActiveConnCount() > 1) {
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (!isPrinterConfigured(i)) continue;
      if (i == rotState.displayIndex) f.dotActive = f.dotCount;
      f.dotCount++;
    }
  }
#if defined(BOARD_HAS_BATTERY)
  if (dispSettings.showBatteryIndicator && Battery::isPresent()) {   // same gate as the status bar
    f.batShow = 1;
    f.batPct = Battery::percent();
    f.batCharging = Battery::isCharging() ? 1 : 0;
  }
#endif
  if (s.doorSensorPresent) {
    f.doorShow = 1;
    f.doorOpen = s.doorOpen ? 1 : 0;
    f.doorClr = s.doorOpen ? dispSettings.doorOpenColor : dispSettings.doorClosedColor;
  }
  strlcpy(f.pill, pillWord(s), sizeof(f.pill));
  f.pillColor = stateBadgeColor(s);
  copyRenderable(f.job, sizeof(f.job), jobDisplayName(s));
  snapAmsColumn(f, s);
  if (geo().noAms && f.amsShow) {
    f.amsShow = 0;
    f.amsInBand = (f.trayCount || f.extShow) ? 1 : 0;
  }
  snapBottom(f, s, p.config.serial);
  // Plate preview replaces the AMS column once a thumbnail exists. Needs the
  // full-frame sprite: the band path has nowhere to copy it from.
  if (dispSettings.cardLeft == 2 && kind != CK_IDLE && g_full) {
    const int16_t size = geo().thumbSize;
    if (thumbGet(size, f.bg, &g_thumbPx, &f.thumbGen)) {
      f.thumbShow = 1;
      f.amsShow = 0;
      f.amsInBand = 0;                    // preview instead of AMS, as elsewhere
    }
  }
}

// ---------------------------------------------------------------------------
//  Scene pieces. All coordinates are screen coordinates.
// ---------------------------------------------------------------------------
static void drawDeg(Cv& cv, int16_t x, int16_t capTop, uint16_t c) {
  const int16_t r = cv.S(3);
  cv.ring(x + r, capTop + r, r, c);
  cv.ring(x + r, capTop + r, r - 1, c);
}

static void drawHeader(Cv& cv, const CardFrame& f, const CardGeo& g) {
  const int16_t right = g.W - g.pad;
  // State pill
  cv.useFont(FONT_CARD_LBL);
  int16_t pw = cv.width(f.pill) + 2 * g.pillPadX;
  int16_t px = right - pw;
  cv.rrect(px, g.hdrCy - g.pillH / 2, pw, g.pillH, g.pillH / 2, f.pillColor);
  cv.text(f.pill, px + pw / 2, g.hdrCy + 1, FONT_CARD_LBL, f.bg, lgfx::textdatum_t::middle_center);

  int16_t limit = px - cv.S(8);
  // Battery
  if (f.batShow) {
    const int16_t bw = cv.S(18), bh = cv.S(9);
    int16_t bx = limit - bw - 2, by = g.hdrCy - bh / 2;
    uint16_t c = f.batCharging ? f.accent : (f.batPct <= 15 ? CLR_RED : f.dim);
    cv.rrectOutline(bx, by, bw, bh, 2, c);
    cv.rect(bx + bw, by + 3, 2, 3, c);
    int16_t fw = (int16_t)((bw - 4) * (f.batPct > 100 ? 100 : f.batPct) / 100);
    if (fw > 0) cv.rect(bx + 2, by + 2, fw, bh - 4, c);
    limit = bx - 8;
  }
  // Door: closed lock / open lock, in the door colours
  if (f.doorShow) {
    cv.icon(limit - 16, g.hdrCy - 8, f.doorOpen ? icon_unlock : icon_lock, f.doorClr);
    limit -= 16 + 6;
  }
  // Plug power while printing
  if (f.watts[0]) {
    cv.text(f.watts, limit, g.hdrCy, FONT_BODY, f.dim, lgfx::textdatum_t::middle_right);
    cv.useFont(FONT_BODY);
    int16_t ix = limit - cv.width(f.watts) - 15;
    // Yellow vanishes on a light background; take the amber there.
    const uint16_t bg = f.bg;
    const bool lightBg = (((bg >> 11) & 0x1F) * 2 + ((bg >> 5) & 0x3F) + (bg & 0x1F) * 2) > 120;
    cv.icon(ix, g.hdrCy - 8, icon_lightning, lightBg ? 0xC400 : CLR_YELLOW);
    limit = ix - 6;
  }
  // Multi-printer dots, centred (or left of whatever sits on the right). The
  // name wins: on a crowded header the dots go before the name gets cut.
  if (f.dotCount > 1) {
    const int16_t dp = cv.S(10);
    int16_t span = (f.dotCount - 1) * dp;
    int16_t x0 = std::min<int16_t>(g.W / 2 - span / 2, limit - span - 4);
    cv.useFont(FONT_BODY);
    if (x0 - 10 - g.pad >= cv.width(f.name)) {
      for (uint8_t k = 0; k < f.dotCount; k++)
        cv.dot(x0 + k * dp, g.hdrCy, cv.S(3), k == f.dotActive ? f.accent : CLR_TEXT_DARK);
      limit = x0 - 10;
    }
  }
  // Printer name
  char buf[24];
  cv.useFont(FONT_BODY);
  fitText(cv, buf, sizeof(buf), f.name, limit - g.pad);
  cv.text(buf, g.pad, g.hdrCy, FONT_BODY, f.pname, lgfx::textdatum_t::middle_left);
  cv.hline(g.pad, g.hdrRule, g.W - 2 * g.pad, f.track);
}

static void drawChipDot(Cv& cv, int16_t cx, int16_t cy, const CardChip& c, const CardFrame& f) {
  if (c.active) cv.dot(cx, cy, cv.S(7), f.txt);
  cv.dot(cx, cy, cv.S(5), c.known ? c.color : f.track);
  if (c.known && c.color == f.bg) cv.ring(cx, cy, cv.S(5), f.dim);  // swatch the colour of the background
}

// One AMS slot (chip + type), shared by the column and the strip.
static void drawSlot(Cv& cv, const CardTray& t, const CardFrame& f,
                     int16_t x, int16_t y, int16_t w, int16_t h) {
  uint16_t fg = t.present ? f.txt : f.dim;
  if (t.active) {
    cv.rrect(x - 2, y, w + 4, h, cv.S(4), f.txt);
    fg = f.bg;
  }
  int16_t cy = y + h / 2;
  const int16_t cs = cv.S(12);
  if (t.present) {
    cv.rrect(x + 2, cy - cs / 2, cs, cs, cv.S(3), t.color);
    if (t.color == (t.active ? f.txt : f.bg)) cv.rrectOutline(x + 2, cy - cs / 2, cs, cs, cv.S(3), f.dim);
  } else {
    cv.rrectOutline(x + 2, cy - cs / 2, cs, cs, cv.S(3), f.dim);
  }
  char buf[12];
  cv.useFont(FONT_CARD_LBL);
  // Too narrow for "PLA Matte": the material word alone beats "PLA ..".
  const int16_t tx = cv.S(19);
  if (cv.width(t.type) > w - tx - 1) {
    strlcpy(buf, t.type, sizeof(buf));
    if (char* sp = strchr(buf, ' ')) *sp = '\0';
    if (cv.width(buf) > w - tx - 1) fitText(cv, buf, sizeof(buf), t.type, w - tx - 1);
  } else {
    strlcpy(buf, t.type, sizeof(buf));
  }
  cv.text(buf, x + tx, cy + 1, FONT_CARD_LBL, fg, lgfx::textdatum_t::middle_left);
}

static void drawAmsColumn(Cv& cv, const CardFrame& f, const CardGeo& g) {
  const int16_t x = g.pad, w = g.amsColW;
  cv.text(f.amsLabel, x, g.amsHdrY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
  if (f.amsHum[0]) cv.text(f.amsHum, x + w, g.amsHdrY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_right);
  uint8_t rows = f.trayCount + (f.extShow ? 1 : 0);
  for (uint8_t r = 0; r < rows; r++) {
    const CardTray& t = (r < f.trayCount) ? f.trays[r] : f.ext;
    drawSlot(cv, t, f, x, g.amsRowY + r * g.amsRowH, w, g.amsRowH - 3);
  }
  int16_t rx = g.pad + g.amsColW + 6;
  cv.vline(rx, g.hdrRule + 8, g.botRuleAms - g.hdrRule - 16, f.track);
}

static void drawAmsStrip(Cv& cv, const CardFrame& f, const CardGeo& g) {
  const int16_t x0 = g.pad, w = g.W - 2 * g.pad;
  cv.text(f.amsLabel, x0, g.stripLblY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
  if (f.amsHum[0]) cv.text(f.amsHum, x0 + w, g.stripLblY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_right);
  const int16_t cols = g.stripCols ? g.stripCols : 4;
  const int16_t slotW = w / cols;
  const int16_t rowH = g.stripCols ? g.stripRowH : 0;
  uint8_t n = 0;
  for (uint8_t r = 0; r < f.trayCount + (f.extShow ? 1 : 0) && n < 4; r++, n++) {
    const CardTray& t = (r < f.trayCount) ? f.trays[r] : f.ext;
    drawSlot(cv, t, f, x0 + (n % cols) * slotW, g.stripY + (n / cols) * rowH, slotW - 8, g.stripH);
  }
}


// Active filament: swatch + type, left-aligned at x, centred on cy.
static int16_t activeFilWidth(Cv& cv, const CardFrame& f) {
  cv.useFont(FONT_CARD_LBL);
  return cv.S(14) + cv.width(f.activeFil.type);
}
static void drawActiveFil(Cv& cv, const CardFrame& f, int16_t x, int16_t cy, int16_t maxW) {
  CardChip c = f.activeFil;
  c.active = 0;
  drawChipDot(cv, x + cv.S(5), cy, c, f);
  char buf[12];
  cv.useFont(FONT_CARD_LBL);
  fitText(cv, buf, sizeof(buf), c.type, maxW - cv.S(14));
  cv.text(buf, x + cv.S(14), cy + 1, FONT_CARD_LBL, f.txt, lgfx::textdatum_t::middle_left);
}

// "2h 14m": numbers in FONT_LARGE, units dim in FONT_BODY, right-aligned at xr.
static int16_t drawDuration(Cv& cv, int16_t xr, int16_t base, uint16_t minutes, const CardFrame& f) {
  char buf[8];
  const uint16_t h = minutes / 60;
  int16_t x = xr;
  // Right to left: "m", minutes, then "h" and hours when there are any.
  for (int part = 0; part < (h ? 2 : 1); part++) {
    snprintf(buf, sizeof(buf), "%u", part ? h : minutes % 60);
    const char* unit = part ? "h" : "m";
    cv.useFont(FONT_BODY);
    x -= cv.width(unit) + (part ? cv.S(6) : 0);
    cv.text(unit, x, base, FONT_BODY, f.dim, lgfx::textdatum_t::baseline_left);
    cv.useFont(FONT_LARGE);
    x -= cv.width(buf) + 1;
    cv.text(buf, x, base, FONT_LARGE, f.etaClr, lgfx::textdatum_t::baseline_left);
  }
  return xr - x;
}

static void drawBottom(Cv& cv, const CardFrame& f, const CardGeo& g, int16_t rule) {
  const int16_t x0 = g.pad, w = g.W - 2 * g.pad;
  const int16_t yMax = g.H - g.pad;
  cv.hline(x0, rule, w, f.track);
  if (f.bandFilaments) {
    cv.text("FILAMENTS", x0, rule + g.cellLblOff, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    // List form when every filament gets its own row: swatch, type, a bar of
    // what is left on the spool in its colour, and the percentage.
    const int16_t rowH = cv.S(22), listTop = rule + g.cellLblOff + cv.S(22);
    if (!f.chipMore && f.chipCount * rowH <= yMax - listTop + rowH / 2) {
      cv.useFont(FONT_BODY);
      const int16_t pctW = cv.width("100%");
      // Bar starts after the longest name, but keeps at least 40 px.
      int16_t typeW = 0;
      for (uint8_t i = 0; i < f.chipCount; i++) typeW = std::max<int16_t>(typeW, cv.width(f.chips[i].type));
      const int16_t barEnd = x0 + w - pctW - 8;
      const int16_t barX = std::min<int16_t>(x0 + cv.S(19) + typeW + 10, barEnd - cv.S(40));
      const int16_t barW = barEnd - barX;
      for (uint8_t i = 0; i < f.chipCount; i++) {
        const CardChip& c = f.chips[i];
        const int16_t cy = listTop + i * rowH;
        drawChipDot(cv, x0 + cv.S(7), cy, c, f);
        char buf[12];
        cv.useFont(FONT_BODY);
        fitText(cv, buf, sizeof(buf), c.type, barX - x0 - cv.S(26));
        cv.text(buf, x0 + cv.S(19), cy + 1, FONT_BODY, c.active ? f.txt : f.dim, lgfx::textdatum_t::middle_left);
        const int16_t bh = cv.S(6);
        cv.rrect(barX, cy - bh / 2, barW, bh, bh / 2, f.track);
        if (c.known && c.remain >= 0) {
          const int16_t fw = (int16_t)((int32_t)barW * (c.remain > 100 ? 100 : c.remain) / 100);
          if (fw >= bh) cv.rrect(barX, cy - bh / 2, fw, bh, bh / 2, c.color == f.bg ? f.txt : c.color);
          snprintf(buf, sizeof(buf), "%d%%", c.remain);
        } else {
          strlcpy(buf, "--", sizeof(buf));
        }
        cv.text(buf, x0 + w, cy + 1, FONT_BODY, f.dim, lgfx::textdatum_t::middle_right);
      }
      return;
    }
    int16_t x = x0, cy = rule + g.cellBaseOff - 7;
    unsigned hidden = f.chipMore;
    cv.useFont(FONT_BODY);
    const int16_t moreW = cv.width("+8") + 8;
    for (uint8_t i = 0; i < f.chipCount; i++) {
      const CardChip& c = f.chips[i];
      char pct[6] = "";
      if (c.known && c.remain >= 0) snprintf(pct, sizeof(pct), "%d%%", c.remain);
      cv.useFont(FONT_CARD_LBL);
      const int16_t pw = pct[0] ? cv.width(pct) + 4 : 0;
      cv.useFont(FONT_BODY);
      const int16_t need = cv.S(19) + cv.width(c.type) + pw;
      const bool canWrap = cy + cv.S(24) + 8 <= yMax;
      if (x > x0 && x + need > x0 + w && canWrap) { x = x0; cy += cv.S(24); }
      // Keep room for a "+N" unless this is the last chip and nothing is hidden.
      const bool reserve = (i + 1 < f.chipCount) || hidden;
      if (x + need > x0 + w - (reserve && !canWrap ? moreW : 0)) { hidden += f.chipCount - i; break; }
      drawChipDot(cv, x + cv.S(7), cy, c, f);
      cv.text(c.type, x + cv.S(19), cy + 1, FONT_BODY, c.active ? f.txt : f.dim, lgfx::textdatum_t::middle_left);
      if (pct[0]) cv.text(pct, x + need, cy + 1, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::middle_right);
      cv.useFont(FONT_BODY);
      x += need + 12;
    }
    if (hidden) {
      char more[8];
      snprintf(more, sizeof(more), "+%u", hidden);
      cv.text(more, x0 + w, cy + 1, FONT_BODY, f.dim, lgfx::textdatum_t::middle_right);
    }
    return;
  }
  if (f.cellCount == 0) return;
  const int16_t cols = g.cellCols ? g.cellCols : f.cellCount;
  // AMS row in the band: one row of temperatures, the slots below it.
  const uint8_t nCells = f.amsInBand ? (uint8_t)std::min<int16_t>(f.cellCount, cols) : f.cellCount;
  if (f.amsInBand) {
    const int16_t top = rule + g.cellRowH;
    cv.text(f.amsLabel, x0, top + g.cellLblOff, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    if (f.amsHum[0])
      cv.text(f.amsHum, x0 + w, top + g.cellLblOff, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_right);
    const int16_t slotW = w / 4, h = cv.S(19);
    const int16_t y = top + g.cellBaseOff - h + 3;
    uint8_t n = 0;
    for (uint8_t r = 0; r < f.trayCount + (f.extShow ? 1 : 0) && n < 4; r++, n++) {
      const CardTray& t = (r < f.trayCount) ? f.trays[r] : f.ext;
      drawSlot(cv, t, f, x0 + n * slotW, y, slotW - 6, h);
    }
  }
  // One row: each cell gets what its label/value needs plus an even share of
  // the slack, so a "250" next to "33" no longer runs into it. Grid: equal.
  int16_t cellX[LY_CARD_TEMP_MAX + 1];
  cellX[0] = x0;
  if (!g.cellCols) {
    int16_t need[LY_CARD_TEMP_MAX], sum = 0;
    for (uint8_t i = 0; i < nCells; i++) {
      const CardCell& c = f.cells[i];
      // Size from a digit template, not the live value: proportional digits
      // made the whole row shift by a pixel or two as a nozzle warmed up.
      // Nozzles always reserve three digits.
      const bool nozzle = strncmp(c.lblShort, "NOZ", 3) == 0;
      const char* tmpl = (nozzle || c.val >= 100 || c.val <= -10) ? "888" : "88";
      cv.useFont(FONT_LARGE);
      int16_t vw = cv.width(tmpl) + cv.S(c.unit == CU_DEG ? 8 : 16);
      cv.useFont(FONT_CARD_LBL);
      int16_t lw = cv.width(c.lblShort);
      need[i] = std::max(vw, lw);
      sum += need[i];
    }
    int16_t slack = (w - sum) / nCells;
    if (slack < 0) slack = 0;
    for (uint8_t i = 0; i < nCells; i++) cellX[i + 1] = cellX[i] + need[i] + slack;
  } else {
    for (uint8_t i = 0; i < nCells; i++) cellX[i + 1] = x0 + ((i + 1) % cols) * (w / cols);
  }
  // One label length for the whole band: "NOZZLE L" beside "NOZ R" reads as a bug.
  bool shortLbl = false;
  cv.useFont(FONT_CARD_LBL);
  for (uint8_t i = 0; i < nCells && !shortLbl; i++) {
    const int16_t cw = g.cellCols ? (w / cols) : (cellX[i + 1] - cellX[i]);
    shortLbl = cv.width(f.cells[i].lbl) > cw - 4;
  }
  for (uint8_t i = 0; i < nCells; i++) {
    const CardCell& c = f.cells[i];
    const int16_t x = g.cellCols ? (x0 + (i % cols) * (w / cols)) : cellX[i];
    const int16_t cw = g.cellCols ? (w / cols) : (cellX[i + 1] - cellX[i]);
    const int16_t top = rule + (i / cols) * g.cellRowH;
    const int16_t base = top + g.cellBaseOff;
    if (base > yMax) break;
    cv.useFont(FONT_CARD_LBL);
    const char* lbl = shortLbl ? c.lblShort : c.lbl;
    cv.text(lbl, x, top + g.cellLblOff, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    char v[8];
    snprintf(v, sizeof(v), "%d", c.val);
    uint16_t vc = c.accent ? f.nozAccent : c.vclr;
    cv.text(v, x, base, FONT_LARGE, vc, lgfx::textdatum_t::baseline_left);
    cv.useFont(FONT_LARGE);
    int16_t vx = x + cv.width(v) + 1;
    if (c.unit == CU_DEG) {
      drawDeg(cv, vx, base - g.valCap, f.dim);
    } else if (c.unit == CU_PCT) {
      cv.text("%", vx, base, FONT_BODY, f.dim, lgfx::textdatum_t::baseline_left);
    } else {
      cv.text("/5", vx, base, FONT_BODY, f.dim, lgfx::textdatum_t::baseline_left);
    }
  }
}

// Hero column: beside the AMS column / thumbnail in landscape, full width otherwise.
static void heroX(const CardFrame& f, const CardGeo& g, int16_t& x, int16_t& w) {
  if (g.amsColW && f.thumbShow)     x = g.pad + g.thumbSize + (g.k > 100 ? 21 : 14);
  else if (g.amsColW && f.amsShow)  x = g.pad + g.amsColW + (g.k > 100 ? 21 : 14);
  else                              x = g.pad;
  w = g.W - g.pad - x;
}

// Portrait / square with a thumbnail: the thumb sits right of the percent, so the
// bar and the line move below it and REMAINING gets its own row or the ETA slot.
static bool besideThumb(const CardFrame& f, const CardGeo& g) { return f.thumbShow && !g.amsColW; }
static int16_t barY(const CardFrame& f, const CardGeo& g) { return besideThumb(f, g) ? g.tbBarY : g.barY; }

static int16_t bottomRule(const CardFrame& f, const CardGeo& g) {
  if (besideThumb(f, g)) return g.tbRule;
  return (f.amsShow && !g.amsColW) ? g.botRuleAms : g.botRuleNoAms;
}

static void drawThumb(Cv& cv, int16_t x, int16_t y, int16_t size) {
  if (!g_thumbPx) return;
  cv.g->pushImage(x - cv.ox, y - cv.oy, size, size, (const lgfx::swap565_t*)g_thumbPx);
}

// Progress bar, optionally with the shimmer highlight centred at shimmerX.
static uint16_t blend565(uint8_t a, uint16_t fg, uint16_t bg) {
  uint32_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (255 - a)) / 255;
  uint32_t gr = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (255 - a)) / 255;
  uint32_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (255 - a)) / 255;
  return (uint16_t)((r << 11) | (gr << 5) | b);
}

static const int16_t SHIMMER_HALF = 12;   // highlight half-width, px

static void drawBar(Cv& cv, const CardFrame& f, const CardGeo& g, int16_t shimmerX) {
  int16_t hx, hw;
  heroX(f, g, hx, hw);
  const int16_t by = barY(f, g);
  cv.rrect(hx, by, hw, g.barH, g.barH / 2, f.track);
  int16_t fw = (int16_t)((int32_t)hw * (f.pct > 100 ? 100 : f.pct) / 100);
  if (fw < g.barH) return;
  cv.rrect(hx, by, fw, g.barH, g.barH / 2, f.bar);
  if (shimmerX < 0) return;
  // Soft triangular highlight toward white, kept off the rounded ends.
  const int16_t lo = hx + g.barH / 2, hi = hx + fw - g.barH / 2;
  for (int16_t x = shimmerX - SHIMMER_HALF; x <= shimmerX + SHIMMER_HALF; x++) {
    if (x < lo || x >= hi) continue;
    int16_t d = abs(x - shimmerX);
    uint8_t a = (uint8_t)(170 * (SHIMMER_HALF - d) / SHIMMER_HALF);
    cv.rect(x, by, 1, g.barH, blend565(a, CLR_TEXT_DEFAULT, f.bar));
  }
}

static void scenePrinting(Cv& cv, const CardFrame& f, const CardGeo& g) {
  drawHeader(cv, f, g);
  if (f.amsShow) { if (g.amsColW) drawAmsColumn(cv, f, g); else drawAmsStrip(cv, f, g); }
  const bool pThumb = besideThumb(f, g);
  if (f.thumbShow) {
    if (g.amsColW) {
      drawThumb(cv, g.pad, g.hdrRule + cv.S(10), g.thumbSize);
      int16_t rx = g.pad + g.thumbSize + 7;
      cv.vline(rx, g.hdrRule + 8, g.botRuleAms - g.hdrRule - 16, f.track);
    } else {
      drawThumb(cv, g.W - g.pad - g.thumbSize, g.thumbY, g.thumbSize);
    }
  }
  int16_t hx, hw;
  heroX(f, g, hx, hw);
  const int16_t hr = hx + hw;

  char buf[64];
  // Job name in the 1x body face on every tier: at 1.5x it outweighed the hero.
  cv.noTier = true;
  cv.useFont(FONT_BODY);
  if (cv.width(f.job) > hw) {
    // Too long: drawn at the marquee's current offset (tickCardMarquee scrolls it),
    // clipped to the hero column so a full repaint lands on the same frame.
    cv.g->setClipRect(hx - cv.ox, g.nameCy - 10 - cv.oy, hw, 20);
    cv.text(f.job, hx - g_marqueeOff, g.nameCy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);
    cv.g->clearClipRect();
  } else {
    cv.text(f.job[0] ? f.job : "--", hx, g.nameCy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);
  }
  cv.noTier = false;

  // Big percent
  char pct[6];
  snprintf(pct, sizeof(pct), "%u", f.pct);
  cv.text(pct, hx - 2, g.bigBase, FONT_CARD_NUM, f.txt, lgfx::textdatum_t::baseline_left);
  cv.useFont(FONT_CARD_NUM);
  int16_t px = hx - 2 + cv.width(pct) + 3;
  cv.text("%", px, g.bigBase, FONT_LARGE, f.dim, lgfx::textdatum_t::baseline_left);
  cv.useFont(FONT_LARGE);
  const int16_t pctEnd = px + cv.width("%");

  // Remaining: top right; with a beside-thumb either its own row under the line
  // or the line's right end (ETA dropped) where there is no room for a row.
  const int16_t cy = pThumb ? g.tbLineCy : g.lineCy;
  const bool remOnLine = pThumb && g.tbRemOnLine;
  FontID remLblFont;
  if (!remOnLine) {
    const int16_t remLbl = pThumb ? g.tbRemLblY : g.remLblY;
    const int16_t remBase = pThumb ? g.tbRemBase : g.bigBase;
    // The label at body size where the top-right corner has room; the small
    // caps face when a wide percent ("100%") would run into it.
    cv.useFont(FONT_BODY);
    remLblFont = (g.k <= 100 && (pThumb || hr - cv.width("REMAINING") > pctEnd + 8))
                     ? FONT_BODY : FONT_CARD_LBL;   // body face only at 1x - too big at 1.5x
    if (pThumb) cv.text("REMAINING", hx, remLbl + cv.S(14), remLblFont, f.dim, lgfx::textdatum_t::top_left);
    else        cv.text("REMAINING", hr, remLbl - (remLblFont == FONT_BODY ? cv.S(3) : 0), remLblFont, f.dim,
                        lgfx::textdatum_t::top_right);
    if (f.remainMin > 0) {
      drawDuration(cv, hr, remBase, f.remainMin, f);
    } else {
      cv.text("--", hr, remBase, FONT_LARGE, f.dim, lgfx::textdatum_t::baseline_right);
    }
  }

  drawBar(cv, f, g, -1);

  // Active filament sits right after the layer count (the landscape thumb
  // column has its own spot under the thumbnail).
  const bool filOnLine = f.showActiveFil && f.activeFil.known && !(f.thumbShow && g.amsColW);

  // ETA (right) first, then the stage label or layer count (left) in what is left.
  int16_t rightStart = hr;
  if (remOnLine && !f.eta[0] && f.remainMin > 0) {
    // No synced clock means no ETA - the remaining time is better than nothing.
    rightStart = hr - drawDuration(cv, hr, cy + cv.S(8), f.remainMin, f);
  } else if (f.eta[0]) {
    cv.text(f.eta, hr, cy, FONT_BODY, f.etaClr, lgfx::textdatum_t::middle_right);
    cv.useFont(FONT_BODY);
    int16_t ex = hr - cv.width(f.eta) - cv.S(5);
    if (f.etaDay[0]) {                     // ends on a later day: "+1" before the time
      cv.text(f.etaDay, ex, cy + 1, FONT_CARD_LBL, f.etaClr, lgfx::textdatum_t::middle_right);
      cv.useFont(FONT_CARD_LBL);
      ex -= cv.width(f.etaDay) + cv.S(5);
    }
    cv.text("ETA", ex, cy + 1, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::middle_right);
    cv.useFont(FONT_CARD_LBL);
    rightStart = ex - cv.width("ETA");
  }
  int16_t leftEnd = hx;
  if (f.stage[0]) {
    cv.useFont(FONT_BODY);
    fitText(cv, buf, sizeof(buf), f.stage, rightStart - hx - cv.S(10));
    cv.text(buf, hx, cy, FONT_BODY, f.accent, lgfx::textdatum_t::middle_left);
    leftEnd = hx + cv.width(buf);
  } else if (f.layers > 0) {
    int16_t x = hx;
    snprintf(buf, sizeof(buf), "%u", f.layer);
    cv.text(buf, x, cy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);
    cv.useFont(FONT_BODY);
    x += cv.width(buf);
    snprintf(buf, sizeof(buf), " / %u", f.layers);
    cv.text(buf, x, cy, FONT_BODY, f.dim, lgfx::textdatum_t::middle_left);
    leftEnd = x + cv.width(buf);
  }
  if (f.showActiveFil && f.activeFil.known && f.thumbShow && g.amsColW) {
    // Landscape: the column under the thumbnail, below its own rule.
    const int16_t ry = g.hdrRule + cv.S(10) + g.thumbSize + cv.S(10);
    cv.hline(g.pad, ry, g.thumbSize, f.track);
    drawActiveFil(cv, f, g.pad, ry + (g.botRuleAms - ry) / 2, g.thumbSize);
  } else if (filOnLine && rightStart - leftEnd > cv.S(40)) {
    drawActiveFil(cv, f, leftEnd + cv.S(10), cy, rightStart - leftEnd - cv.S(18));
  }
  drawBottom(cv, f, g, bottomRule(f, g));
}

static void sceneFinished(Cv& cv, const CardFrame& f, const CardGeo& g) {
  drawHeader(cv, f, g);
  const bool col = (f.amsShow || f.thumbShow) && g.amsColW;
  if (f.thumbShow && g.amsColW) {
    drawThumb(cv, g.pad, g.hdrRule + cv.S(10), g.thumbSize);
    cv.vline(g.pad + g.thumbSize + 7, g.hdrRule + 8, g.finBotRule - g.hdrRule - 16, f.track);
  } else if (col) {
    drawAmsColumn(cv, f, g);
  }
  int16_t hx, hw0;
  heroX(f, g, hx, hw0);
  (void)hw0;
  if (!col) hx = g.pad;
  int16_t hw = g.W - g.pad - hx;
  // Portrait / square: thumbnail top right, the text column narrows beside it.
  if (f.thumbShow && !g.amsColW) {
    drawThumb(cv, g.W - g.pad - g.thumbSize, g.hdrRule + 8, g.thumbSize);
    hw -= g.thumbSize + 8;
  }
  char buf[64];
  cv.useFont(FONT_LARGE);
  fitText(cv, buf, sizeof(buf), f.head, hw);
  cv.text(buf, hx, g.finHeadCy, FONT_LARGE, f.headColor, lgfx::textdatum_t::middle_left);
  cv.text("LAST PRINT", hx, g.finLblY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
  cv.useFont(FONT_BODY);
  fitText(cv, buf, sizeof(buf), f.job[0] ? f.job : "--", hw);
  cv.text(buf, hx, g.finNameCy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);

  int16_t x = hx;
  if (f.doneClock[0]) {
    cv.text("DONE", x, g.finRowY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    cv.text(f.doneClock, x, g.finRowCy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);
    cv.useFont(FONT_BODY);
    x += std::max<int16_t>(cv.width(f.doneClock), 30) + 18;
  }
  if (f.kwh[0]) {
    cv.text("ENERGY", x, g.finRowY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    cv.text(f.kwh, x, g.finRowCy, FONT_BODY, f.txt, lgfx::textdatum_t::middle_left);
  }
  if (f.doorWait)
    cv.text("Open door to dismiss", hx, g.finDoorCy, FONT_BODY, CLR_ORANGE, lgfx::textdatum_t::middle_left);
  drawBottom(cv, f, g, g.finBotRule);
}

static void sceneIdle(Cv& cv, const CardFrame& f, const CardGeo& g) {
  drawHeader(cv, f, g);
  const int16_t x0 = g.pad, xr = g.W - g.pad;
  cv.text(f.head, x0, g.idleHeadCy, FONT_LARGE, f.headColor, lgfx::textdatum_t::middle_left);
  char buf[64];
  cv.useFont(FONT_BODY);
  fitText(cv, buf, sizeof(buf), f.job, g.idleClockRight == 1 ? cv.S(170) : xr - x0);
  cv.text(buf, x0, g.idleSubCy, FONT_BODY, f.dim, lgfx::textdatum_t::middle_left);
  if (f.clock[0]) {
    if (g.idleClockRight == 2) {                     // square: small clock beside the headline
      char clk[16];
      snprintf(clk, sizeof(clk), f.ampm[0] ? "%s %s" : "%s", f.clock, f.ampm);
      cv.text(clk, xr, g.idleClockBase, FONT_LARGE, f.dim, lgfx::textdatum_t::middle_right);
    } else if (g.idleClockRight) {
      int16_t x = xr;
      if (f.ampm[0]) {
        cv.text(f.ampm, x, g.idleClockBase, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::baseline_right);
        cv.useFont(FONT_CARD_LBL);
        x -= cv.width(f.ampm) + 4;
      }
      cv.text(f.clock, x, g.idleClockBase, FONT_CARD_NUM, f.dim, lgfx::textdatum_t::baseline_right);
    } else {
      cv.text(f.clock, x0 - 2, g.idleClockBase, FONT_CARD_NUM, f.dim, lgfx::textdatum_t::baseline_left);
      if (f.ampm[0]) {
        cv.useFont(FONT_CARD_NUM);
        cv.text(f.ampm, x0 + cv.width(f.clock) + 4, g.idleClockBase, FONT_CARD_LBL, f.dim,
                lgfx::textdatum_t::baseline_left);
      }
    }
  }
  if (f.swCount) {
    cv.text("AMS", x0, g.idleAmsY, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::top_left);
    const int16_t s = g.idleSw;
    const int16_t yLimit = g.idleBotRule - 4;
    int16_t x = x0, y = g.idleAmsY + cv.S(14);
    const int16_t gap = cv.S(4), htW = cv.S(22);
    for (uint8_t i = 0; i < f.swCount; i++) {
      const CardTray& t = f.sw[i];
      // Width of the block starting here (one unit, or the "HT" label + HT swatches).
      if (t.type[0] == '|' || t.type[0] == '/' || i == 0) {
        bool ht = (t.type[0] == '/');
        int16_t bw = ht ? htW : 0;
        uint8_t j = (t.type[0] == 'x') ? i : i + 1;
        for (; j < f.swCount && f.sw[j].type[0] == 'x'; j++) bw += s + gap;
        if (t.type[0] == '|') x += 6;
        if (x > x0 && ((ht && g.idleHtNewRow) || x + bw - 4 > xr)) { x = x0; y += s + 8; }
        if (ht) {
          if (x > x0) x += 6;
          cv.text("HT", x, y + s / 2 + 1, FONT_CARD_LBL, f.dim, lgfx::textdatum_t::middle_left);
          x += htW;
        }
        if (t.type[0] != 'x') continue;
      }
      if (y + s > yLimit || x + s > xr) continue;
      if (t.present) {
        if (t.active) cv.rrect(x - 2, y - 2, s + 4, s + 4, 5, f.txt);
        cv.rrect(x, y, s, s, 4, t.color);
        if (t.color == f.bg) cv.rrectOutline(x, y, s, s, 4, f.dim);
      } else {
        cv.rrectOutline(x, y, s, s, 4, f.dim);
      }
      x += s + gap;
    }
  }
  drawBottom(cv, f, g, g.idleBotRule);
}

static void drawScene(Cv& cv, const CardFrame& f, const CardGeo& g) {
  switch (f.kind) {
    case CK_PRINTING: scenePrinting(cv, f, g); break;
    case CK_FINISHED: sceneFinished(cv, f, g); break;
    default:          sceneIdle(cv, f, g);     break;
  }
}

// ---------------------------------------------------------------------------
//  Render + push
// ---------------------------------------------------------------------------
static bool present(bool force) {
  if (!force && g_lastValid && memcmp(&g_cur, &g_last, sizeof(CardFrame)) == 0) return true;
  const CardGeo& g = geo();
  if (lgfx::LGFX_Sprite* full = allocFull(g.W, g.H)) {
    Cv cv{full, 0, 0, FONT_NONE};
    cv.k = g.k;
    full->fillSprite(g_cur.bg);
    drawScene(cv, g_cur, g);
    full->pushSprite(&tft, 0, 0);
  } else {
    // No PSRAM frame (e.g. re-creating it for a new rotation failed): hand the
    // screen back to the classic renderer rather than leave a stale frame up.
    g_lastValid = false;
    return false;
  }
  memcpy(&g_last, &g_cur, sizeof(CardFrame));
  g_lastValid = true;
  markFrameDirty();
  return true;
}

// ---------------------------------------------------------------------------
//  Progress-bar shimmer: redraws only the bar strip, ~40 fps, honouring the
//  "Animated progress bar" setting like the LED-bar shimmer does.
// ---------------------------------------------------------------------------
static const uint16_t SHIMMER_INTERVAL = 25;    // ms per step
static const uint16_t SHIMMER_PAUSE    = 1200;  // ms between sweeps
static const int16_t  SHIMMER_STEP     = 3;     // px per step

// ---------------------------------------------------------------------------
//  Job-name marquee: a name wider than the hero column scrolls back and forth,
//  redrawing only its 20 px strip (~9 KB PSRAM sprite) about 33 px/s. The strip
//  stays clear of the header rule above it, or every step would erase it.
// ---------------------------------------------------------------------------
bool tickCardMarquee() {
  static lgfx::LGFX_Sprite* strip = nullptr;
  static unsigned long lastMs = 0, holdUntil = 0;
  static int8_t dir = 1;
  static char shown[64];
  if (!g_lastValid || g_last.kind != CK_PRINTING || !g_full) return false;

  const CardGeo& g = geo();
  int16_t hx, hw;
  heroX(g_last, g, hx, hw);
  if (!strip) {
    strip = new (std::nothrow) lgfx::LGFX_Sprite(&tft);
    if (!strip) return false;
    strip->setPsram(true);
    strip->setColorDepth(16);
  }
  const int16_t sh = 20;                     // 1x body face on every tier, as the scene
  if (strip->width() != hw || strip->height() != sh) {
    strip->deleteSprite();
    if (!strip->createSprite(hw, sh)) return false;
    loadFontInto(*strip, FONT_BODY);         // once: per-tick loads would churn the heap
  }
  const int16_t span = (int16_t)strip->textWidth(g_last.job) - hw;
  const unsigned long now = millis();
  if (strcmp(shown, g_last.job) != 0) {      // new job: start over from the left
    strlcpy(shown, g_last.job, sizeof(shown));
    g_marqueeOff = 0;
    dir = 1;
    holdUntil = now + 2000;
  }
  if (span <= 0) { g_marqueeOff = 0; return false; }
  if ((long)(now - holdUntil) < 0 || now - lastMs < 30) return false;
  lastMs = now;
  g_marqueeOff += dir;
  if (g_marqueeOff >= span || g_marqueeOff <= 0) {   // pause at either end, then turn
    g_marqueeOff = g_marqueeOff <= 0 ? 0 : span;
    dir = -dir;
    holdUntil = now + 1500;
  }
  strip->fillSprite(g_last.bg);
  strip->setTextDatum(lgfx::textdatum_t::middle_left);
  strip->setTextColor(g_last.txt);
  strip->drawString(g_last.job, -g_marqueeOff, sh / 2);
  strip->pushSprite(&tft, hx, g.nameCy - sh / 2);
  return true;
}

bool tickCardShimmer() {
  static int16_t pos = -1;                 // offset into the filled part, -1 = paused
  static unsigned long lastMs = 0, pauseStart = 0;
  static lgfx::LGFX_Sprite* strip = nullptr;
  if (!dispSettings.animatedBar || !g_lastValid || g_last.kind != CK_PRINTING || !g_last.printing) return false;

  const CardGeo& g = geo();
  int16_t hx, hw;
  heroX(g_last, g, hx, hw);
  const int16_t fw = (int16_t)((int32_t)hw * (g_last.pct > 100 ? 100 : g_last.pct) / 100);
  if (fw < 2 * SHIMMER_HALF + 8) return false;

  const unsigned long now = millis();
  if (pos < 0) {
    if (now - pauseStart < SHIMMER_PAUSE) return false;
    pos = 0;
  }
  if (now - lastMs < SHIMMER_INTERVAL) return false;
  lastMs = now;

  if (!strip) {
    strip = new lgfx::LGFX_Sprite(&tft);
    strip->setColorDepth(16);
    strip->setPsram(false);
  }
  if (strip->width() != hw || strip->height() != g.barH) {
    strip->deleteSprite();
    if (!strip->createSprite(hw, g.barH)) return false;   // ~3 KB
  }
  const int16_t by = barY(g_last, g);
  Cv cv{strip, hx, by, FONT_NONE};
  cv.k = g.k;
  strip->fillSprite(g_last.bg);
  pos += SHIMMER_STEP;
  const bool done = pos >= fw + SHIMMER_HALF;
  drawBar(cv, g_last, g, done ? -1 : hx + pos - SHIMMER_HALF);
  strip->pushSprite(&tft, hx, by);
  if (done) { pos = -1; pauseStart = now; }
  return true;
}

// ---------------------------------------------------------------------------
//  Public entry points
// ---------------------------------------------------------------------------
bool cardFrameView(const uint16_t** buf, int16_t* w, int16_t* h) {
  if (!g_full || !g_lastValid || !g_full->getBuffer()) return false;
  *buf = static_cast<const uint16_t*>(g_full->getBuffer());
  *w = (int16_t)g_full->width();
  *h = (int16_t)g_full->height();
  return true;
}

bool cardSkinActive() {
  return dispSettings.cardStyle == 1;
}

static bool clockSynced() { return time(nullptr) > (time_t)NTP_SYNCED_EPOCH; }

bool drawCardPrinting(PrinterSlot& p, bool force) {
  const BambuState& s = p.state;
  CardFrame& f = g_cur;
  snapCommon(f, CK_PRINTING, p);
  f.pct = s.progress > 100 ? 100 : s.progress;
  f.printing = (s.printing && s.gcodeStateId == GCODE_RUNNING) ? 1 : 0;
  f.remainMin = s.remainingMinutes;
  if (s.remainingMinutes > 0 && clockSynced()) {
    // Rounded to the minute so the frame does not change every second.
    time_t now = time(nullptr);
    const time_t etaT = now - (now % 60) + (time_t)s.remainingMinutes * 60;
    char ampm[4];
    formatClock(etaT, f.eta, sizeof(f.eta), ampm, sizeof(ampm));
    if (ampm[0]) { strlcat(f.eta, " ", sizeof(f.eta)); strlcat(f.eta, ampm, sizeof(f.eta)); }
    // Later day: "+N" in front for up to a week, the date beyond that.
    struct tm tn, te;
    localtime_r(&now, &tn);
    localtime_r(&etaT, &te);
    // Calendar-day difference (no mktime: DST shifts would skew a seconds diff).
    int days = te.tm_yday - tn.tm_yday;
    if (te.tm_year != tn.tm_year) {
      const int y = tn.tm_year + 1900;
      days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    }
    if (days >= 1 && days <= 6) {
      snprintf(f.etaDay, sizeof(f.etaDay), "+%d", days);
    } else if (days > 6) {
      char t[16];
      strlcpy(t, f.eta, sizeof(t));
      if (netSettings.use24h) snprintf(f.eta, sizeof(f.eta), "%02d.%02d. %s", te.tm_mday, te.tm_mon + 1, t);
      else                    snprintf(f.eta, sizeof(f.eta), "%d/%d %s", te.tm_mon + 1, te.tm_mday, t);
    }
  }
  if (const char* st = runningStageLabel(s)) strlcpy(f.stage, st, sizeof(f.stage));
  f.layer = s.layerNum;
  f.layers = s.totalLayers;
  if (tasmotaIsActiveForSlot(rotState.displayIndex))
    snprintf(f.watts, sizeof(f.watts), "%.0f W", tasmotaGetWattsForSlot(rotState.displayIndex));
  // Active filament on the layer line only when nothing else on screen shows it.
  f.showActiveFil = (!f.amsShow && !f.amsInBand && !f.bandFilaments) ? 1 : 0;
  if (f.showActiveFil) activeFilament(s, f.activeFil);
  return present(force);
}

bool drawCardFinished(PrinterSlot& p, bool force) {
  const BambuState& s = p.state;
  CardFrame& f = g_cur;
  snapCommon(f, CK_FINISHED, p);
  if (!geo().amsColW) f.amsShow = 0;      // portrait / square finish: no AMS strip
  if (printerWasCanceled(s)) {
    strlcpy(f.head, "Print canceled", sizeof(f.head)); f.headColor = CLR_YELLOW;
  } else if (s.gcodeStateId == GCODE_FAILED) {
    strlcpy(f.head, "Print failed", sizeof(f.head));   f.headColor = CLR_RED;
  } else {
    strlcpy(f.head, "Print complete", sizeof(f.head)); f.headColor = f.finish;
  }
  formatFinishClock(f.doneClock, sizeof(f.doneClock), s);
  float kwh = tasmotaGetPrintKwhUsedForSlot(rotState.displayIndex);
  if (tasmotaIsActiveForSlot(rotState.displayIndex) && kwh >= 0.0f)
    snprintf(f.kwh, sizeof(f.kwh), "%.2f kWh", kwh);
  f.doorWait = (dpSettings.doorAckEnabled && s.doorSensorPresent && !s.doorAcknowledged) ? 1 : 0;
  return present(force);
}

bool drawCardIdle(PrinterSlot& p, bool force) {
  const BambuState& s = p.state;
  CardFrame& f = g_cur;
  snapCommon(f, CK_IDLE, p);
  f.amsShow = 0;                          // idle lists every unit as swatches instead
  f.amsInBand = 0;
  if (s.gcodeStateId == GCODE_FINISH) {
    strlcpy(f.head, "Print complete", sizeof(f.head));
    f.headColor = f.finish;               // job name stays as the sub line
  } else {
    strlcpy(f.head, s.connected ? "Ready" : "Offline", sizeof(f.head));
    f.headColor = f.txt;
    strlcpy(f.job, s.connected ? "No active print" : "Waiting for printer", sizeof(f.job));
  }
  if (clockSynced()) formatClock(time(nullptr), f.clock, sizeof(f.clock), f.ampm, sizeof(f.ampm));

  // Units first, then AMS HT units. Markers in type[0]: '|' unit gap,
  // '/' start of the HT group; real swatches carry 'x'.
  const AmsState& a = s.ams;
  const uint8_t cap = sizeof(f.sw) / sizeof(f.sw[0]);
  auto mark = [&](const char* m) { if (f.swCount < cap) strlcpy(f.sw[f.swCount++].type, m, sizeof(f.sw[0].type)); };
  bool any = false;
  for (uint8_t u = 0; u < a.unitCount && u < AMS_TRAY_UNITS; u++) {
    if (!a.units[u].present || a.units[u].id >= 128) continue;
    if (any) mark("|");
    any = true;
    uint8_t n = a.units[u].trayCount ? a.units[u].trayCount : AMS_TRAYS_PER_UNIT;
    if (n > AMS_TRAYS_PER_UNIT) n = AMS_TRAYS_PER_UNIT;
    for (uint8_t t = 0; t < n && f.swCount < cap; t++) {
      uint8_t idx = u * AMS_TRAYS_PER_UNIT + t;
      fillTray(f.sw[f.swCount], a.trays[idx], a.activeTray == idx);
      f.sw[f.swCount++].type[0] = 'x';
    }
  }
  bool htGroup = false;
  for (uint8_t u = 0; u < a.unitCount && u < AMS_MAX_UNITS; u++) {
    if (!a.units[u].present || a.units[u].id < 128) continue;
    if (!htGroup) { mark("/"); htGroup = true; }
    if (f.swCount >= cap) break;
    CardTray& c = f.sw[f.swCount++];
    if (u < AMS_TRAY_UNITS) {
      uint8_t idx = u * AMS_TRAYS_PER_UNIT;
      fillTray(c, a.trays[idx], a.activeTray == idx);
    } else if (a.ovUnitId == a.units[u].id && a.ovTray.present) {
      fillTray(c, a.ovTray, a.activeTray == AMS_TRAY_OVERFLOW);   // 5th+ unit: only its feeding tray is kept
    } else {
      memset(&c, 0, sizeof(c));                                   // tray data not stored for 5th+ unit
    }
    c.type[0] = 'x';
  }
  return present(force);
}

#endif // HAS_CARD_SKIN
