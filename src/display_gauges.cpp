#include "display_gauges.h"
#include "config.h"
#include "layout.h"
#include "settings.h"
#include <time.h>
#include <math.h>

// LovyanGFX does not expose alphaBlend() as a member. Provide a compatible
// helper: alpha=0 → pure bg, alpha=255 → pure fg (same semantics as TFT_eSPI).
static inline uint16_t alphaBlend565(uint8_t alpha, uint16_t fg, uint16_t bg) {
  uint8_t r = ((fg >> 11) & 0x1F) * alpha / 255 + ((bg >> 11) & 0x1F) * (255 - alpha) / 255;
  uint8_t g = ((fg >>  5) & 0x3F) * alpha / 255 + ((bg >>  5) & 0x3F) * (255 - alpha) / 255;
  uint8_t b = ( fg        & 0x1F) * alpha / 255 + ( bg        & 0x1F) * (255 - alpha) / 255;
  return (r << 11) | (g << 5) | b;
}

// Return white or black text color that contrasts well against a given RGB565 background.
// Uses perceived luminance (ITU-R BT.601 weights) to decide.
static inline uint16_t contrastTextColor565(uint16_t bgColor) {
  uint8_t r5 = (bgColor >> 11) & 0x1F;
  uint8_t g6 = (bgColor >>  5) & 0x3F;
  uint8_t b5 =  bgColor        & 0x1F;
  // Scale to 0-255 for luminance calculation
  uint8_t r8 = r5 * 255 / 31;
  uint8_t g8 = g6 * 255 / 63;
  uint8_t b8 = b5 * 255 / 31;
  // ITU-R BT.601 perceived luminance
  uint16_t lum = (uint16_t)(r8 * 77 + g8 * 150 + b8 * 29) / 256;
  return (lum < 128) ? 0xFFFF : 0x0000;  // white on dark, black on light
}

// Holds the SPI bus for an entire gauge update. Without this, each primitive
// (fillArc, fillCircle, drawString) opens+closes its own transaction and the
// scheduler can interleave between them — producing visible intermediate
// states (flicker). startWrite is refcounted in LovyanGFX so nesting is safe.
class ScopedWrite {
  lgfx::LovyanGFX& _t;
public:
  explicit ScopedWrite(lgfx::LovyanGFX& t) : _t(t) { _t.startWrite(); }
  ~ScopedWrite() { _t.endWrite(); }
};

