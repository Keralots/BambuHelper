#ifndef DISPLAY_GAUGES_H
#define DISPLAY_GAUGES_H

#include <LovyanGFX.hpp>
#include "fonts.h"

struct GaugeColors;  // forward declaration from settings.h

// Draw H2-style LED progress bar (full-width, top of screen)
void drawLedProgressBar(lgfx::LovyanGFX& gfx, int16_t y, uint8_t progress);

// Shimmer animation tick — call from loop(), runs at its own cadence
void tickProgressShimmer(lgfx::LovyanGFX& gfx, int16_t y, uint8_t progress, bool printing);

// Truncate s to fit maxW px at the current font, appending ".." when cut.
const char* ellipsizeToWidth(lgfx::LovyanGFX& gfx, const char* s, int16_t maxW,
                             char* out, size_t outLen);

// Draw a centered gauge label below the arc (auto-shrinks over-wide labels).
void drawGaugeLabel(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    const char* label, uint16_t lblColor, uint16_t bg);

#if defined(DISPLAY_ROUND_240)
// Full-circle rim progress ring (round displays). Fill runs clockwise from
// 12 o'clock; incremental redraw unless forceRedraw / regression / color change.
// cacheSlot (0-2) selects an independent incremental-draw cache so the Rings
// skin can keep three concentric rings on screen at once.
void drawRimRing(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                 int16_t radius, int16_t thickness,
                 uint8_t pct, uint16_t fillColor, bool forceRedraw,
                 uint8_t cacheSlot = 0);

// Shimmer ticks (experimental): sweep a bright specular band around the filled
// progress arc. Call from updateDisplay() at its own cadence; gated on
// dispSettings.animatedBar. tickRimShimmer = full circle from 12 o'clock (Rim
// skin + Rings outer ring); tickSpeedoShimmer = 240-deg gauge arc (Speedo).
void tickRimShimmer(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                    int16_t radius, int16_t thickness,
                    uint8_t pct, uint16_t fillColor, bool printing);
void tickSpeedoShimmer(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                       int16_t radius, int16_t thickness,
                       uint8_t pct, uint16_t fillColor, bool printing);

// Draw str along a circular arc of radius r around (cx,cy), centered on
// 12 o'clock (bottom=false, glyph tops facing the rim) or 6 o'clock
// (bottom=true, glyph tops facing the center — coin-style, reads left to
// right). clearHalfDeg > 0 first wipes the annulus band r +/- fontHeight/2
// over the sector midpoint +/- clearHalfDeg; the caller must keep the string
// short enough to stay inside that sector.
void drawCurvedString(lgfx::LovyanGFX& gfx, const char* str,
                      int16_t cx, int16_t cy, int16_t r, bool bottom,
                      uint16_t color, FontID font, int16_t clearHalfDeg);

// Arbitrary-sector variant of drawCurvedString: centerAA is the sector center
// in drawArcAA space (0 = 6 o'clock, clockwise; 12 o'clock = 180). Glyphs use
// top-style orientation (bottoms toward the center), so side text renders
// tilted — decorative watch-bezel style, keep the strings short.
void drawCurvedStringSector(lgfx::LovyanGFX& gfx, const char* str,
                            int16_t cx, int16_t cy, int16_t r,
                            uint16_t centerAA, uint16_t color, FontID font,
                            int16_t clearHalfDeg);
#endif

// Standard 240-degree gauge arc primitive (track 60..300, gap at 6 o'clock).
// fillEnd is the fill's end angle in that space; forceRedraw repaints the
// track (and wipes the enclosing circle) first. Used by every arc gauge and
// directly by the round Speedo skin.
void drawArcFill(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                 int16_t radius, int16_t thickness,
                 uint16_t fillEnd, uint16_t fillColor, bool forceRedraw);

// Draw progress arc with percentage and time in center
void drawProgressArc(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw);

// Draw temperature arc gauge with current/target
// arcValue: smooth value for arc position, current: actual value for text display
void drawTempGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors = nullptr,
                   float arcValue = -1.0f);

// Draw fan speed gauge (0-100%)
// arcPercent: smooth value for arc position (-1 = use percent)
void drawFanGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors = nullptr,
                  float arcPercent = -1.0f);

// Draw Tasmota power gauge. watts < 1000 renders "200W", >=1000 renders "1.2kW"
// (unit in a smaller suffix font). Arc fills 0..dispSettings.powerScaleW and saturates.
// active=false (offline/stale plug) renders a dim "--".
void drawPowerGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    float watts, bool active, const char* label, bool forceRedraw);

// Draw clock widget (HH:MM inside track ring)
void drawClockWidget(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, bool forceRedraw);

// Color AMS humidity from raw RH when available, with legacy level fallback.
uint16_t amsHumidityColor(uint8_t humidityRaw, uint8_t humidityLevel, bool present);

// Draw AMS humidity gauge (humidityRaw % with color from raw RH/legacy level)
void drawHumidityGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                       uint8_t humidityRaw, uint8_t humidityLevel, bool present,
                       const char* label, bool forceRedraw);

// Draw layer progress gauge (current / total layers)
void drawLayerGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    int16_t thickness, uint16_t layerNum, uint16_t totalLayers,
                    bool forceRedraw);

// Reset cached text (call on screen/printer transitions)
void resetGaugeTextCache();

// Get short filament type label (e.g., "PLA", "PETG", "TPU", "ABS")
const char* getFilamentTypeLabel(const char* fullType);

// Draw all 4 trays of the selected AMS unit (color + type + % + humidity).
// Absent unit or unparsed tray renders as a diagonal X cross-out.
void drawAmsFilamentAllGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                             int16_t thickness, const struct AmsState& ams,
                             uint8_t unitIndex, bool forceRedraw);

#endif // DISPLAY_GAUGES_H
