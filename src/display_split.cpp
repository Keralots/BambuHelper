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
char       sPrevEta[2][24] = { { 0 }, { 0 } };
BambuState sPrevState[2];

// Build the ETA / remaining string the same way the single-printer view does
// (display_ui.cpp): wall-clock finish time when the user prefers it and NTP is
// up, otherwise the remaining duration. Returns false when there is nothing to
// show (not printing / no estimate) so the caller can render a dim placeholder.
bool buildSplitEta(const BambuState& s, char* buf, size_t n) {
  if (s.remainingMinutes == 0) return false;
  time_t nowEpoch = time(nullptr);
  struct tm now;
  localtime_r(&nowEpoch, &now);
  const bool ntpSynced = now.tm_year > (2020 - 1900);

  if (!dispSettings.showTimeRemaining && ntpSynced) {
    time_t etaEpoch = nowEpoch + (time_t)s.remainingMinutes * 60;
    struct tm e;
    localtime_r(&etaEpoch, &e);
    int eh = e.tm_hour;
    const char* ampm = "";
    if (!netSettings.use24h) { ampm = eh < 12 ? "AM" : "PM"; eh %= 12; if (eh == 0) eh = 12; }
    if (e.tm_yday != now.tm_yday || e.tm_year != now.tm_year) {
      if (netSettings.use24h)
        snprintf(buf, n, "ETA: %02d.%02d. %02d:%02d", e.tm_mday, e.tm_mon + 1, eh, e.tm_min);
      else
        snprintf(buf, n, "ETA: %02d/%02d %d:%02d%s", e.tm_mon + 1, e.tm_mday, eh, e.tm_min, ampm);
    } else {
      if (netSettings.use24h)
        snprintf(buf, n, "ETA: %02d:%02d", eh, e.tm_min);
      else
        snprintf(buf, n, "ETA: %d:%02d %s", eh, e.tm_min, ampm);
    }
  } else {
    // showTimeRemaining set, or NTP not synced yet: show the duration. No
    // "Remaining:" prefix - the progress gauge already labels this context and
    // the narrow landscape bands are tight.
    snprintf(buf, n, "%dh %02dm", s.remainingMinutes / 60, s.remainingMinutes % 60);
  }
  return true;
}

// Draw the printer name left-aligned at (x, cy), trimming with a ".." suffix if
// it would run into the state dot. Assumes the caller already set the font.
void drawClippedName(const char* name, int16_t x, int16_t cy, int16_t maxW) {
  if (tft.textWidth(name) <= maxW) { tft.drawString(name, x, cy); return; }
  char buf[28];
  strlcpy(buf, name, sizeof(buf));
  int n = (int)strlen(buf);
  while (n > 1) {
    buf[--n] = '\0';
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

// Mirrors the per-type change detection in drawPrinting()'s slot loop so a tile
// is only redrawn when its underlying value actually changed.
bool tileValueChanged(uint8_t gt, const BambuState& s, const BambuState& p) {
  switch (gt) {
    case GAUGE_PROGRESS:      return s.progress != p.progress || s.remainingMinutes != p.remainingMinutes;
    case GAUGE_NOZZLE:        return s.nozzleTemp != p.nozzleTemp || s.nozzleTarget != p.nozzleTarget;
    case GAUGE_NOZZLE_RIGHT:  return s.nozzleTempN[0] != p.nozzleTempN[0] || s.nozzleTargetN[0] != p.nozzleTargetN[0];
    case GAUGE_NOZZLE_LEFT:   return s.nozzleTempN[1] != p.nozzleTempN[1] || s.nozzleTargetN[1] != p.nozzleTargetN[1];
    case GAUGE_BED:           return s.bedTemp != p.bedTemp || s.bedTarget != p.bedTarget;
    case GAUGE_PART_FAN:      return s.coolingFanPct != p.coolingFanPct;
    case GAUGE_AUX_FAN:       return s.auxFanPct != p.auxFanPct;
    case GAUGE_AUX_FAN_RIGHT: return s.auxFanRightPct != p.auxFanRightPct;
    case GAUGE_CHAMBER_FAN:   return s.chamberFanPct != p.chamberFanPct;
    case GAUGE_EXHAUST_FAN:   return s.exhaustFanPct != p.exhaustFanPct;
    case GAUGE_CHAMBER_TEMP:  return s.chamberTemp != p.chamberTemp;
    case GAUGE_HEATBREAK:     return s.heatbreakFanPct != p.heatbreakFanPct;
    case GAUGE_CLOCK:         return true;   // text cache gates the actual redraw
    case GAUGE_POWER:         return true;   // watts live outside BambuState
    case GAUGE_LAYER:         return s.layerNum != p.layerNum || s.totalLayers != p.totalLayers;
    default: break;
  }
  if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
    uint8_t ui = gt - GAUGE_AMS_HUM_1;
    const AmsUnit& cu = s.ams.units[ui]; const AmsUnit& pu = p.ams.units[ui];
    return cu.humidityRaw != pu.humidityRaw || cu.humidity != pu.humidity || cu.present != pu.present;
  }
  if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
    uint8_t ui = gt - GAUGE_AMS_TEMP_1;
    const AmsUnit& cu = s.ams.units[ui]; const AmsUnit& pu = p.ams.units[ui];
    return cu.temp != pu.temp || cu.present != pu.present;
  }
  if ((gt >= GAUGE_AMS_FILAMENT_1 && gt <= GAUGE_AMS_FILAMENT_4) ||
      (gt >= GAUGE_AMS_BARS_1 && gt <= GAUGE_AMS_BARS_4)) {
    const bool isBars = (gt >= GAUGE_AMS_BARS_1);
    uint8_t ui = isBars ? (gt - GAUGE_AMS_BARS_1) : (gt - GAUGE_AMS_FILAMENT_1);
    if (s.ams.present != p.ams.present || s.ams.unitCount != p.ams.unitCount) return true;
    const AmsUnit& cu = s.ams.units[ui]; const AmsUnit& pu = p.ams.units[ui];
    if (cu.present != pu.present || (!isBars && cu.humidity != pu.humidity) ||
        cu.trayCount != pu.trayCount) return true;
    if (isBars && s.ams.activeTray != p.ams.activeTray) return true;
    for (int tr = 0; tr < AMS_TRAYS_PER_UNIT; tr++) {
      int idx = ui * AMS_TRAYS_PER_UNIT + tr;
      const AmsTray& ct = s.ams.trays[idx]; const AmsTray& pt = p.ams.trays[idx];
      if (ct.present != pt.present || ct.colorRgb565 != pt.colorRgb565 ||
          ct.remain != pt.remain || (!isBars && strcmp(ct.type, pt.type) != 0)) return true;
    }
    return false;
  }
  return false;
}

