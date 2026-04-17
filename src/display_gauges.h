#ifndef DISPLAY_GAUGES_H
#define DISPLAY_GAUGES_H

#include <LovyanGFX.hpp>

struct GaugeColors;  // forward declaration from settings.h

// Draw H2-style LED progress bar (full-width, top of screen)
void drawLedProgressBar(lgfx::LovyanGFX& tft, int16_t y, uint8_t progress);

// Shimmer animation tick — call from loop(), runs at its own cadence
void tickProgressShimmer(lgfx::LovyanGFX& tft, int16_t y, uint8_t progress, bool printing);

// Draw progress arc with percentage and time in center
void drawProgressArc(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw);

// Draw temperature arc gauge with current/target
// arcValue: smooth value for arc position, current: actual value for text display
void drawTempGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors = nullptr,
                   float arcValue = -1.0f);

// Draw fan speed gauge (0-100%)
// arcPercent: smooth value for arc position (-1 = use percent)
void drawFanGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors = nullptr,
                  float arcPercent = -1.0f);

// Draw clock widget (HH:MM inside track ring)
void drawClockWidget(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, bool forceRedraw);

// Draw AMS humidity gauge (humidityRaw % with color from humidity level)
void drawHumidityGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                       uint8_t humidityRaw, uint8_t humidityLevel, bool present,
                       const char* label, bool forceRedraw);

// Draw layer progress gauge (current / total layers)
void drawLayerGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                    int16_t thickness, uint16_t layerNum, uint16_t totalLayers,
                    bool forceRedraw);

// Reset cached text (call on screen/printer transitions)
void resetGaugeTextCache();

#endif // DISPLAY_GAUGES_H
