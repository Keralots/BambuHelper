#include "display_split.h"
#include "config.h"
#include "layout.h"

#if defined(LAYOUT_HAS_SPLIT)

#include "display_ui.h"      // tft, markFrameDirty, isDisplayForceRedraw, drawAmsBarsGauge
#include "display_gauges.h"  // arc/temp/fan/... primitives
#include "settings.h"        // dispSettings, GaugeType enum
#include "bambu_state.h"     // printers[], rotState, BambuState
#include "fonts.h"           // setFont, FontID
#include "tasmota.h"         // tasmotaGetWattsForSlot / tasmotaIsActiveForSlot
#include <string.h>

// Match display_ui.cpp / display_anim.cpp: themed background, not the literal
// black CLR_BG from config.h.
#ifdef CLR_BG
#undef CLR_BG
#endif
#define CLR_BG (dispSettings.bgColor)

namespace {

struct BandGeom {
  int16_t top, height;   // band rect (caller clears on a force frame)
  int16_t hdrCY;         // header text center Y
  int16_t barY;          // progress bar top Y
  int16_t cols[3];       // gauge column center X
  int16_t rows[2];       // gauge row center Y (rows[1] unused when slots <= 3)
  int16_t r, t;          // gauge radius, arc thickness
  uint8_t slots;         // 3 or 6
};

// Per-band caches (index 0 = top band, 1 = bottom band).
uint8_t    sPrevTypes[2][6] = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
                                { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };
uint32_t   sPrevAirduct[2]  = { 0xFFFFFFFFu, 0xFFFFFFFFu };
uint8_t    sPrevHdrState[2] = { 0xFF, 0xFF };
uint8_t    sPrevBarProg[2]  = { 0xFF, 0xFF };
BambuState sPrevState[2];

uint16_t stateColor(uint8_t gid) {
  switch (gid) {
    case GCODE_RUNNING: return CLR_GREEN;
    case GCODE_PAUSE:   return CLR_YELLOW;
    case GCODE_FAILED:  return CLR_RED;
    case GCODE_PREPARE: return CLR_BLUE;
    default:            return CLR_TEXT_DIM;
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
    if (!force) tft.fillRect(0, g.hdrCY - 9, LY_W, 18, CLR_BG);
    setFont(tft, FONT_BODY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    const char* name = (cfg.name[0] != '\0') ? cfg.name : "Printer";
    tft.drawString(name, LY_SPLIT_BAR_MARGIN, g.hdrCY);
    tft.fillCircle(LY_W - LY_SPLIT_BAR_MARGIN - 5, g.hdrCY, 5, stateColor(s.gcodeStateId));
    sPrevHdrState[bandIdx] = s.gcodeStateId;
  }

  // --- Thin progress bar under the header ---
  if (force || s.progress != sPrevBarProg[bandIdx]) {
    markFrameDirty();
    const int16_t bx = LY_SPLIT_BAR_MARGIN;
    const int16_t bw = LY_W - 2 * LY_SPLIT_BAR_MARGIN;
    const int16_t fw = (int16_t)((int32_t)bw * (s.progress > 100 ? 100 : s.progress) / 100);
    tft.fillRect(bx, g.barY, bw, LY_SPLIT_BAR_H, tft.color565(40, 40, 40));
    if (fw > 0) tft.fillRect(bx, g.barY, fw, LY_SPLIT_BAR_H, dispSettings.progress.arc);
    sPrevBarProg[bandIdx] = s.progress;
  }

  // --- Gauge row(s) ---
  for (uint8_t si = 0; si < g.slots; si++) {
    uint8_t gt = cfg.gaugeSlots[si];
    if (gt >= GAUGE_TYPE_COUNT) gt = GAUGE_EMPTY;
    const int16_t cx = g.cols[si % 3];
    const int16_t cy = g.rows[si / 3];

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
#if LY_SPLIT_SLOTS >= 6
  ga.rows[1] = LY_SPLIT_A_ROW2;
  gb.rows[1] = LY_SPLIT_B_ROW2;
#else
  ga.rows[1] = LY_SPLIT_A_ROW1;
  gb.rows[1] = LY_SPLIT_B_ROW1;
#endif

  // Divider line. The caller (updateDisplay screen-change / triggerDisplayTransition)
  // has already cleared the screen on a force frame, so draw it only then.
  if (force) {
    tft.drawFastHLine(0, LY_SPLIT_DIV_Y - 1, LY_W, tft.color565(40, 40, 40));
  }

  drawBand(printers[a].state, printers[a].config, a, ga, 0, force);
  drawBand(printers[b].state, printers[b].config, b, gb, 1, force);
}

#else  // !LAYOUT_HAS_SPLIT

void drawSplit() {}

#endif  // LAYOUT_HAS_SPLIT