// ---------------------------------------------------------------------------
//  H2-style LED progress bar
// ---------------------------------------------------------------------------
void drawLedProgressBar(lgfx::LovyanGFX& tft, int16_t y, uint8_t progress) {
  ScopedWrite sw(tft);
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  // Use the active canvas width so the bar reaches both edges in landscape
  // (rotation 1/3 on 240x320 → tft.width()=320). 240x240 and portrait keep
  // their existing centered 236px bar via LY_BAR_W when canvas == SCREEN_W.
  const int16_t scrW = (int16_t)tft.width();
  const int16_t barW = (scrW > SCREEN_W) ? (scrW - 4) : LY_BAR_W;
  const int16_t barH = LY_BAR_H;
  const int16_t barX = (scrW - barW) / 2;

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

  const int16_t scrW = (int16_t)tft.width();
  const int16_t barW = (scrW > SCREEN_W) ? (scrW - 4) : LY_BAR_W;
  const int16_t barH = LY_BAR_H;
  const int16_t barX = (scrW - barW) / 2;
  // Reset shimmer if bar geometry changed (rotation flip) so the cached
  // erase position from the old bar doesn't smear pixels onto the new one.
  static int16_t prevBarW = -1;
  if (prevBarW != barW) {
    shimmerPos = -1;
    prevBarW = barW;
  }
  int16_t fillW = (progress * barW) / 100;
  if (fillW < SHIMMER_W + 4) return;  // too small for shimmer

  uint16_t barColor = dispSettings.progress.arc;

  ScopedWrite sw_(tft);

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
static void drawArcFillLegacy(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy,
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

  // drawArc() renders an anti-aliased annulus slice — equivalent to
  // TFT_eSPI drawSmoothArc. fillArc() is not anti-aliased on LovyanGFX.
  auto arcDraw = [&](uint16_t a0, uint16_t a1, uint16_t color) {
    float la0 = (float)((a0 + 90u) % 360u);
    float la1 = (float)((a1 + 90u) % 360u);
    if (la0 > la1) {
      // Arc crosses the 0° boundary — split into two segments
      tft.drawArc(cx, cy, radius, radius - thickness, la0, 360.0f, color);
      tft.drawArc(cx, cy, radius, radius - thickness, 0.0f,  la1,  color);
    } else {
      tft.drawArc(cx, cy, radius, radius - thickness, la0, la1, color);
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

// Integer square-root fraction — U8.8 fixed point. Port of TFT_eSPI helper
// used to derive AA alpha from squared distance without an FPU roundtrip.
static inline uint8_t sqrt_fraction(uint32_t num) {
  if (num > 0x40000000) return 0;
  uint32_t bsh = 0x00004000;
  uint32_t fpr = 0;
  uint32_t osh = 0;
  while (num > bsh) { bsh <<= 2; osh++; }
  do {
    uint32_t bod = bsh + fpr;
    if (num >= bod) { num -= bod; fpr = bsh + bod; }
    num <<= 1;
  } while (bsh >>= 1);
  return fpr >> osh;
}

// Subpixel AA wedge line with explicit background color. This avoids the
// integer endpoint truncation in LovyanGFX::drawWedgeLine when arc caps move.
static inline float wedgeLineDistanceAA(float xpax, float ypay,
                                        float bax, float bay, float dr = 0.0f) {
  const float denom = bax * bax + bay * bay;
  const float h = (denom > 0.0f)
    ? fmaxf(fminf((xpax * bax + ypay * bay) / denom, 1.0f), 0.0f)
    : 0.0f;
  const float dx = xpax - bax * h;
  const float dy = ypay - bay * h;
  return sqrtf(dx * dx + dy * dy) + h * dr;
}

static void drawWedgeLineAA(lgfx::LovyanGFX& tft,
                            float ax, float ay, float bx, float by,
                            float ar, float br,
                            uint16_t fg_color, uint16_t bg_color) {
  constexpr float pixelAlphaGain  = 255.0f;
  constexpr float loAlphaTheshold = 1.0f / 32.0f;
  constexpr float hiAlphaTheshold = 1.0f - loAlphaTheshold;

  if (ar < 0.0f || br < 0.0f) return;
  if (fabsf(ax - bx) < 0.01f && fabsf(ay - by) < 0.01f) bx += 0.01f;

  int32_t x0 = (int32_t)floorf(fminf(ax - ar, bx - br));
  int32_t x1 = (int32_t) ceilf(fmaxf(ax + ar, bx + br));
  int32_t y0 = (int32_t)floorf(fminf(ay - ar, by - br));
  int32_t y1 = (int32_t) ceilf(fmaxf(ay + ar, by + br));

  const int32_t maxX = (int32_t)tft.width() - 1;
  const int32_t maxY = (int32_t)tft.height() - 1;
  if (x1 < 0 || y1 < 0 || x0 > maxX || y0 > maxY) return;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > maxX) x1 = maxX;
  if (y1 > maxY) y1 = maxY;

  const float bax = bx - ax;
  const float bay = by - ay;
  const float rdt = ar - br;
  const float aaRadius = ar + 0.5f;

  for (int32_t yp = y0; yp <= y1; ++yp) {
    const float ypay = yp - ay;
    for (int32_t xp = x0; xp <= x1; ++xp) {
      const float xpax = xp - ax;
      const float alpha = aaRadius - wedgeLineDistanceAA(xpax, ypay, bax, bay, rdt);
      if (alpha <= loAlphaTheshold) continue;
      if (alpha > hiAlphaTheshold) {
        tft.drawPixel(xp, yp, fg_color);
        continue;
      }
      const uint8_t blendAlpha = (uint8_t)(alpha * pixelAlphaGain);
      if (blendAlpha == 0) continue;
      tft.drawPixel(xp, yp, alphaBlend565(blendAlpha, fg_color, bg_color));
    }
  }
}

// Scan-quadrant AA annulus slice. Port of TFT_eSPI::drawArc (smooth=true).
// Angles: 0°=6 o'clock, clockwise, range 0-360. r=outer, ir=inner (inclusive).
// Ends are NOT anti-aliased — caller adds radial AA wedges for smooth ends.
static void drawArcAA(lgfx::LovyanGFX& tft, int32_t x, int32_t y,
                      int32_t r, int32_t ir,
                      uint32_t startAngle, uint32_t endAngle,
                      uint16_t fg_color, uint16_t bg_color) {
  constexpr float deg2rad = 3.14159265358979f / 180.0f;
  if (endAngle > 360) endAngle = 360;
  if (startAngle > 360) startAngle = 360;
  if (startAngle == endAngle) return;
  if (r < ir) { int32_t t = r; r = ir; ir = t; }
  if (r <= 0 || ir < 0) return;

  if (endAngle < startAngle) {
    if (startAngle < 360) drawArcAA(tft, x, y, r, ir, startAngle, 360, fg_color, bg_color);
    if (endAngle == 0) return;
    startAngle = 0;
  }

  int32_t xs = 0;
  uint8_t alpha = 0;
  uint32_t r2 = r * r;
  r++;
  uint32_t r1 = r * r;
  int32_t w = r - ir;
  uint32_t r3 = ir * ir;
  ir--;
  uint32_t r4 = ir * ir;

  uint32_t startSlope[4] = {0, 0, 0xFFFFFFFF, 0};
  uint32_t endSlope[4]   = {0, 0xFFFFFFFF, 0, 0};
  constexpr float minDivisor = 1.0f / 0x8000;

  float fabscos = fabsf(cosf(startAngle * deg2rad));
  float fabssin = fabsf(sinf(startAngle * deg2rad));
  uint32_t slope = (uint32_t)((fabscos / (fabssin + minDivisor)) * (float)(1UL << 16));
  if (startAngle <= 90) {
    startSlope[0] = slope;
  } else if (startAngle <= 180) {
    startSlope[1] = slope;
  } else if (startAngle <= 270) {
    startSlope[1] = 0xFFFFFFFF;
    startSlope[2] = slope;
  } else {
    startSlope[1] = 0xFFFFFFFF;
    startSlope[2] = 0;
    startSlope[3] = slope;
  }

  fabscos = fabsf(cosf(endAngle * deg2rad));
  fabssin = fabsf(sinf(endAngle * deg2rad));
  slope = (uint32_t)((fabscos / (fabssin + minDivisor)) * (float)(1UL << 16));
  if (endAngle <= 90) {
    endSlope[0] = slope;
    endSlope[1] = 0;
    startSlope[2] = 0;
  } else if (endAngle <= 180) {
    endSlope[1] = slope;
    startSlope[2] = 0;
  } else if (endAngle <= 270) {
    endSlope[2] = slope;
  } else {
    endSlope[3] = slope;
  }

  for (int32_t cy = r - 1; cy > 0; cy--) {
    uint32_t len[4] = {0, 0, 0, 0};
    int32_t xst[4]  = {-1, -1, -1, -1};
    uint32_t dy2 = (r - cy) * (r - cy);
    while ((r - xs) * (r - xs) + dy2 >= r1) xs++;

    for (int32_t cx = xs; cx < r; cx++) {
      uint32_t hyp = (r - cx) * (r - cx) + dy2;
      if (hyp > r2) {
        alpha = ~sqrt_fraction(hyp);
      } else if (hyp >= r3) {
        slope = ((r - cy) << 16) / (r - cx);
        if (slope <= startSlope[0] && slope >= endSlope[0]) { xst[0] = cx; len[0]++; }
        if (slope >= startSlope[1] && slope <= endSlope[1]) { xst[1] = cx; len[1]++; }
        if (slope <= startSlope[2] && slope >= endSlope[2]) { xst[2] = cx; len[2]++; }
        if (slope <= endSlope[3] && slope >= startSlope[3]) { xst[3] = cx; len[3]++; }
        continue;
      } else {
        if (hyp <= r4) break;
        alpha = sqrt_fraction(hyp);
      }
      if (alpha < 16) continue;
      uint16_t pcol = alphaBlend565(alpha, fg_color, bg_color);
      slope = ((r - cy) << 16) / (r - cx);
      if (slope <= startSlope[0] && slope >= endSlope[0]) tft.drawPixel(x + cx - r, y - cy + r, pcol);
      if (slope >= startSlope[1] && slope <= endSlope[1]) tft.drawPixel(x + cx - r, y + cy - r, pcol);
      if (slope <= startSlope[2] && slope >= endSlope[2]) tft.drawPixel(x - cx + r, y + cy - r, pcol);
      if (slope <= endSlope[3] && slope >= startSlope[3]) tft.drawPixel(x - cx + r, y - cy + r, pcol);
    }
    if (len[0]) tft.drawFastHLine(x + xst[0] - len[0] + 1 - r, y - cy + r, len[0], fg_color);
    if (len[1]) tft.drawFastHLine(x + xst[1] - len[1] + 1 - r, y + cy - r, len[1], fg_color);
    if (len[2]) tft.drawFastHLine(x - xst[2] + r, y + cy - r, len[2], fg_color);
    if (len[3]) tft.drawFastHLine(x - xst[3] + r, y - cy + r, len[3], fg_color);
  }

  if (startAngle ==   0 || endAngle == 360) tft.drawFastVLine(x, y + r - w, w, fg_color);
  if (startAngle <=  90 && endAngle >=  90) tft.drawFastHLine(x - r + 1, y, w, fg_color);
  if (startAngle <= 180 && endAngle >= 180) tft.drawFastVLine(x, y - r + 1, w, fg_color);
  if (startAngle <= 270 && endAngle >= 270) tft.drawFastHLine(x + r - w, y, w, fg_color);
}

static void drawArcCapAA(lgfx::LovyanGFX& tft, int32_t x, int32_t y,
                         int32_t r, int32_t ir, uint32_t angle,
                         uint16_t fg_color, uint16_t bg_color) {
  constexpr float deg2rad = 3.14159265358979f / 180.0f;
  const float sx = -sinf(angle * deg2rad);
  const float sy = +cosf(angle * deg2rad);
  drawWedgeLineAA(tft,
                  sx * ir + x, sy * ir + y,
                  sx *  r + x, sy *  r + y,
                  0.3f, 0.3f, fg_color, bg_color);
}

static void drawArcSegmentAA(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy,
                             int16_t radius, int16_t innerRadius,
                             uint16_t a0, uint16_t a1,
                             uint16_t fg_color, uint16_t bg_color,
                             bool drawStartCap, bool drawEndCap,
                             uint16_t startCapBg, uint16_t endCapBg) {
  if (a1 <= a0) return;
  if (drawStartCap) drawArcCapAA(tft, cx, cy, radius, innerRadius, a0, fg_color, startCapBg);
  if (drawEndCap)   drawArcCapAA(tft, cx, cy, radius, innerRadius, a1, fg_color, endCapBg);
  drawArcAA(tft, cx, cy, radius, innerRadius, a0, a1, fg_color, bg_color);
}

static void drawArcFill(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy,
                        int16_t radius, int16_t thickness,
                        uint16_t fillEnd, uint16_t fillColor, bool forceRedraw) {
  const uint16_t startAngle = 60;
  const uint16_t endAngle = 300;
  const uint16_t clampedFillEnd =
    (fillEnd < startAngle) ? startAngle : ((fillEnd > endAngle) ? endAngle : fillEnd);
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;
  const int16_t innerRadius = radius - thickness;

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
    drawArcSegmentAA(tft, cx, cy, radius, innerRadius,
                     startAngle, endAngle, track, bg,
                     true, true, bg, bg);
  }

  if (clampedFillEnd > startAngle) {
    drawArcSegmentAA(tft, cx, cy, radius, innerRadius,
                     startAngle, clampedFillEnd, fillColor, bg,
                     true, true, bg, (clampedFillEnd < endAngle) ? track : bg);
  }
  if (clampedFillEnd < endAngle) {
    drawArcSegmentAA(tft, cx, cy, radius, innerRadius,
                     clampedFillEnd, endAngle, track, bg,
                     false, true, bg, bg);
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
  ScopedWrite sw(tft);
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
  ScopedWrite sw(tft);
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
  ScopedWrite sw(tft);
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
  ScopedWrite sw(tft);
  const uint16_t startAngle = 60;
  const int16_t thickness = 6;
  uint16_t bg = dispSettings.bgColor;

  uint8_t pct = present ? humidityRaw : 0;
  if (pct > 100) pct = 100;

  uint16_t fillEnd = startAngle + (uint16_t)(pct * 240 / 100);
  if (fillEnd > 300) fillEnd = 300;

  // Color based on humidity level (0-5): green = dry, red = humid
  // Bambu AMS humidity field: 5 = bone dry (good), 1 = very wet (bad).
  // Invert for color mapping: wetLevel = (6 - humidity) so 5→1(good/green) and 1→5(bad/red).
  uint8_t wetLevel = (humidityLevel > 0) ? (6 - humidityLevel) : 0;
  uint16_t arcColor;
  if (!present || wetLevel == 0) {
    arcColor = CLR_TEXT_DIM;
  } else if (wetLevel <= 2) {
    arcColor = CLR_GREEN;
  } else if (wetLevel <= 3) {
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
  ScopedWrite sw(tft);
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
  ScopedWrite sw(tft);
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

// -----------------------------------------------------------------------------
//  Filament type shorthand mapping
// -----------------------------------------------------------------------------
const char* getFilamentTypeLabel(const char* fullType) {
  if (!fullType || !fullType[0]) return "---";
  // Match Bambu-specific type codes to shorthand
  if (strstr(fullType, "GFL99") || strstr(fullType, "PLA")) return "PLA";
  if (strstr(fullType, "GFG99") || strstr(fullType, "PETG")) return "PETG";
  if (strstr(fullType, "GFCR") || strstr(fullType, "CF") || strstr(fullType, "Carbon")) return "CF";
  if (strstr(fullType, "GFT99") || strstr(fullType, "TPU")) return "TPU";
  if (strstr(fullType, "GFAB") || strstr(fullType, "ABS")) return "ABS";
  if (strstr(fullType, "GFPA") || strstr(fullType, "PA")) return "PA";
  if (strstr(fullType, "GFPA-CF") || strstr(fullType, "PA-CF")) return "PA-CF";
  if (strstr(fullType, "GFSG") || strstr(fullType, "SG")) return "SG";
  if (strstr(fullType, "GFPEI") || strstr(fullType, "PEI")) return "PEI";
  if (strstr(fullType, "PVB")) return "PVB";
  if (strstr(fullType, "HPCS")) return "HPCS";
  if (strstr(fullType, "PVA")) return "PVA";
  if (strstr(fullType, "ECO")) return "ECO";
  // Generic fallback: extract first 4 chars
  static char buf[5];
  strlcpy(buf, fullType, sizeof(buf));
  return buf;
}

// -----------------------------------------------------------------------------
//  AMS Filament gauge - shows active tray color swatch + type label
// -----------------------------------------------------------------------------
void drawAmsFilamentGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                          int16_t thickness, const AmsState& ams,
                          bool forceRedraw) {
  ScopedWrite sw(tft);
  uint16_t bg = dispSettings.bgColor;

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
  }

  // Find active tray
  uint8_t activeTray = ams.activeTray;
  const AmsTray* tray = nullptr;
  if (ams.present && activeTray < AMS_MAX_TRAYS) {
    tray = &ams.trays[activeTray];
  }

  // Build label: type + remain%
  char mainText[12];
  char subText[12];
  uint16_t swatchColor = CLR_TEXT_DIM;

  if (tray && tray->present) {
    strlcpy(mainText, getFilamentTypeLabel(tray->type), sizeof(mainText));
    swatchColor = tray->colorRgb565;
    if (tray->remain >= 0) {
      snprintf(subText, sizeof(subText), "%d%%", tray->remain);
    } else {
      subText[0] = '\0';
    }
  } else if (ams.present && ams.unitCount > 0) {
    // AMS is connected but no active tray selected — show AMS status
    strlcpy(mainText, "AMS OK", sizeof(mainText));
    swatchColor = CLR_TEXT_DIM;
    subText[0] = '\0';
  } else {
    strlcpy(mainText, "No AMS", sizeof(mainText));
    subText[0] = '\0';
  }

  // Only redraw if text changed
  if (!gaugeTextChanged(cx, cy, mainText, subText, forceRedraw)) return;

  tft.fillCircle(cx, cy, radius - 1, bg);

  // Draw color swatch circle in center
  int16_t swatchR = radius * 5 / 12;  // ~42% of gauge radius
  if (swatchR < 4) swatchR = 4;
  tft.fillCircle(cx, cy, swatchR, swatchColor);

  // White border on swatch for visibility
  uint16_t swatchBorder = alphaBlend565(100, 0xFFFF, swatchColor);
  tft.drawCircle(cx, cy, swatchR + 1, swatchBorder);

  // Draw type label below swatch
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, bg);
  tft.drawString(mainText, cx, cy + radius * 4 / 10);

  // Draw remain % as subtitle if present
  if (subText[0]) {
    tft.setTextFont(1);
    tft.setTextColor(CLR_TEXT_DIM, bg);
    tft.drawString(subText, cx, cy + radius * 6 / 10);
  }

  // Draw "AMS" label at bottom
  bool sm = dispSettings.smallLabels;
  tft.setTextFont(sm ? 1 : 2);
  tft.setTextColor(CLR_TEXT_DIM, bg);
  tft.drawString("AMS", cx, cy + radius + (sm ? 3 : -1));
}

// -----------------------------------------------------------------------------
//  AMS Filament All gauge - circular gauge divided into 4 quadrants
//  Top-Left=Slot1, Top-Right=Slot2, Bottom-Right=Slot3, Bottom-Left=Slot4
//  Separator lines: solid left half of horizontal, dotted elsewhere.
//  Each quadrant: filament color bg + type label + % remaining.
//  Humidity "H{n}" shown in a tiny circle at the center of the gauge.
// -----------------------------------------------------------------------------
void drawAmsFilamentAllGauge(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                             int16_t thickness, const struct AmsState& ams,
                             bool forceRedraw) {
  ScopedWrite sw(tft);
  uint16_t bg = dispSettings.bgColor;
  uint16_t dim = CLR_TEXT_DIM;

  if (forceRedraw) {
    tft.fillCircle(cx, cy, radius + 2, bg);
  }

  const int16_t innerR = radius - 2;

  // Fill circle background
  tft.fillCircle(cx, cy, innerR, bg);

  // Humidity color scale: Bambu humidity field is INVERTED (5=dry/good, 1=wet/bad).
  // Invert for display: idx=0→dim, idx 1,2→green-ish (dry), idx 3→yellow, idx 4,5→red-ish (wet)
  static const uint16_t humidColors[6] = {
    0x18E3,  // 0 - dim (no data)
    0x07E0,  // 1 - green  (Bambu 5 = bone dry)
    0xAFE5,  // 2 - light green (Bambu 4 = dry)
    0xFFE0,  // 3 - yellow (Bambu 3 = moderate)
    0xFC80,  // 4 - orange (Bambu 2 = humid)
    0xF800   // 5 - red   (Bambu 1 = very wet)
  };

  uint8_t humidLevel = 0;
  if (ams.present && ams.unitCount > 0) {
    // Invert: Bambu 5→1(dry), Bambu 1→5(wet), Bambu 0→0(no data)
    uint8_t rawHumid = ams.units[0].humidity;
    humidLevel = (rawHumid > 0 && rawHumid <= 5) ? (6 - rawHumid) : 0;
    if (humidLevel > 5) humidLevel = 5;
  }
  uint16_t hColor = humidColors[humidLevel];

  // Quadrant slot mapping: TL=Slot1, TR=Slot2, BR=Slot3, BL=Slot4
  // trays[0]=Slot1, trays[1]=Slot2, trays[2]=Slot3, trays[3]=Slot4
  static const int8_t qSlotX[4] = {-1, 1, 1, -1};  // TL, TR, BR, BL
  static const int8_t qSlotY[4] = {-1, -1, 1, 1};
  const int16_t halfR = innerR;
  // Inset text toward center so it doesn't touch the circle edge
  const int16_t textInset = 5;
  const int16_t qCX = halfR / 2;  // quadrant center X offset
  const int16_t qCY = halfR / 2;  // quadrant center Y offset

  for (int qi = 0; qi < 4; qi++) {
    const AmsTray& tray = ams.trays[qi];
    int16_t qCenterX = cx + qSlotX[qi] * qCX;
    int16_t qCenterY = cy + qSlotY[qi] * qCY;

    // Quadrant fill rectangle
    int16_t fx, fy, fw, fh;
    if (qi == 0) {        // Top-Left
      fx = cx - halfR;  fy = cy - halfR;  fw = halfR;  fh = halfR;
    } else if (qi == 1) { // Top-Right
      fx = cx;           fy = cy - halfR;  fw = halfR;  fh = halfR;
    } else if (qi == 2) { // Bottom-Right
      fx = cx;           fy = cy;          fw = halfR;  fh = halfR;
    } else {              // Bottom-Left
      fx = cx - halfR;  fy = cy;          fw = halfR;  fh = halfR;
    }

    if (tray.present) {
      uint16_t swatchColor = tray.colorRgb565;

      // Fill quadrant with filament color
      tft.fillRect(fx + 1, fy + 1, fw - 2, fh - 2, swatchColor);

      // Contrast border around quadrant
      uint16_t borderCol = alphaBlend565(60, 0xFFFF, swatchColor);
      tft.drawRoundRect(fx + 1, fy + 1, fw - 2, fh - 2, 2, borderCol);

      // Pick text color that contrasts well against the filament color
      uint16_t txtColor = contrastTextColor565(swatchColor);

      // Inset text toward center of circle to avoid touching the edges
      int16_t tCX = qCenterX - qSlotX[qi] * textInset;
      int16_t tCY = qCenterY - qSlotY[qi] * textInset;

      // Type label (Font 2)
      const char* typeLabel = getFilamentTypeLabel(tray.type);
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(txtColor, swatchColor);
      tft.drawString(typeLabel, tCX, tCY - 8);

      // Remaining % (Font 2)
      char remainBuf[8];
      if (tray.remain >= 0) {
        snprintf(remainBuf, sizeof(remainBuf), "%d%%", tray.remain);
      } else {
        strlcpy(remainBuf, "--%", sizeof(remainBuf));
      }
      tft.setTextFont(2);
      tft.setTextColor(txtColor, swatchColor);
      tft.drawString(remainBuf, tCX, tCY + 10);
    } else {
      // Empty slot — inset toward center
      int16_t tCX = qCenterX - qSlotX[qi] * textInset;
      int16_t tCY = qCenterY - qSlotY[qi] * textInset;
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(dim, bg);
      tft.drawString("--", tCX, tCY);
    }
  }

  // Clip quadrant fill rectangles to the circle:
  // Erase the 4 corner areas outside the circle by drawing bg-colored
  // fast horizontal lines from the bounding square edges to the chord.
  for (int16_t y = cy - innerR; y <= cy + innerR; y++) {
    int16_t dy = y - cy;
    int32_t d2 = (int32_t)dy * dy;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 >= r2) continue;
    int16_t halfChord = (int16_t)sqrtf((float)(r2 - d2));
    // Erase left side: from bounding square left edge to circle left edge
    tft.drawFastHLine(cx - innerR, y, innerR - halfChord, bg);
    // Erase right side: from circle right edge to bounding square right edge
    tft.drawFastHLine(cx + halfChord, y, innerR - halfChord, bg);
  }
  // Erase rows above and below the circle entirely
  tft.fillRect(cx - innerR, cy - innerR - 1, innerR * 2 + 1, 2, bg);
  tft.fillRect(cx - innerR, cy + innerR, innerR * 2 + 1, 2, bg);

  // Redraw circle boundary ring
  uint16_t ringColor = alphaBlend565(80, 0xFFFF, bg);
  tft.drawCircle(cx, cy, innerR, ringColor);

  // Separator lines: all dotted except the TL↔BL (left vertical) segment.
  //
  //   Slot1 (TL)  ·  Slot2 (TR)
  //       ·  ┃  ·
  //   Slot4 (BL)  ·  Slot3 (BR)
  //
  // Left half of horizontal (between TL & BL) = SOLID
  // Everything else = dotted

  // Solid left-half of horizontal line (between TL/Slot1 and BL/Slot4)
  {
    int16_t startX = cx - innerR + 2;
    int16_t endX = cx;
    for (int16_t x = startX; x < endX; x++) {
      int16_t dx = x - cx;
      int32_t d2 = (int32_t)dx * dx;
      int32_t r2 = (int32_t)innerR * innerR;
      if (d2 < r2) {
        tft.drawPixel(x, cy, dim);
      }
    }
  }

  // Dotted right-half of horizontal line (between TR/Slot2 and BR/Slot3)
  for (int16_t dx = 1; dx < innerR - 2; dx += 3) {
    int32_t d2 = (int32_t)dx * dx;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 < r2) {
      tft.drawPixel(cx + dx, cy, dim);
    }
  }

  // Dotted vertical line — both top half (TL↔TR) and bottom half (BL↔BR)
  for (int16_t dy = -innerR + 3; dy < innerR - 2; dy += 3) {
    int32_t d2 = (int32_t)dy * dy;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 < r2) {
      tft.drawPixel(cx, cy + dy, dim);
    }
  }

  // Slot number labels at the corners outside the circle
  // Placed diagonally outside the circle boundary, near each quadrant
  static const int8_t sNumX[4] = {-1, 1, 1, -1};  // TL, TR, BR, BL
  static const int8_t sNumY[4] = {-1, -1, 1, 1};
  static const char* sNumLabel[4] = {"1", "2", "3", "4"};
  // 45-degree offset: place labels along diagonal outside the circle
  // sqrt(2)/2 ≈ 0.707; use 0.71 * radius for the diagonal distance from center
  int16_t diagOff = (int16_t)(radius * 0.71f) + 4;  // just outside the circle edge
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);  // Font 2 for slot numbers (larger, more readable)
  for (int i = 0; i < 4; i++) {
    int16_t nx = cx + sNumX[i] * diagOff;
    int16_t ny = cy + sNumY[i] * diagOff;
    tft.setTextColor(dim, bg);
    tft.drawString(sNumLabel[i], nx, ny);
  }

  // Humidity indicator: tiny circle at the center of the gauge
  // Bambu humidity: 5=dry(good), 1=wet(bad) — inverted display: 1=dry, 5=wet
  const int16_t humCircleR = 12;
  tft.fillCircle(cx, cy, humCircleR, bg);
  tft.drawCircle(cx, cy, humCircleR, dim);
  if (ams.present && ams.unitCount > 0 && ams.units[0].present) {
    char humBuf[4];
    // Invert displayed value: Bambu 5→1(dry), 1→5(wet) so higher=wetter (human intuition)
    uint8_t displayHumid = (ams.units[0].humidity > 0 && ams.units[0].humidity <= 5)
                           ? (6 - ams.units[0].humidity) : 0;
    snprintf(humBuf, sizeof(humBuf), "H%d", displayHumid);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(hColor, bg);
    tft.drawString(humBuf, cx, cy);
  }
}
