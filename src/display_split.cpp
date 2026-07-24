#include "display_split.h"
#include "config.h"
#include "layout.h"

#if defined(LAYOUT_HAS_SPLIT)

#include "display_ui.h"      // tft, markFrameDirty, isDisplayForceRedraw, drawAmsBarsGauge
#include "display_gauges.h"  // arc/temp/fan/... primitives
#include "icons.h"           // drawIcon16, icon_lock / icon_unlock (door status)
#include "settings.h"        // dispSettings, GaugeType enum
#include "bambu_state.h"     // printers[], rotState, BambuState
#include "fonts.h"           // setFont, FontID
#include "tasmota.h"         // tasmotaGetWattsForSlot / tasmotaIsActiveForSlot
#include <string.h>
#include <time.h>

// Match display_ui.cpp / display_anim.cpp: themed background, not the literal
// black CLR_BG from config.h.
#ifdef CLR_BG
#undef CLR_BG
#endif
#define CLR_BG (dispSettings.bgColor)

namespace {

#if defined(LAYOUT_HAS_SPLIT_LANDSCAPE)
// Mirrors display_ui.cpp isLandscape(): rotations 1/3 swap the panel to a wide
// orientation, which switches the split to two side-by-side bands.
bool splitIsLandscape() {
  return dispSettings.rotation == 1 || dispSettings.rotation == 3;
}
#endif

struct BandGeom {
  int16_t x, w;          // band left edge + width (full width portrait, half landscape)
  int16_t top, height;   // band rect (caller clears on a force frame)
  int16_t hdrCY;         // header text center Y
  int16_t barY;          // progress bar top Y
  int16_t cols[3];       // gauge column center X (absolute)
  int16_t rows[2];       // gauge row center Y (rows[1] unused when slots <= ncols)
  int16_t footCY;        // bottom info-line center Y
  int16_t etaCY;         // dedicated ETA line center Y; 0 = fold ETA into the foot
  FontID  etaFont;       // font for the dedicated ETA line
  int16_t r, t;          // gauge radius, arc thickness
  int16_t barH;          // progress bar height
  int16_t margin;        // left/right inset for header, bar and foot
  uint8_t slots;         // gauges per band (3 / 6 portrait, 4 landscape)
  uint8_t ncols;         // columns in the gauge grid (3 portrait, 2 landscape)
};

// Per-band caches (index 0 = top band, 1 = bottom band).
uint8_t    sPrevTypes[2][6] = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
                                { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };
uint32_t   sPrevAirduct[2]  = { 0xFFFFFFFFu, 0xFFFFFFFFu };
uint8_t    sPrevHdrState[2] = { 0xFF, 0xFF };
uint8_t    sPrevBarProg[2]  = { 0xFF, 0xFF };
char       sPrevFootCenter[2][20] = { { 0 }, { 0 } };
char       sPrevEta[2][40] = { { 0 }, { 0 } };
BambuState sPrevState[2];

// Drying-band state (index 0 = top/left band, 1 = bottom/right band). A band
// renders the drying view instead of the print gauges when its printer is idle
// but an AMS unit is actively drying. sPrevMode flips the band between the two
// views; the rest is per-band change detection mirroring drawBand's caches.
uint8_t       sPrevMode[2]     = { 0xFF, 0xFF };   // 0 = print view, 1 = drying view
uint8_t       sDryIdx[2]       = { 0, 0 };         // which drying unit the band shows
unsigned long sDryRotMs[2]     = { 0, 0 };         // last unit-rotation timestamp
int8_t        sPrevDryUnit[2]  = { -1, -1 };
uint8_t       sPrevDryCount[2] = { 0xFF, 0xFF };
uint16_t      sPrevDryMin[2]   = { 0xFFFF, 0xFFFF };
int16_t       sPrevDryTemp[2]  = { -32768, -32768 };
uint8_t       sPrevDryHum[2]   = { 0xFF, 0xFF };
uint8_t       sPrevDryHumRaw[2] = { 0xFF, 0xFF };
uint8_t       sPrevDryProg[2]  = { 0xFF, 0xFF };
const unsigned long DRY_ROT_MS = 60000;            // rotate drying units every 60s

// Build the ETA / remaining string the same way the single-printer view does
// (display_ui.cpp): wall-clock finish time when the user prefers it and NTP is
// up, otherwise the remaining duration. Returns false when there is nothing to
// show (not printing / no estimate) so the caller can render a dim placeholder.
bool buildSplitEta(const BambuState& s, char* buf, size_t n, int16_t maxW) {
  if (s.remainingMinutes == 0) return false;
  // labelRemaining=false: no "Remaining:" prefix - the progress gauge already
  // labels this context and the narrow landscape bands are tight. maxW is the
  // band width, so formatEtaLine can step a long combined/cross-day value down
  // to a form that fits instead of overrunning into the neighbouring band. The
  // caller sets the ETA font before calling, so the width measurement is right.
  formatEtaLine(s.remainingMinutes, dispSettings.timeDisplayMode,
                /*labelRemaining=*/false, maxW, buf, n);
  return true;
}

// Draw the printer name left-aligned at (x, cy), trimming with a ".." suffix if
// it would run into the state dot. Assumes the caller already set the font.
void drawClippedName(const char* name, int16_t x, int16_t cy, int16_t maxW) {
  if (tft.textWidth(name) <= maxW) { tft.drawString(name, x, cy); return; }
  char buf[28];
  strlcpy(buf, name, sizeof(buf));
  utf8TrimPartial(buf);
  int n = (int)strlen(buf);
  while (n > 1) {
    // step back over one whole UTF-8 char before appending ".."
    uint8_t removed;
    do { removed = (uint8_t)buf[n - 1]; buf[--n] = '\0'; }
    while (n > 1 && (removed & 0xC0) == 0x80);
    char tmp[30];
    snprintf(tmp, sizeof(tmp), "%s..", buf);
    if (tft.textWidth(tmp) <= maxW) { tft.drawString(tmp, x, cy); return; }
  }
  tft.drawString(buf, x, cy);
}

uint16_t stateColor(uint8_t gid) {
  switch (gid) {
    case GCODE_RUNNING: return CLR_GREEN;
    case GCODE_PAUSE:   return CLR_YELLOW;
    case GCODE_FAILED:  return CLR_RED;
    case GCODE_PREPARE: return CLR_BLUE;
    default:            return CLR_TEXT_DIM;
  }
}

// Short status word shown next to the state dot in each band header.
const char* stateLabel(uint8_t gid) {
  switch (gid) {
    case GCODE_RUNNING: return "Printing";
    case GCODE_PAUSE:   return "Paused";
    case GCODE_PREPARE: return "Preparing";
    case GCODE_FINISH:  return "Done";
    case GCODE_FAILED:  return "Failed";
    case GCODE_IDLE:    return "Idle";
    default:            return "";   // UNKNOWN / OTHER: dot only, no misleading text
  }
}

// --- Drying-band helpers (mirror display_ui.cpp's idle-drying screen) --------

uint16_t dryHumidityColor(uint8_t level) {
  if (level <= 2) return CLR_GREEN;
  if (level == 3) return CLR_YELLOW;
  if (level == 4) return CLR_ORANGE;
  return CLR_RED;
}

// N-th actively drying unit (dryRemainMin > 0), wrapping to the first if idx is
// past the count. Returns -1 when nothing is drying.
int8_t dryFindUnit(const AmsState& ams, uint8_t idx) {
  uint8_t found = 0;
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++) {
    if (ams.units[i].dryRemainMin > 0) {
      if (found == idx) return (int8_t)i;
      found++;
    }
  }
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++)
    if (ams.units[i].dryRemainMin > 0) return (int8_t)i;
  return -1;
}

