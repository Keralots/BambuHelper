#include "display_gauges.h"
#include "config.h"
#include "layout.h"
#include "settings.h"
#include <time.h>

// LovyanGFX does not expose alphaBlend() as a member. Provide a compatible
// helper: alpha=0 → pure bg, alpha=255 → pure fg (same semantics as TFT_eSPI).
static inline uint16_t alphaBlend565(uint8_t alpha, uint16_t fg, uint16_t bg) {
  uint8_t r = ((fg >> 11) & 0x1F) * alpha / 255 + ((bg >> 11) & 0x1F) * (255 - alpha) / 255;
  uint8_t g = ((fg >>  5) & 0x3F) * alpha / 255 + ((bg >>  5) & 0x3F) * (255 - alpha) / 255;
  uint8_t b = ( fg        & 0x1F) * alpha / 255 + ( bg        & 0x1F) * (255 - alpha) / 255;
  return (r << 11) | (g << 5) | b;
}

// ---------------------------------------------------------------------------
//  H2-style LED progress bar
// ---------------------------------------------------------------------------
void drawLedProgressBar(lgfx::LovyanGFX& tft, int16_t y, uint8_t progress) {
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  const int16_t barW = LY_BAR_W;
  const int16_t barH = LY_BAR_H;
  const int16_t barX = (SCREEN_W - barW) / 2;

  tft.fillRect(barX, y, barW, barH, bg);

  if (progress == 0) return;

  int16_t fillW = (progress * barW) / 100;
  if (fillW < 1) fillW = 1;

  uint16_t barColor = dispSettings.progress.arc;

  tft.fillRoundRect(barX, y, fillW, barH, 2, barColor);

  uint16_t glowColor = alphaBlend565(160, CLR_TEXT, barColor);
  tft.drawFastHLine(barX + 1, y + barH / 2, fillW - 2, glowColor);

  if (fillW > 4 && progress < 100) {
    tft.fillRect(barX + fillW - 3, y, 3, barH, glowColor);
  }

  if (fillW < barW) {
    tft.fillRoundRect(barX + fillW, y, barW - fillW, barH, 2, track);
  }
}

// ---------------------------------------------------------------------------
//  Shimmer animation for progress bar
// ---------------------------------------------------------------------------
static int16_t shimmerPos = -1;       // current x offset within filled area
static unsigned long shimmerLastMs = 0;
static bool shimmerPaused = false;
static unsigned long shimmerPauseStart = 0;

static const int16_t SHIMMER_W = 12;       // width of highlight
static const uint16_t SHIMMER_INTERVAL = 25;  // ms between steps (~40fps)
static const uint16_t SHIMMER_PAUSE = 1200;   // ms pause between sweeps
static const int16_t SHIMMER_STEP = 3;       // pixels per step