// Reuses the shared gauge primitives. No smoothing pointers (the global smooth
// structs cannot serve two printers); arcs snap to value. Power uses slotIndex
// so each band reports its own plug.
void drawTile(uint8_t gt, const BambuState& s, uint8_t slotIndex,
              int16_t cx, int16_t cy, int16_t r, int16_t t, bool fr) {
  switch (gt) {
    case GAUGE_PROGRESS:
      drawProgressArc(tft, cx, cy, r, t, s.progress, s.progress, s.remainingMinutes, fr);
      break;
    case GAUGE_NOZZLE:
      drawTempGauge(tft, cx, cy, r, s.nozzleTemp, s.nozzleTarget, (float)dispSettings.nozzleScaleMax,
                    dispSettings.nozzle.arc,
                    s.dualNozzle ? (s.activeNozzle == 0 ? "Nozzle R" : "Nozzle L") : "Nozzle",
                    nullptr, fr, &dispSettings.nozzle);
      break;
    case GAUGE_NOZZLE_RIGHT:
      drawTempGauge(tft, cx, cy, r, s.nozzleTempN[0], s.nozzleTargetN[0], (float)dispSettings.nozzleScaleMax,
                    dispSettings.nozzle.arc, "Nozzle R", nullptr, fr, &dispSettings.nozzle);
      break;
    case GAUGE_NOZZLE_LEFT:
      drawTempGauge(tft, cx, cy, r, s.nozzleTempN[1], s.nozzleTargetN[1], (float)dispSettings.nozzleScaleMax,
                    dispSettings.nozzle.arc, "Nozzle L", nullptr, fr, &dispSettings.nozzle);
      break;
    case GAUGE_BED:
      drawTempGauge(tft, cx, cy, r, s.bedTemp, s.bedTarget, (float)dispSettings.bedScaleMax,
                    dispSettings.bed.arc, "Bed", nullptr, fr, &dispSettings.bed);
      break;
    case GAUGE_PART_FAN:
      drawFanGauge(tft, cx, cy, r, s.coolingFanPct, dispSettings.partFan.arc, "Part", fr, &dispSettings.partFan);
      break;
    case GAUGE_AUX_FAN:
      drawFanGauge(tft, cx, cy, r, s.auxFanPct, dispSettings.auxFan.arc,
                   (s.airductFuncs & (1u << 6)) ? "L.Aux" : "Aux", fr, &dispSettings.auxFan);
      break;
    case GAUGE_AUX_FAN_RIGHT:
      drawFanGauge(tft, cx, cy, r, s.auxFanRightPct, dispSettings.auxFanRight.arc, "R.Aux", fr, &dispSettings.auxFanRight);
      break;
    case GAUGE_CHAMBER_FAN:
      drawFanGauge(tft, cx, cy, r, s.chamberFanPct, dispSettings.chamberFan.arc,
                   (s.airductFuncs & (1u << 2)) ? "Exhaust" : "Chamber", fr, &dispSettings.chamberFan);
      break;
    case GAUGE_EXHAUST_FAN:
      drawFanGauge(tft, cx, cy, r, s.exhaustFanPct, dispSettings.exhaustFan.arc, "Exhaust", fr, &dispSettings.exhaustFan);
      break;
    case GAUGE_CHAMBER_TEMP:
      drawTempGauge(tft, cx, cy, r, s.chamberTemp, 0.0f, (float)dispSettings.chamberScaleMax,
                    dispSettings.chamberTemp.arc, "Chamber", nullptr, fr, &dispSettings.chamberTemp);
      break;
    case GAUGE_HEATBREAK:
      drawFanGauge(tft, cx, cy, r, s.heatbreakFanPct, dispSettings.heatbreak.arc, "HBreak", fr, &dispSettings.heatbreak);
      break;
    case GAUGE_CLOCK:
      drawClockWidget(tft, cx, cy, r, t, fr);
      break;
    case GAUGE_LAYER:
      drawLayerGauge(tft, cx, cy, r, t, s.layerNum, s.totalLayers, fr);
      break;
    case GAUGE_POWER:
      drawPowerGauge(tft, cx, cy, r, tasmotaGetWattsForSlot(slotIndex),
                     tasmotaIsActiveForSlot(slotIndex), "Power", fr);
      break;
    case GAUGE_EMPTY:
      if (fr) tft.fillCircle(cx, cy, r + 2, CLR_BG);
      break;
    default: {
      static const char* amsLabel[AMS_MAX_UNITS] = { "AMS 1", "AMS 2", "AMS 3", "AMS 4" };
      if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
        uint8_t ui = gt - GAUGE_AMS_HUM_1;
        const AmsUnit& u = s.ams.units[ui];
        drawHumidityGauge(tft, cx, cy, r, u.humidityRaw, u.humidity, u.present, amsLabel[ui], fr);
      } else if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
        uint8_t ui = gt - GAUGE_AMS_TEMP_1;
        const AmsUnit& u = s.ams.units[ui];
        drawTempGauge(tft, cx, cy, r, u.present ? u.temp : 0, 0, (float)dispSettings.chamberScaleMax,
                      dispSettings.chamberTemp.arc, amsLabel[ui], nullptr, fr, &dispSettings.chamberTemp);
      } else if (gt >= GAUGE_AMS_FILAMENT_1 && gt <= GAUGE_AMS_FILAMENT_4) {
        uint8_t ui = gt - GAUGE_AMS_FILAMENT_1;
        drawAmsFilamentAllGauge(tft, cx, cy, r, t, s.ams, ui, fr);
      } else if (gt >= GAUGE_AMS_BARS_1 && gt <= GAUGE_AMS_BARS_4) {
        uint8_t ui = gt - GAUGE_AMS_BARS_1;
        drawAmsBarsGauge(cx, cy, r, s.ams, ui, fr);
      } else if (fr) {
        tft.fillCircle(cx, cy, r + 2, CLR_BG);
      }
    } break;
  }
}