uint8_t dryCountUnits(const AmsState& ams) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++)
    if (ams.units[i].dryRemainMin > 0) n++;
  return n;
}

// Temperature value with a degree ring + "C", centred at (cx, cy). The VLW
// fonts have no degree glyph, so the ring is drawn (same trick as display_ui).
// numFont picks the digit size: FONT_7SEG on roomy bands, FONT_LARGE on tight
// ones; the "C" tracks it (LARGE / BODY) so the unit stays proportional.
void drawDryTempC(int16_t cx, int16_t cy, int16_t tShown, uint16_t color,
                  FontID numFont) {
  char b[8];
  snprintf(b, sizeof(b), "%d", tShown);
  const FontID unitFont = (numFont == FONT_7SEG) ? FONT_LARGE : FONT_BODY;
  const int16_t ringDY  = (numFont == FONT_7SEG) ? 12 : 7;
  const int16_t unitW   = (numFont == FONT_7SEG) ? 22 : 14;   // ring + "C" tail
  setFont(tft, numFont);
  const int16_t numW = tft.textWidth(b);
  const int16_t startX = cx - (numW + unitW) / 2;
  tft.setTextColor(color, CLR_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(b, startX, cy);
  const int16_t ux = startX + numW + 3;
  tft.drawCircle(ux + 3, cy - ringDY, 3, color);
  tft.drawCircle(ux + 3, cy - ringDY, 2, color);
  setFont(tft, unitFont);
  tft.drawString("C", ux + 8, cy);
}

// Drying view for one band: orange "Drying" header, dry-progress bar, large
// temperature, remaining time, humidity, and the AMS unit name on the foot
// line. Reuses the band geometry so it fits every split profile.
void drawDryingBand(const BambuState& s, const PrinterConfig& cfg,
                    const BandGeom& g, uint8_t bandIdx, bool force) {
  // Rotate between simultaneously-drying AMS units once a minute.
  const uint8_t dc = dryCountUnits(s.ams);
  bool unitForce = force;
  if (dc > 1 && millis() - sDryRotMs[bandIdx] >= DRY_ROT_MS) {
    sDryIdx[bandIdx] = (uint8_t)((sDryIdx[bandIdx] + 1) % dc);
    sDryRotMs[bandIdx] = millis();
    unitForce = true;
  }
  if (dc <= 1) sDryIdx[bandIdx] = 0;

  const int8_t ui = dryFindUnit(s.ams, sDryIdx[bandIdx]);
  if (ui < 0) return;                      // nothing drying (race): leave band as-is
  const AmsUnit& u = s.ams.units[ui];

  const int16_t tempShown = (int16_t)(u.temp >= 0.0f ? u.temp + 0.5f : u.temp - 0.5f);
  const bool unitChanged = unitForce ||
                           ui != sPrevDryUnit[bandIdx] ||
                           dc != sPrevDryCount[bandIdx];
  const bool bodyChanged = unitChanged ||
                           tempShown != sPrevDryTemp[bandIdx] ||
                           u.dryRemainMin != sPrevDryMin[bandIdx] ||
                           u.humidity != sPrevDryHum[bandIdx] ||
                           u.humidityRaw != sPrevDryHumRaw[bandIdx];

  uint8_t dryProg = 0;
  if (u.dryTotalMin > 0 && u.dryRemainMin <= u.dryTotalMin)
    dryProg = 100 - (uint8_t)((uint32_t)u.dryRemainMin * 100 / u.dryTotalMin);
  const bool progChanged = unitChanged || dryProg != sPrevDryProg[bandIdx];

  // --- Header: printer name (left) + orange "Drying" + dot (right) ---
  // Static while drying, so only repaint on a force frame (screen/band cleared).
  if (force) {
    markFrameDirty();
    const int16_t dotCX = g.x + g.w - g.margin - 5;
    tft.fillCircle(dotCX, g.hdrCY, 5, CLR_ORANGE);
    setFont(tft, FONT_SMALL);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    const int16_t stRight = dotCX - 9;
    tft.drawString("Drying", stRight, g.hdrCY);
    const int16_t stLeft = stRight - tft.textWidth("Drying");

    setFont(tft, FONT_BODY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    const char* name = (cfg.name[0] != '\0') ? cfg.name : "Printer";
    drawClippedName(name, g.x + g.margin, g.hdrCY, stLeft - 4 - (g.x + g.margin));
  }

  // --- Dry-progress bar (orange) ---
  if (progChanged) {
    markFrameDirty();
    const int16_t bx = g.x + g.margin;
    const int16_t bw = g.w - 2 * g.margin;
    const int16_t fw = (int16_t)((int32_t)bw * dryProg / 100);
    tft.fillRect(bx, g.barY, bw, g.barH, tft.color565(40, 40, 40));
    if (fw > 0) tft.fillRect(bx, g.barY, fw, g.barH, CLR_ORANGE);
    sPrevDryProg[bandIdx] = dryProg;
  }

  // --- Body: temperature, remaining time, humidity, AMS unit name ---
  if (bodyChanged) {
    markFrameDirty();
    // Body starts below the header (hdrCY) - the header is painted once on a
    // force frame, so clearing it here every value change would erase it.
    const int16_t bodyTop = g.hdrCY + 13;
    const int16_t nameY   = g.footCY;            // AMS unit label on the foot line
    const int16_t bodyBot = nameY - 12;
    const int16_t bodyH   = bodyBot - bodyTop;
    tft.fillRect(g.x, bodyTop, g.w, bodyBot - bodyTop, CLR_BG);
    // 7-seg temperature where the band is tall enough; LARGE on cramped bands.
    const FontID tempFont = (bodyH >= 84) ? FONT_7SEG : FONT_LARGE;

    char timeBuf[16];
    {
      uint16_t h = u.dryRemainMin / 60, m = u.dryRemainMin % 60;
      if (h > 0) snprintf(timeBuf, sizeof(timeBuf), "%dh %02dm", h, m);
      else       snprintf(timeBuf, sizeof(timeBuf), "%dm", m);
    }
    char humBuf[8];
    snprintf(humBuf, sizeof(humBuf), "%d%%", u.humidityRaw);
    const uint16_t humClr = dryHumidityColor(u.humidity);

    if (g.w >= 210) {
      // Wide band: temperature left, remaining/humidity stacked right. Row Y
      // positions are body-relative fractions so the short 240x240 band packs
      // the four lines without overlap and the tall 320x480 band stays balanced.
      const int16_t leftCx  = g.x + g.w * 30 / 100;
      const int16_t rightCx = g.x + g.w * 70 / 100;
      drawDryTempC(leftCx, bodyTop + bodyH / 2, tempShown, CLR_ORANGE, tempFont);

      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_SMALL);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Remaining", rightCx, bodyTop + bodyH * 15 / 100);
      setFont(tft, FONT_BODY);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString(timeBuf, rightCx, bodyTop + bodyH * 37 / 100);
      setFont(tft, FONT_SMALL);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Humidity", rightCx, bodyTop + bodyH * 62 / 100);
      setFont(tft, FONT_BODY);
      tft.setTextColor(humClr, CLR_BG);
      tft.drawString(humBuf, rightCx, bodyTop + bodyH * 84 / 100);
    } else {
      // Narrow band (landscape half-width): stack temp / time / humidity.
      const int16_t cx = g.x + g.w / 2;
      drawDryTempC(cx, bodyTop + bodyH * 22 / 100, tempShown, CLR_ORANGE, tempFont);
      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_BODY);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString(timeBuf, cx, bodyTop + bodyH * 58 / 100);
      char humLine[12];
      snprintf(humLine, sizeof(humLine), "Hum %s", humBuf);
      tft.setTextColor(humClr, CLR_BG);
      tft.drawString(humLine, cx, bodyTop + bodyH * 86 / 100);
    }

    // AMS unit name on the foot line. HT units report id >= 128.
    const bool isHT = (u.id >= 128);
    const uint8_t num = isHT ? (uint8_t)(u.id - 128 + 1) : (uint8_t)(u.id + 1);
    char unitName[64];
    formatAmsDryName(unitName, sizeof(unitName), isHT, num, sDryIdx[bandIdx], dc);
    tft.fillRect(g.x, nameY - 9, g.w, 18, CLR_BG);
    tft.setTextDatum(MC_DATUM);
    setFont(tft, FONT_SMALL);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    char eb[40];
    ellipsizeToWidth(tft, unitName, g.w - 4, eb, sizeof(eb));
    tft.drawString(eb, g.x + g.w / 2, nameY);

    sPrevDryTemp[bandIdx]   = tempShown;
    sPrevDryMin[bandIdx]    = u.dryRemainMin;
    sPrevDryHum[bandIdx]    = u.humidity;
    sPrevDryHumRaw[bandIdx] = u.humidityRaw;
  }

  sPrevDryUnit[bandIdx]  = ui;
  sPrevDryCount[bandIdx] = dc;
}