void tickProgressShimmer(lgfx::LovyanGFX& tft, int16_t y, uint8_t progress, bool printing) {
  if (!dispSettings.animatedBar || !printing || progress == 0) return;

  unsigned long now = millis();

  // Handle pause between sweeps
  if (shimmerPaused) {
    if (now - shimmerPauseStart < SHIMMER_PAUSE) return;
    shimmerPaused = false;
    shimmerPos = 0;
  }

  if (now - shimmerLastMs < SHIMMER_INTERVAL) return;
  shimmerLastMs = now;

  const int16_t barW = LY_BAR_W;
  const int16_t barH = LY_BAR_H;
  const int16_t barX = (SCREEN_W - barW) / 2;
  int16_t fillW = (progress * barW) / 100;
  if (fillW < SHIMMER_W + 4) return;  // too small for shimmer

  uint16_t barColor = dispSettings.progress.arc;

  // Erase previous shimmer position (redraw base bar segment)
  if (shimmerPos > 0) {
    int16_t eraseX = barX + shimmerPos - SHIMMER_STEP;
    int16_t eraseW = SHIMMER_STEP;
    if (eraseX < barX) { eraseW -= (barX - eraseX); eraseX = barX; }
    if (eraseW > 0) {
      tft.fillRect(eraseX, y, eraseW, barH, barColor);
    }
  }

  // Draw shimmer highlight
  int16_t sx = barX + shimmerPos;
  int16_t sw = SHIMMER_W;
  if (sx + sw > barX + fillW) sw = barX + fillW - sx;
  if (sw > 0) {
    // Gradient-like shimmer: brighter in center
    uint16_t bright = alphaBlend565(180, CLR_TEXT, barColor);
    uint16_t mid    = alphaBlend565(100, CLR_TEXT, barColor);
    // Edge pixels
    if (sw >= 3) {
      tft.fillRect(sx, y, 2, barH, mid);
      tft.fillRect(sx + 2, y, sw - 4 > 0 ? sw - 4 : 1, barH, bright);
      if (sw > 4) tft.fillRect(sx + sw - 2, y, 2, barH, mid);
    } else {
      tft.fillRect(sx, y, sw, barH, bright);
    }
  }

  shimmerPos += SHIMMER_STEP;

  // Reached end of filled area — restore last segment and pause
  if (shimmerPos >= fillW) {
    // Restore the tail
    int16_t tailX = barX + fillW - SHIMMER_W - SHIMMER_STEP;
    if (tailX < barX) tailX = barX;
    tft.fillRect(tailX, y, barX + fillW - tailX, barH, barColor);
    // Re-draw center glow line
    uint16_t glowColor = alphaBlend565(160, CLR_TEXT, barColor);
    tft.drawFastHLine(barX + 1, y + barH / 2, fillW - 2, glowColor);

    shimmerPos = 0;
    shimmerPaused = true;
    shimmerPauseStart = now;
  }
}

// ---------------------------------------------------------------------------
//  Helper: draw arc track + fill, handling decrease properly
// ---------------------------------------------------------------------------
static void drawArcFill(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy,
                        int16_t radius, int16_t thickness,
                        uint16_t fillEnd, uint16_t fillColor, bool forceRedraw) {
  // Internal angles use TFT_eSPI convention: 0°=bottom (6 o'clock), clockwise.
  // LovyanGFX fillArc uses 0°=right (3 o'clock), clockwise — offset by +90°
  // places the 120° gap at the bottom (6 o'clock), matching the desired layout.
  // When the converted start > end the arc crosses 0°, so split into two calls.
  const uint16_t startAngle = 60;
  const uint16_t endAngle = 300;
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  auto arcDraw = [&](uint16_t a0, uint16_t a1, uint16_t color) {
    float la0 = (float)((a0 + 90u) % 360u);
    float la1 = (float)((a1 + 90u) % 360u);
    if (la0 > la1) {
      // Arc crosses the 0° boundary — split into two segments
      tft.fillArc(cx, cy, radius, radius - thickness, la0, 360.0f, color);
      tft.fillArc(cx, cy, radius, radius - thickness, 0.0f,  la1,  color);
    } else {
      tft.fillArc(cx, cy, radius, radius - thickness, la0, la1, color);
    }
  };

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
    arcDraw(startAngle, endAngle, track);
  }

  // Draw filled portion
  if (fillEnd > startAngle) {
    arcDraw(startAngle, fillEnd, fillColor);
  }

  // Always redraw track for unfilled portion (handles value decrease)
  if (fillEnd < endAngle) {
    arcDraw(fillEnd, endAngle, track);
  }
}

// ---------------------------------------------------------------------------
//  Helper: clear gauge center and prepare for text
// ---------------------------------------------------------------------------
static void clearGaugeCenter(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy,
                             int16_t radius, int16_t thickness) {
  int16_t textR = radius - thickness - 1;
  tft.fillCircle(cx, cy, textR, dispSettings.bgColor);
}

// ---------------------------------------------------------------------------
//  Text cache — only clear+redraw gauge text when displayed string changes
// ---------------------------------------------------------------------------
#define GAUGE_CACHE_SLOTS 12

struct GaugeTextCache {
  int16_t cx, cy;
  char main[12];
  char sub[12];
};