void drawBand(const BambuState& s, const PrinterConfig& cfg, uint8_t slotIndex,
              const BandGeom& g, uint8_t bandIdx, bool force) {
  BambuState& prev = sPrevState[bandIdx];

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

    bool needDraw = force || typeChanged || tileValueChanged(gt, s, prev);
    // Refresh Chamber/Exhaust + Aux/L.Aux labels when the airduct mask first
    // lands (it starts 0 and gets bits OR'd in on the first pushall).
    if (!needDraw && (gt == GAUGE_CHAMBER_FAN || gt == GAUGE_AUX_FAN) &&
        sPrevAirduct[bandIdx] != s.airductFuncs) {
      needDraw = true;
    }
    if (!needDraw) continue;

    markFrameDirty();
    drawTile(gt, s, slotIndex, cx, cy, g.r, g.t, force || typeChanged);
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
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      swColor = s.ams.vtColorRgb565;
      swType  = s.ams.vtType;
    }

    char etaStr[24];
    const bool hasEta = buildSplitEta(s, etaStr, sizeof(etaStr));
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
      char cbuf[20];
      const float w = tasmotaGetWattsForSlot(slotIndex);
      if (tasmotaIsActiveForSlot(slotIndex) && w > 0.5f) {
        snprintf(cbuf, sizeof(cbuf), "%.0fW", w);
      } else {
        snprintf(cbuf, sizeof(cbuf), "%d/%d", s.layerNum, s.totalLayers);
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

        // Centre string drawn first so the left filament label can clamp to it.
        const int16_t centerLeft = fcx - tft.textWidth(cbuf) / 2;
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        tft.drawString(cbuf, fcx, fy);

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
          tft.drawString("Door", g.x + g.w - g.margin - 18, fy);
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