void drawBand(const BambuState& s, const PrinterConfig& cfg, uint8_t slotIndex,
              const BandGeom& g, uint8_t bandIdx, bool force) {
  BambuState& prev = sPrevState[bandIdx];

  // A band shows the drying view when its printer is idle but an AMS unit is
  // drying; otherwise the normal print gauges. Switching views (same pair, one
  // printer just started/stopped drying) without a global force frame leaves the
  // other view's pixels behind, so wipe the band and force a full repaint.
  const uint8_t modeNow = (s.ams.anyDrying && !s.printing) ? 1 : 0;
  if (sPrevMode[bandIdx] != modeNow && !force) {
    markFrameDirty();
    tft.fillRect(g.x, g.top, g.w, g.height, CLR_BG);
    force = true;
  }
  sPrevMode[bandIdx] = modeNow;
  if (modeNow == 1) { drawDryingBand(s, cfg, g, bandIdx, force); return; }

  // --- Header: printer name (left) + state dot (right) ---
  if (force || s.gcodeStateId != sPrevHdrState[bandIdx]) {
    markFrameDirty();
    // On a force frame the whole screen was just cleared; otherwise wipe the
    // text strip so the new name/dot does not overprint the old.
    if (!force) tft.fillRect(g.x, g.hdrCY - 9, g.w, 18, CLR_BG);

    // Right: state dot + status word ("Printing" / "Idle" / "Preparing" ...).
    const uint16_t stClr = stateColor(s.gcodeStateId);
    const int16_t dotCX = g.x + g.w - g.margin - 5;
    tft.fillCircle(dotCX, g.hdrCY, 5, stClr);
    const char* st = stateLabel(s.gcodeStateId);
    int16_t stLeft = dotCX - 10;   // name still clears the dot when status is empty
    if (st[0] != '\0') {
      setFont(tft, FONT_SMALL);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(stClr, CLR_BG);
      const int16_t stRight = dotCX - 9;   // 4px gap left of the dot
      tft.drawString(st, stRight, g.hdrCY);
      stLeft = stRight - tft.textWidth(st);
    }

    // Left: printer name, clipped so it stops before the status word / dot.
    setFont(tft, FONT_BODY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    const char* name = (cfg.name[0] != '\0') ? cfg.name : "Printer";
    const int16_t nameMaxW = stLeft - 4 - (g.x + g.margin);
    drawClippedName(name, g.x + g.margin, g.hdrCY, nameMaxW);
    sPrevHdrState[bandIdx] = s.gcodeStateId;
  }

  // --- Thin progress bar under the header ---
  if (force || s.progress != sPrevBarProg[bandIdx]) {
    markFrameDirty();
    const int16_t bx = g.x + g.margin;
    const int16_t bw = g.w - 2 * g.margin;
    const int16_t fw = (int16_t)((int32_t)bw * (s.progress > 100 ? 100 : s.progress) / 100);
    tft.fillRect(bx, g.barY, bw, g.barH, tft.color565(40, 40, 40));
    if (fw > 0) tft.fillRect(bx, g.barY, fw, g.barH, dispSettings.progress.arc);
    sPrevBarProg[bandIdx] = s.progress;
  }

  // --- Gauge row(s) ---
  for (uint8_t si = 0; si < g.slots; si++) {
    uint8_t gt = cfg.gaugeSlots[si];
    if (gt >= GAUGE_TYPE_COUNT) gt = GAUGE_EMPTY;
    const int16_t cx = g.cols[si % g.ncols];
    const int16_t cy = g.rows[si / g.ncols];

    const bool typeChanged = (gt != sPrevTypes[bandIdx][si]);
    if (typeChanged) {
      // Screen is already clear on a force frame; only clear the tile box +
      // label band on an in-place type change (same approach as drawPrinting).
      if (!force) {
        const int16_t clearSz = g.r * 2 + 4;
        tft.fillRect(cx - g.r - 2, cy - g.r - 2, clearSz, clearSz, CLR_BG);
        const bool sm = dispSettings.smallLabels;
        const int16_t labelY = cy + g.r + (sm ? 3 : -1);
        const int16_t lh = sm ? 18 : 24;
        tft.fillRect(cx - g.r - 2, labelY - lh / 2, g.r * 2 + 4, lh, CLR_BG);
      }
      sPrevTypes[bandIdx][si] = gt;
    }

    bool needDraw = force || typeChanged || gaugeTileValueChanged(gt, s, prev);
    // Refresh Chamber/Exhaust + Aux/L.Aux labels when the airduct mask first
    // lands (it starts 0 and gets bits OR'd in on the first pushall).
    if (!needDraw && (gt == GAUGE_CHAMBER_FAN || gt == GAUGE_AUX_FAN) &&
        sPrevAirduct[bandIdx] != s.airductFuncs) {
      needDraw = true;
    }
    if (!needDraw) continue;

    markFrameDirty();
    drawGaugeTile(gt, s, slotIndex, cx, cy, g.r, g.t, force || typeChanged, false);
  }

  // --- ETA line + bottom info line ---
  {
    // Active-tray filament swatch (shown in both foot variants).
    uint16_t swColor = 0;
    const char* swType = nullptr;
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS &&
        s.ams.trays[s.ams.activeTray].present) {
      swColor = s.ams.trays[s.ams.activeTray].colorRgb565;
      swType  = s.ams.trays[s.ams.activeTray].type;
    } else if (s.ams.activeTray == AMS_TRAY_OVERFLOW && s.ams.ovTray.present) {
      swColor = s.ams.ovTray.colorRgb565;
      swType  = s.ams.ovTray.type;
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      swColor = s.ams.vtColorRgb565;
      swType  = s.ams.vtType;
    }

    char etaStr[40];
    setFont(tft, g.etaFont);   // set before building: formatEtaLine measures to fit
    const bool hasEta = buildSplitEta(s, etaStr, sizeof(etaStr), g.w - 4);
    if (!hasEta) strlcpy(etaStr, "ETA: --", sizeof(etaStr));
    const uint16_t etaClr = hasEta ? CLR_GREEN : CLR_TEXT_DIM;

    if (g.etaCY > 0) {
      // --- Roomy: dedicated ETA line, plus the full foot bar below it. ---
      if (force || strcmp(etaStr, sPrevEta[bandIdx]) != 0) {
        markFrameDirty();
        strlcpy(sPrevEta[bandIdx], etaStr, sizeof(sPrevEta[bandIdx]));
        setFont(tft, g.etaFont);
        const int16_t eh = tft.fontHeight();
        tft.fillRect(g.x, g.etaCY - eh / 2 - 1, g.w, eh + 2, CLR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(etaClr, CLR_BG);
        tft.drawString(etaStr, g.x + g.w / 2, g.etaCY);
      }

      // Foot bar: filament swatch + type (left), layers/power (centre), door (right).
      // The centre readout can be hidden (shown as gauges) to free width for the
      // filament name - matches the single-printer status bar in drawPrinting().
      const bool hideReadout = dispSettings.hideStatusReadout;
      char cbuf[20];
      cbuf[0] = '\0';
      if (!hideReadout) {
        // displayMode 2 = "Always show layer count" (power lives on a gauge):
        // never swap to watts even when the plug is active.
        const float w = tasmotaGetWattsForSlot(slotIndex);
        if (tasmotaDisplayModeForSlot(slotIndex) != 2 &&
            tasmotaIsActiveForSlot(slotIndex) && w > 0.5f) {
          snprintf(cbuf, sizeof(cbuf), "%.0fW", w);
        } else {
          snprintf(cbuf, sizeof(cbuf), "%d/%d", s.layerNum, s.totalLayers);
        }
      }
      bool footChanged = force
        || s.doorOpen != prev.doorOpen
        || s.doorSensorPresent != prev.doorSensorPresent
        || s.ams.activeTray != prev.ams.activeTray
        || strcmp(cbuf, sPrevFootCenter[bandIdx]) != 0;
      if (footChanged) {
        markFrameDirty();
        strlcpy(sPrevFootCenter[bandIdx], cbuf, sizeof(sPrevFootCenter[bandIdx]));
        const int16_t fy = g.footCY;
        const int16_t fcx = g.x + g.w / 2;
        tft.fillRect(g.x, fy - 8, g.w, 16, CLR_BG);
        setFont(tft, FONT_SMALL);

        // Left edge the filament name clamps to: the centre readout when shown,
        // otherwise the right indicator (door) or the band edge.
        int16_t centerLeft;
        if (hideReadout) {
          if (s.doorSensorPresent) {
            const int16_t lbl = gaugeLabels.door[0] ? tft.textWidth(gaugeLabels.door) : 0;
            centerLeft = g.x + g.w - g.margin - 18 - lbl;  // door label + icon
          } else {
            centerLeft = g.x + g.w - g.margin;             // band right edge
          }
        } else {
          // Centre string drawn first so the left filament label can clamp to it.
          centerLeft = fcx - tft.textWidth(cbuf) / 2;
          tft.setTextDatum(MC_DATUM);
          tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
          tft.drawString(cbuf, fcx, fy);
        }

        const int16_t lx = g.x + g.margin;
        if (swType) {
          tft.drawCircle(lx + 5, fy, 5, CLR_TEXT_DARK);
          tft.fillCircle(lx + 5, fy, 4, swColor);
          const int16_t typeMaxW = centerLeft - 3 - (lx + 14);
          if (typeMaxW > 12) {
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
            drawClippedName(swType, lx + 14, fy, typeMaxW);
          }
        }

        if (s.doorSensorPresent) {
          uint16_t clr = s.doorOpen ? CLR_ORANGE : CLR_GREEN;
          tft.setTextDatum(MR_DATUM);
          tft.setTextColor(clr, CLR_BG);
          if (gaugeLabels.door[0]) tft.drawString(gaugeLabels.door, g.x + g.w - g.margin - 18, fy);
          drawIcon16(tft, g.x + g.w - g.margin - 16, fy - 8,
                     s.doorOpen ? icon_unlock : icon_lock, clr);
        }
      }
    } else {
      // --- Cramped: foot = colour swatch + ETA only (no type / layers / door),
      // so a long multi-day ETA gets the full band width. ---
      bool footChanged = force
        || s.ams.activeTray != prev.ams.activeTray
        || strcmp(etaStr, sPrevEta[bandIdx]) != 0;
      if (footChanged) {
        markFrameDirty();
        strlcpy(sPrevEta[bandIdx], etaStr, sizeof(sPrevEta[bandIdx]));
        const int16_t fy = g.footCY;
        setFont(tft, g.etaFont);
        const int16_t eh = tft.fontHeight();
        tft.fillRect(g.x, fy - eh / 2 - 1, g.w, eh + 2, CLR_BG);
        if (swType) {
          const int16_t lx = g.x + g.margin;
          tft.drawCircle(lx + 5, fy, 5, CLR_TEXT_DARK);
          tft.fillCircle(lx + 5, fy, 4, swColor);
        }
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(etaClr, CLR_BG);
        tft.drawString(etaStr, g.x + g.w / 2, fy);
      }
    }
  }

  sPrevAirduct[bandIdx] = s.airductFuncs;
  memcpy(&prev, &s, sizeof(BambuState));
}

}  // namespace