static GaugeTextCache gCache[GAUGE_CACHE_SLOTS];
static uint8_t gCacheCount = 0;

// Find or create cache slot for gauge at (cx, cy).
// If the cache is full, evict the oldest entry (slot 0) to make room.
static GaugeTextCache* gaugeCache(int16_t cx, int16_t cy) {
  for (uint8_t i = 0; i < gCacheCount; i++) {
    if (gCache[i].cx == cx && gCache[i].cy == cy) return &gCache[i];
  }
  if (gCacheCount < GAUGE_CACHE_SLOTS) {
    GaugeTextCache* c = &gCache[gCacheCount++];
    c->cx = cx; c->cy = cy;
    c->main[0] = '\0'; c->sub[0] = '\0';
    return c;
  }
  // Cache full: evict oldest slot (index 0), shift remaining entries down
  memmove(&gCache[0], &gCache[1], (GAUGE_CACHE_SLOTS - 1) * sizeof(GaugeTextCache));
  GaugeTextCache* c = &gCache[GAUGE_CACHE_SLOTS - 1];
  c->cx = cx; c->cy = cy;
  c->main[0] = '\0'; c->sub[0] = '\0';
  return c;
}

// Check if text changed; update cache. Returns true if redraw needed.
static bool gaugeTextChanged(int16_t cx, int16_t cy, const char* main,
                             const char* sub, bool force) {
  if (force) {
    GaugeTextCache* c = gaugeCache(cx, cy);
    if (c) { strlcpy(c->main, main, sizeof(c->main)); strlcpy(c->sub, sub, sizeof(c->sub)); }
    return true;
  }
  GaugeTextCache* c = gaugeCache(cx, cy);
  if (!c) return true;
  bool changed = (strcmp(c->main, main) != 0) || (strcmp(c->sub, sub) != 0);
  if (changed) {
    strlcpy(c->main, main, sizeof(c->main));
    strlcpy(c->sub, sub, sizeof(c->sub));
  }
  return changed;
}

void resetGaugeTextCache() {
  gCacheCount = 0;
}

// ---------------------------------------------------------------------------
//  Main progress arc
// ---------------------------------------------------------------------------
void drawProgressArc(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw) {
  const uint16_t startAngle = 60;
  const GaugeColors& gc = dispSettings.progress;
  uint16_t bg = dispSettings.bgColor;

  uint16_t fillEnd = startAngle + (progress * 240) / 100;
  if (fillEnd > 300) fillEnd = 300;

  drawArcFill(tft, cx, cy, radius, thickness, fillEnd, gc.arc, forceRedraw);

  bool compact = (radius < 50);

  // Build display strings
  char pctBuf[8];
  if (compact) {
    snprintf(pctBuf, sizeof(pctBuf), "%d", progress);
  } else {
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", progress);
  }
  char timeBuf[16];
  if (remainingMin >= 60) {
    snprintf(timeBuf, sizeof(timeBuf), "%dh%dm", remainingMin / 60, remainingMin % 60);
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%dm", remainingMin);
  }

  // Only clear center + redraw text when displayed string actually changes
  if (gaugeTextChanged(cx, cy, pctBuf, timeBuf, forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(gc.value);
    tft.setTextFont(4);
    tft.drawString(pctBuf, cx, cy - (compact ? 4 : 8));

    tft.setTextFont(compact ? 1 : 2);
    tft.setTextColor(CLR_TEXT_DIM);
    tft.drawString(timeBuf, cx, cy + (compact ? 10 : 18));

    if (compact) {
      bool sm = dispSettings.smallLabels;
      tft.setTextFont(sm ? 1 : 2);
      tft.setTextColor(gc.label, bg);
      tft.drawString("Progress", cx, cy + radius + (sm ? 3 : -1));
    }
  }
}

// ---------------------------------------------------------------------------
//  Temperature arc gauge
// ---------------------------------------------------------------------------
void drawTempGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors, float arcValue) {
  const uint16_t startAngle = 60;
  const int16_t thickness = 6;
  uint16_t bg = dispSettings.bgColor;

  // Use custom colors if provided, otherwise fall back to accentColor
  uint16_t arcColor = colors ? colors->arc : accentColor;
  uint16_t lblColor = colors ? colors->label : accentColor;
  uint16_t valColor = colors ? colors->value : CLR_TEXT;

  // Arc uses smooth value if provided, text always uses actual current
  float arcVal = (arcValue >= 0.0f) ? arcValue : current;
  float ratio = (maxTemp > 0) ? (arcVal / maxTemp) : 0;
  if (ratio > 1.0f) ratio = 1.0f;
  if (ratio < 0.0f) ratio = 0.0f;

  uint16_t fillEnd = startAngle + (uint16_t)(ratio * 240);
  if (fillEnd <= startAngle && ratio > 0.01f) fillEnd = startAngle + 1;
  if (fillEnd > 300) fillEnd = 300;

  // Use custom arc color for all fill levels
  uint16_t tempColor = arcColor;

  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, tempColor, forceRedraw);

  // Build display strings
  char tempBuf[12], targetBuf[12];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", current);
  bool hasTarget = (target > 0.5f);
  if (hasTarget) snprintf(targetBuf, sizeof(targetBuf), "/%.0f", target);
  else targetBuf[0] = '\0';

  // Only clear center + redraw text when displayed string actually changes
  if (gaugeTextChanged(cx, cy, tempBuf, targetBuf, forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(valColor);
    tft.drawString(tempBuf, cx, hasTarget ? (cy - 4) : cy);

    if (hasTarget) {
      tft.setTextFont(1);
      tft.setTextColor(CLR_TEXT_DIM);
      tft.drawString(targetBuf, cx, cy + 10);
    }

    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(lblColor, bg);
    tft.drawString(label, cx, cy + radius + (sm ? 3 : -1));
  }
}

// ---------------------------------------------------------------------------
//  Fan speed gauge (0-100%)
// ---------------------------------------------------------------------------
void drawFanGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors,
                  float arcPercent) {
  const uint16_t startAngle = 60;
  const int16_t thickness = 6;
  uint16_t bg = dispSettings.bgColor;

  uint16_t arcColor = colors ? colors->arc : accentColor;
  uint16_t lblColor = colors ? colors->label : accentColor;
  uint16_t valColor = colors ? colors->value : CLR_TEXT;

  // Arc uses smooth value if provided, text always uses actual percent
  float arcVal = (arcPercent >= 0.0f) ? arcPercent : (float)percent;
  uint16_t fillEnd = startAngle + (uint16_t)(arcVal * 240.0f / 100.0f);
  if (fillEnd > 300) fillEnd = 300;

  uint16_t fanColor;
  if (percent == 0 && arcVal < 0.5f) {
    fanColor = CLR_TEXT_DIM;
  } else {
    fanColor = arcColor;
  }

  uint16_t drawFill = (arcVal > 0.5f) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, fanColor, forceRedraw);

  // Build display string
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", percent);

  // Only clear center + redraw text when displayed value actually changes
  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(valColor);
    tft.drawString(buf, cx, cy);

    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(lblColor, bg);
    tft.drawString(label, cx, cy + radius + (sm ? 3 : -1));
  }
}

// ---------------------------------------------------------------------------
//  AMS humidity gauge (percentage from humidityRaw, color from humidity level)
// ---------------------------------------------------------------------------
void drawHumidityGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                       uint8_t humidityRaw, uint8_t humidityLevel, bool present,
                       const char* label, bool forceRedraw) {
  const uint16_t startAngle = 60;
  const int16_t thickness = 6;
  uint16_t bg = dispSettings.bgColor;

  uint8_t pct = present ? humidityRaw : 0;
  if (pct > 100) pct = 100;

  uint16_t fillEnd = startAngle + (uint16_t)(pct * 240 / 100);
  if (fillEnd > 300) fillEnd = 300;

  // Color based on humidity level (0-5): green = dry, red = humid
  uint16_t arcColor;
  if (!present || humidityLevel == 0) {
    arcColor = CLR_TEXT_DIM;
  } else if (humidityLevel <= 2) {
    arcColor = CLR_GREEN;
  } else if (humidityLevel <= 3) {
    arcColor = CLR_YELLOW;
  } else {
    arcColor = CLR_RED;
  }

  uint16_t drawFill = (pct > 0) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, arcColor, forceRedraw);

  // Build display string
  char buf[8];
  if (present) {
    snprintf(buf, sizeof(buf), "%d%%", humidityRaw);
  } else {
    strlcpy(buf, "--", sizeof(buf));
  }

  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(present ? CLR_TEXT : CLR_TEXT_DIM);
    tft.drawString(buf, cx, cy);

    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(arcColor, bg);
    tft.drawString(label, cx, cy + radius + (sm ? 3 : -1));
  }
}