void drawSplit() {
  const bool force = isDisplayForceRedraw();

  uint8_t a = rotState.displayIndex;
  if (a >= MAX_PRINTERS) a = 0;
  uint8_t b = rotState.splitIndexB;
  if (b >= MAX_PRINTERS || b == a) b = (a == 0) ? 1 : 0;

  BandGeom ga, gb;

#if defined(LAYOUT_HAS_SPLIT_LANDSCAPE)
  if (splitIsLandscape()) {
    // Left/right split (panel rotated to landscape). Two full-height bands side
    // by side, each a 2x2 gauge grid reusing gaugeSlots[0..3]. Same per-band
    // anatomy as portrait: bar on top, name+status beneath, gauges, foot line.
    const int16_t W = (int16_t)tft.width();
    const int16_t H = (int16_t)tft.height();
    const int16_t halfW = W / 2;

    // Left band stops 1px short of the divider; right band starts 1px past it,
    // so neither band's header/foot clear strip overwrites the divider column.
    ga.x = 0;         ga.w = halfW;
    gb.x = halfW + 1; gb.w = W - halfW - 1;
    ga.top = gb.top = 0;
    ga.height = gb.height = H;
    ga.r = gb.r = LY_SPLIT_L_GAUGE_R;
    ga.t = gb.t = LY_SPLIT_L_GAUGE_T;
    ga.slots = gb.slots = LY_SPLIT_L_SLOTS;
    ga.ncols = gb.ncols = LY_SPLIT_L_NCOLS;
    ga.barH = gb.barH = LY_SPLIT_L_BAR_H;
    ga.margin = gb.margin = LY_SPLIT_L_BAR_MARGIN;
    ga.barY = gb.barY = LY_SPLIT_L_BAR_Y;
    ga.hdrCY = gb.hdrCY = LY_SPLIT_L_HDR_CY;
    ga.rows[0] = gb.rows[0] = LY_SPLIT_L_ROW1;
    ga.rows[1] = gb.rows[1] = LY_SPLIT_L_ROW2;
    // Columns are band-relative; offset the right band by its left edge.
    ga.cols[0] = LY_SPLIT_L_COL1;         ga.cols[1] = LY_SPLIT_L_COL2;
    gb.cols[0] = gb.x + LY_SPLIT_L_COL1;  gb.cols[1] = gb.x + LY_SPLIT_L_COL2;
    ga.cols[2] = gb.cols[2] = 0;          // unused (ncols == 2)
    ga.footCY = gb.footCY = H - 12;
    // Gauges sit just below the name; the freed space below them holds a larger
    // dedicated ETA line above the foot.
    ga.etaCY = gb.etaCY = (H - 12) - 28;
#if defined(DISPLAY_320x480)
    ga.etaFont = gb.etaFont = FONT_LARGE;
#else
    ga.etaFont = gb.etaFont = FONT_BODY;
#endif

    // Vertical divider between the two bands. The screen is already cleared on a
    // force frame, so draw it only then.
    if (force) {
      tft.drawFastVLine(halfW, 0, H, tft.color565(40, 40, 40));
    }

    drawBand(printers[a].state, printers[a].config, a, ga, 0, force);
    drawBand(printers[b].state, printers[b].config, b, gb, 1, force);
    return;
  }
#endif

  // Portrait: two stacked full-width bands.
  ga.x = gb.x = 0;
  ga.w = gb.w = LY_W;
  ga.ncols = gb.ncols = 3;
  ga.barH = gb.barH = LY_SPLIT_BAR_H;
  ga.margin = gb.margin = LY_SPLIT_BAR_MARGIN;
  ga.cols[0] = gb.cols[0] = LY_COL1;
  ga.cols[1] = gb.cols[1] = LY_COL2;
  ga.cols[2] = gb.cols[2] = LY_COL3;
  ga.r = gb.r = LY_SPLIT_GAUGE_R;
  ga.t = gb.t = LY_SPLIT_GAUGE_T;
  ga.slots = gb.slots = LY_SPLIT_SLOTS;
  ga.top = 0;             ga.height = LY_SPLIT_DIV_Y;
  gb.top = LY_SPLIT_DIV_Y; gb.height = LY_H - LY_SPLIT_DIV_Y;
  ga.hdrCY = LY_SPLIT_A_HDR_CY; ga.barY = LY_SPLIT_A_BAR_Y; ga.rows[0] = LY_SPLIT_A_ROW1;
  gb.hdrCY = LY_SPLIT_B_HDR_CY; gb.barY = LY_SPLIT_B_BAR_Y; gb.rows[0] = LY_SPLIT_B_ROW1;
  ga.footCY = ga.top + ga.height - 12;
  gb.footCY = gb.top + gb.height - 12;
  // Portrait split: one bottom line per band (filament swatch + ETA only, no
  // type/layers/door) so the ETA stays large and legible. 240x320 has room for
  // a BODY-size ETA; the tiny 240x240 and the gauge-packed 320x480 stay SMALL.
  ga.etaCY = gb.etaCY = 0;
#if defined(DISPLAY_240x320)
  ga.etaFont = gb.etaFont = FONT_BODY;
#else
  ga.etaFont = gb.etaFont = FONT_SMALL;
#endif
#if LY_SPLIT_SLOTS >= 6
  ga.rows[1] = LY_SPLIT_A_ROW2;
  gb.rows[1] = LY_SPLIT_B_ROW2;
#else
  ga.rows[1] = LY_SPLIT_A_ROW1;
  gb.rows[1] = LY_SPLIT_B_ROW1;
#endif

  // No divider line: each band's top-anchored progress bar gives enough visual
  // separation between the two printers.

  drawBand(printers[a].state, printers[a].config, a, ga, 0, force);
  drawBand(printers[b].state, printers[b].config, b, gb, 1, force);
}

#else  // !LAYOUT_HAS_SPLIT

void drawSplit() {}

#endif  // LAYOUT_HAS_SPLIT