// ---------------------------------------------------------------------------
//  Layer progress gauge (current / total)
// ---------------------------------------------------------------------------
void drawLayerGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                    int16_t thickness, uint16_t layerNum, uint16_t totalLayers,
                    bool forceRedraw) {
  const uint16_t startAngle = 60;
  uint16_t bg = dispSettings.bgColor;
  uint16_t arcColor = dispSettings.progress.arc;

  float ratio = (totalLayers > 0) ? ((float)layerNum / totalLayers) : 0;
  if (ratio > 1.0f) ratio = 1.0f;

  uint16_t fillEnd = startAngle + (uint16_t)(ratio * 240);
  if (fillEnd > 300) fillEnd = 300;

  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(tft, cx, cy, radius, thickness, drawFill, arcColor, forceRedraw);

  // Build display strings - use smaller font for large numbers
  char layerBuf[12], totalBuf[12];
  snprintf(layerBuf, sizeof(layerBuf), "%d", layerNum);
  if (totalLayers > 0) {
    snprintf(totalBuf, sizeof(totalBuf), "/%d", totalLayers);
  } else {
    totalBuf[0] = '\0';
  }

  if (gaugeTextChanged(cx, cy, layerBuf, totalBuf, forceRedraw)) {
    clearGaugeCenter(tft, cx, cy, radius, thickness);

    tft.setTextDatum(MC_DATUM);

    // Pick font size based on digit count to fit inside gauge
    bool hasTot = (totalLayers > 0);
    int digits = strlen(layerBuf) + strlen(totalBuf);
    bool useSmall = (digits > 7);

    tft.setTextFont(useSmall ? 2 : 4);
    tft.setTextColor(CLR_TEXT);
    tft.drawString(layerBuf, cx, hasTot ? (cy - 4) : cy);

    if (hasTot) {
      tft.setTextFont(useSmall ? 1 : 2);
      tft.setTextColor(CLR_TEXT_DIM);
      tft.drawString(totalBuf, cx, cy + (useSmall ? 8 : 10));
    }

    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(arcColor, bg);
    tft.drawString("Layer", cx, cy + radius + (sm ? 3 : -1));
  }
}

// ---------------------------------------------------------------------------
//  Clock widget - shows current time HH:MM inside a track ring
// ---------------------------------------------------------------------------
void drawClockWidget(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, bool forceRedraw) {
  uint16_t bg = dispSettings.bgColor;

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
  }

  // Get current time
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);

  char timeBuf[8];
  // Show placeholder until NTP has synced
  if (now < 1704067200) {  // 2024-01-01 00:00:00 UTC
    strlcpy(timeBuf, "--:--", sizeof(timeBuf));
  } else {
    int h = tm.tm_hour;
    if (!netSettings.use24h) {
      h = h % 12;
      if (h == 0) h = 12;
    }
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", h, tm.tm_min);
  }

  if (gaugeTextChanged(cx, cy, timeBuf, "", forceRedraw)) {
    tft.fillCircle(cx, cy, radius - 1, bg);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(CLR_TEXT);
    tft.drawString(timeBuf, cx, cy);

    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(CLR_TEXT_DIM, bg);
    tft.drawString("Clock", cx, cy + radius + (sm ? 3 : -1));
  }
}
