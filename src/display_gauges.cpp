#include "display_gauges.h"
#include "config.h"
#include "layout.h"
#include "settings.h"
#include "fonts.h"
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

// Pick white or black text for maximum contrast against a 565-color background.
// Uses relative luminance (sRGB perception weights) to decide.
static inline uint16_t contrastTextColor565(uint16_t bg) {
  // Extract RGB components from 565
  uint8_t r = (bg >> 11) & 0x1F;  // 5 bits (0-31)
  uint8_t g = (bg >>  5) & 0x3F;  // 6 bits (0-63)
  uint8_t b =  bg        & 0x1F;  // 5 bits (0-31)
  // Scale to 0-255 for luminance calc
  float rf = (float)r / 31.0f;
  float gf = (float)g / 63.0f;
  float bf = (float)b / 31.0f;
  // sRGB relative luminance (perceptual brightness)
  float lum = 0.2126f * rf + 0.7152f * gf + 0.0722f * bf;
  return (lum > 0.5f) ? 0x0000   // TFT_BLACK
                      : 0xFFFF;  // TFT_WHITE
}

// ---------------------------------------------------------------------------
//  H2-style LED progress bar
// ---------------------------------------------------------------------------
void drawLedProgressBar(lgfx::LovyanGFX& gfx, int16_t y, uint8_t progress) {
  ScopedWrite sw(gfx);
  uint16_t bg = dispSettings.bgColor;
  uint16_t track = dispSettings.trackColor;

  // Use the active canvas width so the bar reaches both edges in landscape
  // (rotation 1/3 on 240x320 → tft.width()=320). 240x240 and portrait keep
  // their existing centered 236px bar via LY_BAR_W when canvas == SCREEN_W.
  const int16_t scrW = (int16_t)gfx.width();
  const int16_t barW = (scrW > SCREEN_W) ? (scrW - 4) : LY_BAR_W;
  const int16_t barH = LY_BAR_H;
  const int16_t barX = (scrW - barW) / 2;

  gfx.fillRect(barX, y, barW, barH, bg);

  if (progress == 0) return;

  int16_t fillW = (progress * barW) / 100;
  if (fillW < 1) fillW = 1;

  uint16_t barColor = dispSettings.progressBarColor;

  gfx.fillRoundRect(barX, y, fillW, barH, 2, barColor);

  uint16_t glowColor = alphaBlend565(160, CLR_TEXT, barColor);

  if (fillW > 4 && progress < 100) {
    gfx.fillRect(barX + fillW - 3, y, 3, barH, glowColor);
  }

  if (fillW < barW) {
    gfx.fillRoundRect(barX + fillW, y, barW - fillW, barH, 2, track);
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
static const uint16_t SHIMMER_INTERVAL = 20;  // ms between steps (~50fps)
static const uint16_t SHIMMER_PAUSE = 1200;   // ms pause between sweeps
static const int16_t SHIMMER_STEP = 3;       // pixels per step

void tickProgressShimmer(lgfx::LovyanGFX& gfx, int16_t y, uint8_t progress, bool printing) {
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

  const int16_t scrW = (int16_t)gfx.width();
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

  uint16_t barColor = dispSettings.progressBarColor;

  ScopedWrite sw_(gfx);

  // Erase previous shimmer position (redraw base bar segment)
  if (shimmerPos > 0) {
    int16_t eraseX = barX + shimmerPos - SHIMMER_STEP;
    int16_t eraseW = SHIMMER_STEP;
    if (eraseX < barX) { eraseW -= (barX - eraseX); eraseX = barX; }
    if (eraseW > 0) {
      gfx.fillRect(eraseX, y, eraseW, barH, barColor);
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
      gfx.fillRect(sx, y, 2, barH, mid);
      gfx.fillRect(sx + 2, y, sw - 4 > 0 ? sw - 4 : 1, barH, bright);
      if (sw > 4) gfx.fillRect(sx + sw - 2, y, 2, barH, mid);
    } else {
      gfx.fillRect(sx, y, sw, barH, bright);
    }
  }

  shimmerPos += SHIMMER_STEP;

  // Reached end of filled area — restore last segment and pause
  if (shimmerPos >= fillW) {
    // Restore the tail
    int16_t tailX = barX + fillW - SHIMMER_W - SHIMMER_STEP;
    if (tailX < barX) tailX = barX;
    gfx.fillRect(tailX, y, barX + fillW - tailX, barH, barColor);

    shimmerPos = 0;
    shimmerPaused = true;
    shimmerPauseStart = now;
  }
}

// ---------------------------------------------------------------------------
//  Helper: draw arc track + fill, handling decrease properly
// ---------------------------------------------------------------------------
static void drawArcFillLegacy(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
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
      gfx.drawArc(cx, cy, radius, radius - thickness, la0, 360.0f, color);
      gfx.drawArc(cx, cy, radius, radius - thickness, 0.0f,  la1,  color);
    } else {
      gfx.drawArc(cx, cy, radius, radius - thickness, la0, la1, color);
    }
  };

  if (forceRedraw) {
    gfx.fillCircle(cx, cy, radius + 2, bg);
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

static void drawWedgeLineAA(lgfx::LovyanGFX& gfx,
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

  const int32_t maxX = (int32_t)gfx.width() - 1;
  const int32_t maxY = (int32_t)gfx.height() - 1;
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
        gfx.drawPixel(xp, yp, fg_color);
        continue;
      }
      const uint8_t blendAlpha = (uint8_t)(alpha * pixelAlphaGain);
      if (blendAlpha == 0) continue;
      gfx.drawPixel(xp, yp, alphaBlend565(blendAlpha, fg_color, bg_color));
    }
  }
}

// Scan-quadrant AA annulus slice. Port of TFT_eSPI::drawArc (smooth=true).
// Angles: 0°=6 o'clock, clockwise, range 0-360. r=outer, ir=inner (inclusive).
// Ends are NOT anti-aliased — caller adds radial AA wedges for smooth ends.
static void drawArcAA(lgfx::LovyanGFX& gfx, int32_t x, int32_t y,
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
    if (startAngle < 360) drawArcAA(gfx, x, y, r, ir, startAngle, 360, fg_color, bg_color);
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
      if (slope <= startSlope[0] && slope >= endSlope[0]) gfx.drawPixel(x + cx - r, y - cy + r, pcol);
      if (slope >= startSlope[1] && slope <= endSlope[1]) gfx.drawPixel(x + cx - r, y + cy - r, pcol);
      if (slope <= startSlope[2] && slope >= endSlope[2]) gfx.drawPixel(x - cx + r, y + cy - r, pcol);
      if (slope <= endSlope[3] && slope >= startSlope[3]) gfx.drawPixel(x - cx + r, y - cy + r, pcol);
    }
    if (len[0]) gfx.drawFastHLine(x + xst[0] - len[0] + 1 - r, y - cy + r, len[0], fg_color);
    if (len[1]) gfx.drawFastHLine(x + xst[1] - len[1] + 1 - r, y + cy - r, len[1], fg_color);
    if (len[2]) gfx.drawFastHLine(x - xst[2] + r, y + cy - r, len[2], fg_color);
    if (len[3]) gfx.drawFastHLine(x - xst[3] + r, y - cy + r, len[3], fg_color);
  }

  if (startAngle ==   0 || endAngle == 360) gfx.drawFastVLine(x, y + r - w, w, fg_color);
  if (startAngle <=  90 && endAngle >=  90) gfx.drawFastHLine(x - r + 1, y, w, fg_color);
  if (startAngle <= 180 && endAngle >= 180) gfx.drawFastVLine(x, y - r + 1, w, fg_color);
  if (startAngle <= 270 && endAngle >= 270) gfx.drawFastHLine(x + r - w, y, w, fg_color);
}

static void drawArcCapAA(lgfx::LovyanGFX& gfx, int32_t x, int32_t y,
                         int32_t r, int32_t ir, uint32_t angle,
                         uint16_t fg_color, uint16_t bg_color) {
  constexpr float deg2rad = 3.14159265358979f / 180.0f;
  const float sx = -sinf(angle * deg2rad);
  const float sy = +cosf(angle * deg2rad);
  drawWedgeLineAA(gfx,
                  sx * ir + x, sy * ir + y,
                  sx *  r + x, sy *  r + y,
                  0.3f, 0.3f, fg_color, bg_color);
}

static void drawArcSegmentAA(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                             int16_t radius, int16_t innerRadius,
                             uint16_t a0, uint16_t a1,
                             uint16_t fg_color, uint16_t bg_color,
                             bool drawStartCap, bool drawEndCap,
                             uint16_t startCapBg, uint16_t endCapBg) {
  if (a1 <= a0) return;
  if (drawStartCap) drawArcCapAA(gfx, cx, cy, radius, innerRadius, a0, fg_color, startCapBg);
  if (drawEndCap)   drawArcCapAA(gfx, cx, cy, radius, innerRadius, a1, fg_color, endCapBg);
  drawArcAA(gfx, cx, cy, radius, innerRadius, a0, a1, fg_color, bg_color);
}

void drawArcFill(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
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
    gfx.fillCircle(cx, cy, radius + 2, bg);
    drawArcSegmentAA(gfx, cx, cy, radius, innerRadius,
                     startAngle, endAngle, track, bg,
                     true, true, bg, bg);
  }

  if (clampedFillEnd > startAngle) {
    drawArcSegmentAA(gfx, cx, cy, radius, innerRadius,
                     startAngle, clampedFillEnd, fillColor, bg,
                     true, true, bg, (clampedFillEnd < endAngle) ? track : bg);
  }
  if (clampedFillEnd < endAngle) {
    drawArcSegmentAA(gfx, cx, cy, radius, innerRadius,
                     clampedFillEnd, endAngle, track, bg,
                     false, true, bg, bg);
  }
}

#if defined(DISPLAY_ROUND_240)
// ---------------------------------------------------------------------------
//  Full-circle rim ring (round displays): progress fill runs clockwise from
//  12 o'clock. drawArcAA's angle space has 0 at 6 o'clock increasing
//  clockwise, so the fill starts at 180 and wraps through 360 -> 0.
//  Incremental: only the newly filled span is drawn, unless the value moved
//  backwards, the fill color changed, or forceRedraw is set (every screen
//  wipe passes forceRedraw, which is what keeps the single static cache
//  valid across the printing / finished / drying screens that share it).
// ---------------------------------------------------------------------------
void drawRimRing(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                 int16_t radius, int16_t thickness,
                 uint8_t pct, uint16_t fillColor, bool forceRedraw,
                 uint8_t cacheSlot) {
  static uint16_t prevDegs[3]   = { 0xFFFF, 0xFFFF, 0xFFFF };
  static uint16_t prevColors[3] = { 0, 0, 0 };
  if (cacheSlot > 2) cacheSlot = 0;
  uint16_t& prevDeg   = prevDegs[cacheSlot];
  uint16_t& prevColor = prevColors[cacheSlot];

  if (pct > 100) pct = 100;
  const uint16_t deg   = (uint16_t)pct * 360 / 100;
  const uint16_t bg    = dispSettings.bgColor;
  const uint16_t track = dispSettings.trackColor;
  const int16_t  ir    = radius - thickness;

  const bool full = forceRedraw || prevDeg == 0xFFFF || deg < prevDeg ||
                    (deg > 0 && fillColor != prevColor);
  if (!full && deg == prevDeg) return;

  // Draw the span [a..b] (degrees from 12 o'clock, clockwise) in `color`,
  // splitting at the 360 wrap of drawArcAA's angle space.
  auto spanDraw = [&](uint16_t a, uint16_t b, uint16_t color) {
    if (b <= a) return;
    uint16_t s0 = 180 + a, s1 = 180 + b;
    if (s0 >= 360) { s0 -= 360; s1 -= 360; }
    if (s1 <= 360) {
      drawArcAA(gfx, cx, cy, radius, ir, s0, s1, color, bg);
    } else {
      drawArcAA(gfx, cx, cy, radius, ir, s0, 360, color, bg);
      drawArcAA(gfx, cx, cy, radius, ir, 0, s1 - 360, color, bg);
    }
  };

  if (full) {
    if (deg < 360) spanDraw(deg, 360, track);
    if (deg > 0)   spanDraw(0, deg, fillColor);
  } else {
    spanDraw(prevDeg, deg, fillColor);
  }

  prevDeg   = deg;
  prevColor = fillColor;
}

// ---------------------------------------------------------------------------
//  Rim-ring shimmer (experimental): a white-tinted specular band sweeps
//  clockwise around the filled portion of the progress ring, then pauses and
//  repeats. Runs at its own ~40fps cadence from updateDisplay(), independent
//  of the event-driven ring redraw. Incremental: each frame restores the
//  previous band to the base fill color and paints the new one, so only about
//  one band-width of the thin ring is touched per frame (cheap SPI, no full-
//  ring recompose). Angles are degrees from 12 o'clock, clockwise, matching
//  drawRimRing; the 180-offset + 360-wrap maps into drawArcAA's angle space.
// ---------------------------------------------------------------------------
static const int16_t RIM_SHIM_BW     = 30;  // band angular width (deg)
static const int16_t RIM_SHIM_STEP   = 3;   // deg advanced per frame (lower = smoother motion)
static const int16_t RIM_SHIM_SLICES = 6;   // angular gradient steps across the band
static const uint8_t RIM_SHIM_PEAK   = 230; // max white blend at band center

// Draw an arc [s0..s1] in drawArcAA angle space (0 = 6 o'clock, CW), splitting
// at the 360 wrap so callers can pass s0/s1 up to ~720 for wrapped spans.
static void shimSpanAA(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                       int16_t r, int16_t ir, int16_t s0, int16_t s1,
                       uint16_t color, uint16_t bg) {
  if (s1 <= s0) return;
  if (s0 >= 360) { s0 -= 360; s1 -= 360; }
  if (s1 <= 360) {
    drawArcAA(gfx, cx, cy, r, ir, s0, s1, color, bg);
  } else {
    drawArcAA(gfx, cx, cy, r, ir, s0, 360, color, bg);
    drawArcAA(gfx, cx, cy, r, ir, 0, s1 - 360, color, bg);
  }
}

// Paint one shimmer band [a..b] (in drawArcAA space) over the base fill color.
// The band is split into RIM_SHIM_SLICES equal angular sub-spans whose white
// blend follows a parabolic profile (0 at the band edges, RIM_SHIM_PEAK at the
// center). Adjacent slices differ by only a small alpha, so the highlight reads
// as a smooth gradient instead of the old 3-segment mid/bright/mid blocks with
// their visible hard edges.
static void shimPaintBand(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                          int16_t r, int16_t ir, int16_t a, int16_t b,
                          uint16_t fillColor, uint16_t bg) {
  const int16_t span = b - a;
  if (span <= 0) return;
  if (span < RIM_SHIM_SLICES) {          // too narrow to slice: single band
    shimSpanAA(gfx, cx, cy, r, ir, a, b,
               alphaBlend565(RIM_SHIM_PEAK, CLR_TEXT, fillColor), bg);
    return;
  }
  const int32_t s2 = (int32_t)RIM_SHIM_SLICES * RIM_SHIM_SLICES;
  int16_t s0 = a;
  for (int16_t i = 0; i < RIM_SHIM_SLICES; i++) {
    int16_t s1 = a + (int32_t)span * (i + 1) / RIM_SHIM_SLICES;
    if (s1 > s0) {
      const int32_t d = 2 * i + 1 - RIM_SHIM_SLICES;   // signed dist*SLICES from center
      const uint8_t alpha = (uint8_t)((int32_t)RIM_SHIM_PEAK * (s2 - d * d) / s2);
      if (alpha >= 16)                                  // near-fill edges: leave base
        shimSpanAA(gfx, cx, cy, r, ir, s0, s1,
                   alphaBlend565(alpha, CLR_TEXT, fillColor), bg);
    }
    s0 = s1;
  }
}

// Core sweep engine shared by the full-circle (Rim/Rings) and the 240-deg arc
// (Speedo) skins. Sweeps a band from fillStart to fillEnd (drawArcAA space),
// restoring the trailing band to fillColor each frame, then pauses and repeats.
// `state` holds this instance's animation cursor so different skins/geometries
// don't share a cursor; the caller resets it when the geometry changes.
struct ShimState {
  int16_t       a       = 0;
  int16_t       prevA   = 0;
  bool          hasPrev = false;
  unsigned long lastMs  = 0;
  bool          paused  = false;
  unsigned long pauseMs = 0;
  int16_t       geom    = -1;   // geometry signature; change -> reset
};

static void shimSweep(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                      int16_t radius, int16_t thickness,
                      int16_t fillStart, int16_t fillEnd,
                      uint16_t fillColor, int16_t geomSig, ShimState& st) {
  if (fillEnd - fillStart < RIM_SHIM_BW + 4) return;  // filled arc too short

  if (st.geom != geomSig) {          // geometry changed (skin/radius) -> reset
    st.geom = geomSig;
    st.hasPrev = false;
    st.paused  = false;
    st.a       = fillStart;
  }

  unsigned long now = millis();
  if (st.paused) {
    if (now - st.pauseMs < SHIMMER_PAUSE) return;
    st.paused  = false;
    st.hasPrev = false;
    st.a       = fillStart;
  }
  if (now - st.lastMs < SHIMMER_INTERVAL) return;
  st.lastMs = now;
  if (st.a < fillStart) st.a = fillStart;

  const uint16_t bg = dispSettings.bgColor;
  const int16_t  ir = radius - thickness;

  ScopedWrite sw_(gfx);

  if (st.hasPrev) {
    int16_t pb = st.prevA + RIM_SHIM_BW;
    if (pb > fillEnd) pb = fillEnd;
    shimSpanAA(gfx, cx, cy, radius, ir, st.prevA, pb, fillColor, bg);
  }

  int16_t a = st.a;
  int16_t b = a + RIM_SHIM_BW;
  if (b > fillEnd) b = fillEnd;
  shimPaintBand(gfx, cx, cy, radius, ir, a, b, fillColor, bg);

  st.prevA   = a;
  st.hasPrev = true;

  st.a += RIM_SHIM_STEP;
  if (st.a >= fillEnd) {
    int16_t pb = st.prevA + RIM_SHIM_BW;
    if (pb > fillEnd) pb = fillEnd;
    shimSpanAA(gfx, cx, cy, radius, ir, st.prevA, pb, fillColor, bg);
    st.hasPrev = false;
    st.paused  = true;
    st.pauseMs = now;
    st.a       = fillStart;
  }
}

// Full-circle rim ring (Rim skin outer ring, Rings skin outer progress ring):
// fill runs clockwise from 12 o'clock, which is 180 in drawArcAA space.
void tickRimShimmer(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                    int16_t radius, int16_t thickness,
                    uint8_t pct, uint16_t fillColor, bool printing) {
  if (!dispSettings.animatedBar || !printing || pct == 0) return;
  if (pct > 100) pct = 100;
  const int16_t deg = (int16_t)pct * 360 / 100;
  static ShimState st;
  // geomSig keys on radius only (Rim 118 vs Rings outer 116) so switching skins
  // resets the cursor; a normal progress change must NOT reset the sweep.
  shimSweep(gfx, cx, cy, radius, thickness, 180, 180 + deg, fillColor,
            radius, st);
}

// 240-deg gauge arc (Speedo skin): fill runs from 60 to 60+pct*240/100 in
// drawArcAA space, gap at the bottom (6 o'clock).
void tickSpeedoShimmer(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                       int16_t radius, int16_t thickness,
                       uint8_t pct, uint16_t fillColor, bool printing) {
  if (!dispSettings.animatedBar || !printing || pct == 0) return;
  if (pct > 100) pct = 100;
  const int16_t fillEnd = 60 + (int16_t)pct * 240 / 100;
  static ShimState st;   // own state; geomSig = radius (constant for this skin)
  shimSweep(gfx, cx, cy, radius, thickness, 60, fillEnd, fillColor, radius, st);
}

// ---------------------------------------------------------------------------
//  Curved rim text (round displays)
// ---------------------------------------------------------------------------
// Each glyph is rendered into a small 16-bit sprite and pushed rotated (with
// AA resampling) tangent to the arc. The sprite is pre-filled with the screen
// background color, which doubles as the transparency key: only glyph pixels
// land on screen, so neighboring glyph cells can't erase each other's AA edges.
// Angle math below uses screen convention: 0 deg = 3 o'clock, clockwise
// positive (y grows downward). drawArcAA's own space (0 = 6 o'clock) applies
// only to the band-clear call.
// Internal worker. midAA = sector center in drawArcAA space (0 = 6 o'clock,
// clockwise). reverse = bottom-style: text runs counterclockwise with glyph
// tops toward the center so it still reads left to right; otherwise top-style
// (clockwise, glyph bottoms toward the center) — which is also what the
// arbitrary-sector variant uses for side text.
static void drawCurvedStringImpl(lgfx::LovyanGFX& gfx, const char* str,
                                 int16_t cx, int16_t cy, int16_t r,
                                 uint16_t midAA, bool reverse,
                                 uint16_t color, FontID font,
                                 int16_t clearHalfDeg) {
  ScopedWrite sw(gfx);
  const uint16_t bg = dispSettings.bgColor;
  setFont(gfx, font);
  const int16_t fh = gfx.fontHeight();

  if (clearHalfDeg > 0) {
    // drawArcAA space: 0 = 6 o'clock, clockwise. It handles end < start by
    // splitting at the 360 wrap, so the bottom sector needs no special case.
    // Radial pad is +1, not +2: with fg == bg the arc's edge-AA pixels come
    // out solid, so the clear effectively reaches ~1px past its nominal
    // radius already. Glyph ink tops out at r + fh/2 + 1 (sprite cell fh+2
    // plus AA resample spill), which +1 nominal (+~2 effective) still covers.
    // At +2 the band bit into the rim/speedo ring's inner AA edge and left a
    // hard aliased staircase across the top/bottom text sectors.
    uint32_t a0 = ((uint32_t)midAA + 360 - (uint32_t)clearHalfDeg) % 360;
    uint32_t a1 = ((uint32_t)midAA + (uint32_t)clearHalfDeg) % 360;
    drawArcAA(gfx, cx, cy, r + fh / 2 + 1, r - fh / 2 - 2, a0, a1, bg, bg);
  }
  if (!str || !str[0]) return;

  // Sector center in screen convention (0 = 3 o'clock, clockwise, y down).
  constexpr float d2r = 3.14159265f / 180.0f;
  const float midMath = (float)midAA + 90.0f;
  const int16_t fbx = cx + (int16_t)lroundf((r - fh / 2) * cosf(midMath * d2r));
  const int16_t fby = cy + (int16_t)lroundf((r - fh / 2) * sinf(midMath * d2r));

  // Split into UTF-8 code points and measure each advance at the target font.
  struct GlyphRef { const char* p; uint8_t len; int16_t w; };
  GlyphRef glyphs[48];
  int   n = 0;
  int   totalW = 0;
  int16_t maxW = 1;
  for (const char* p = str; *p && n < 48;) {
    uint8_t c = (uint8_t)*p;
    uint8_t len = (c < 0x80) ? 1 : (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    char tmp[5];
    uint8_t i = 0;
    for (; i < len && p[i]; i++) tmp[i] = p[i];
    tmp[i] = '\0';
    int16_t w = gfx.textWidth(tmp);
    glyphs[n].p = p;
    glyphs[n].len = i;
    glyphs[n].w = w;
    if (w > maxW) maxW = w;
    totalW += w;
    n++;
    p += i;
  }
  if (n == 0 || totalW <= 0) return;

  lgfx::LGFX_Sprite spr(&gfx);
  spr.setColorDepth(16);
  const int16_t sprW = maxW + 2, sprH = fh + 2;
  if (!spr.createSprite(sprW, sprH) || !loadFontInto(spr, font)) {
    // Heap-starved fallback: straight line at the arc's chord.
    spr.deleteSprite();
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(color, bg);
    gfx.drawString(str, fbx, fby);
    return;
  }
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(color, bg);
  spr.setPivot(sprW * 0.5f, sprH * 0.5f);

  // Rotation happens sprite-to-sprite: the AA affine push blends its edge
  // pixels by reading the destination, and this panel is write-only (7-pin
  // GC9A01, no MISO) — pushing rotated glyphs straight at the panel blends
  // against floating-bus garbage and shatters the text. A RAM destination
  // blends correctly; the composed square then goes out with a plain
  // transparent (non-reading) push.
  const int16_t side = (int16_t)ceilf(sqrtf((float)(sprW * sprW + sprH * sprH))) + 2;
  lgfx::LGFX_Sprite rotspr(&gfx);
  rotspr.setColorDepth(16);
  if (!rotspr.createSprite(side, side)) {
    spr.unloadFont();
    spr.deleteSprite();
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(color, bg);
    gfx.drawString(str, fbx, fby);
    return;
  }

  const float degPerPx = 180.0f / (3.14159265f * (float)r);
  const float totalDeg = totalW * degPerPx;
  // Forward text runs clockwise through the sector center; reversed text runs
  // counterclockwise so it still reads left to right below the center.
  float a = reverse ? (midMath + totalDeg * 0.5f) : (midMath - totalDeg * 0.5f);
  for (int i = 0; i < n; i++) {
    const float half = glyphs[i].w * degPerPx * 0.5f;
    const float ac = reverse ? (a - half) : (a + half);  // glyph center angle
    char tmp[5];
    memcpy(tmp, glyphs[i].p, glyphs[i].len);
    tmp[glyphs[i].len] = '\0';
    spr.fillSprite(bg);
    spr.drawString(tmp, sprW / 2, sprH / 2);
    const float px = cx + r * cosf(ac * d2r);
    const float py = cy + r * sinf(ac * d2r);
    // Tangent rotation: upright at the sector midpoint, tilting with the arc.
    const float rot = reverse ? (ac - 90.0f) : (ac - 270.0f);
    rotspr.fillSprite(bg);
    spr.pushRotateZoomWithAA(&rotspr, side * 0.5f, side * 0.5f, rot,
                             1.0f, 1.0f, bg);
    rotspr.pushSprite(&gfx, (int32_t)lroundf(px) - side / 2,
                      (int32_t)lroundf(py) - side / 2, bg);
    a = reverse ? (a - 2.0f * half) : (a + 2.0f * half);
  }
  rotspr.deleteSprite();
  spr.unloadFont();
  spr.deleteSprite();
}

void drawCurvedString(lgfx::LovyanGFX& gfx, const char* str,
                      int16_t cx, int16_t cy, int16_t r, bool bottom,
                      uint16_t color, FontID font, int16_t clearHalfDeg) {
  drawCurvedStringImpl(gfx, str, cx, cy, r, bottom ? 0 : 180, bottom,
                       color, font, clearHalfDeg);
}

// Arbitrary-sector variant: centerAA in drawArcAA space (0 = 6 o'clock,
// clockwise). Top-style glyph orientation, so side text reads clockwise like
// the top arc (tilted; decorative watch-bezel style).
void drawCurvedStringSector(lgfx::LovyanGFX& gfx, const char* str,
                            int16_t cx, int16_t cy, int16_t r,
                            uint16_t centerAA, uint16_t color, FontID font,
                            int16_t clearHalfDeg) {
  drawCurvedStringImpl(gfx, str, cx, cy, r, centerAA, false,
                       color, font, clearHalfDeg);
}
#endif // DISPLAY_ROUND_240

// ---------------------------------------------------------------------------
//  Helper: clear gauge center and prepare for text
// ---------------------------------------------------------------------------
static void clearGaugeCenter(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                             int16_t radius, int16_t thickness) {
  int16_t textR = radius - thickness - 1;
  gfx.fillCircle(cx, cy, textR, dispSettings.bgColor);
}

// ---------------------------------------------------------------------------
//  Helper: choose a center-value font that fits a gauge.
//
//  All panels draw the value text transparently over the cleared center disc,
//  so no opaque box can clip the arc and the font only has to be legible, not
//  boxed. Large gauges keep the layout base font; only the tiny R<30 portrait
//  9-slot grid steps down when a wide reading would overflow the inner circle.
//  Leaves the chosen font active on gfx.
// ---------------------------------------------------------------------------
static void fitValueFont(lgfx::LovyanGFX& gfx, const char* s,
                         int16_t radius, int16_t thickness, FontID base) {
  if (radius >= 30) { setFont(gfx, base); return; }
  FontID f = (base == FONT_SMALL) ? FONT_SMALL : FONT_BODY;
  setFont(gfx, f);
  if (f != FONT_SMALL) {
    const int16_t innerW = 2 * (radius - thickness - 1) - 2;
    if (gfx.textWidth(s) > innerW) setFont(gfx, FONT_SMALL);
  }
}

// Truncate s to fit maxW px at the CURRENT font, appending ".." when cut (the
// fonts have no real ellipsis glyph). UTF-8-aware: the copy and every cut land
// on a character boundary so a multi-byte glyph is never split. Writes into
// out; returns out.
const char* ellipsizeToWidth(lgfx::LovyanGFX& gfx, const char* s, int16_t maxW,
                             char* out, size_t outLen) {
  strlcpy(out, s, outLen);
  utf8TrimPartial(out);          // strlcpy may have sliced a char at outLen
  if (gfx.textWidth(out) <= maxW) return out;
  size_t n = strlen(out);
  while (n > 1) {
    n--;
    while (n > 1 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;  // back to a char boundary
    if (n + 2 >= outLen) continue;
    out[n] = '.'; out[n + 1] = '.'; out[n + 2] = '\0';
    if (gfx.textWidth(out) <= maxW) break;
  }
  return out;
}

// Draw a centered gauge label below the arc. Font follows the global smallLabels
// preference only (uniform size across a screen); an over-long label (> 8 chars)
// is trimmed to the slot width without "..". Exported so AMS tiles in
// display_ui.cpp render labels identically.
void drawGaugeLabel(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    const char* label, uint16_t lblColor, uint16_t bg) {
  // maxW matches the per-slot clear band (gR*2+4).
  const int16_t maxW = radius * 2 + 4;
  // Font is purely the global "Smaller gauge labels" toggle - never per-label, so
  // every gauge label on a screen is the same size. That toggle also raises the
  // web input cap (8 -> 12 chars), so longer custom labels are only entered when
  // the small font is on and they can actually fit.
  const bool sm = dispSettings.smallLabels;
  setFont(gfx, sm ? FONT_SMALL : FONT_BODY);

  // Safety trim (no "..") so a long label can't bleed into a neighbor. Only long
  // labels (> 8) are trimmed; short ones (incl. the dynamic "Nozzle R/L") draw in
  // full even if a hair wider than the slot, matching pre-#124 behavior.
  char buf[48];
  const char* draw = label;
  if (strlen(label) > 8 && gfx.textWidth(label) > maxW) {
    strlcpy(buf, label, sizeof(buf));
    utf8TrimPartial(buf);
    size_t n = strlen(buf);
    while (n > 0 && gfx.textWidth(buf) > maxW) {
      // drop one whole UTF-8 char (continuation bytes, then the lead)
      uint8_t removed;
      do { removed = (uint8_t)buf[n - 1]; buf[--n] = '\0'; }
      while (n > 0 && (removed & 0xC0) == 0x80);
    }
    draw = buf;
  }

  const int16_t ly = cy + radius + (sm ? 3 : -1);
  // Clear the actual drawn extent so a previous, wider label leaves no ghost
  // (e.g. "Nozzle R" -> "Nozzle L" on a side flip, or a full label > maxW).
  const int16_t fh = gfx.fontHeight();
#if defined(DISPLAY_ROUND_240)
  // Side-gauge labels sit close to the rim ring; a wider-than-slot clear band
  // would cut a rectangular notch into it. Cap at the slot width — ghosts are
  // impossible in practice (labels here only flip between short strings).
  const int16_t clearW = maxW;
#else
  const int16_t tw = gfx.textWidth(draw);
  const int16_t clearW = (tw + 4 > maxW) ? (int16_t)(tw + 4) : maxW;
#endif
  gfx.fillRect(cx - clearW / 2, ly - fh / 2 - 1, clearW, fh + 2, bg);

  gfx.setTextDatum(MC_DATUM);
  gfx.setTextColor(lblColor, bg);
  gfx.drawString(draw, cx, ly);
}

// Choose the largest humidity-number font that leaves room for a small "%"
// suffix inside the clearable center disc. Compact R=28 gauges start at BODY
// for vertical fit; larger layouts retain their normal value font when it fits.
static FontID fitHumidityValueFont(lgfx::LovyanGFX& gfx, const char* value,
                                   int16_t radius, int16_t thickness,
                                   FontID base, int16_t suffixGap) {
  setFont(gfx, FONT_SMALL);
  const int16_t suffixW = gfx.textWidth("%");
  const int16_t innerW = 2 * (radius - thickness - 1) - 2;

  FontID candidates[] = { base, FONT_LARGE, FONT_BODY, FONT_SMALL };
  for (FontID f : candidates) {
    if (radius < 30 && (f == FONT_LARGE || f == FONT_XLARGE)) continue;
    setFont(gfx, f);
    if (gfx.textWidth(value) + suffixGap + suffixW <= innerW) return f;
  }

  setFont(gfx, FONT_SMALL);
  return FONT_SMALL;
}

// Draw gauge value/secondary text transparently on every panel. The center
// disc is cleared before redraw, so glyphs blend against the bg there; where a
// large value spills past the inner circle the arc shows through instead of an
// opaque box. JC3248W535 renders into a readable 16bpp PSRAM frame sprite, so
// antialiased readback blends the same as the direct-draw panels — the opaque
// background (which was what clipped the arc on the small split gauges) is not
// needed.
static void setGaugeClearedTextColor(lgfx::LovyanGFX& gfx,
                                     uint16_t fg, uint16_t bg) {
#if defined(BOARD_IS_JC3248W535)
  // Transparent value text fixed the smaller dual-printer gauges in PORTRAIT,
  // but the rotated (landscape) JC sprite renders transparent antialiased text
  // black. Keep the explicit background in landscape, where the opaque box was
  // always fine; stay transparent in portrait.
  if (dispSettings.rotation == 1 || dispSettings.rotation == 3) {
    gfx.setTextColor(fg, bg);
    return;
  }
#endif
  (void)bg;
  gfx.setTextColor(fg);
}

// ---------------------------------------------------------------------------
//  Text cache — only clear+redraw gauge text when displayed string changes
// ---------------------------------------------------------------------------
// 12 covers every single-printer screen (max 9 gauges). The split screen draws
// up to 12 tiles (2 printers x 6 on 320x480); 16 leaves headroom so distinct
// (cx,cy) tiles never evict each other mid-frame.
#define GAUGE_CACHE_SLOTS 16

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
void drawProgressArc(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, uint8_t progress, uint8_t prevProgress,
                     uint16_t remainingMin, bool forceRedraw) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  const GaugeColors& gc = dispSettings.progress;
  uint16_t bg = dispSettings.bgColor;

  uint16_t fillEnd = startAngle + (progress * 240) / 100;
  if (fillEnd > 300) fillEnd = 300;

  drawArcFill(gfx, cx, cy, radius, thickness, fillEnd, gc.arc, forceRedraw);

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
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    gfx.setTextDatum(MC_DATUM);
    setGaugeClearedTextColor(gfx, gc.value, bg);
    fitValueFont(gfx, pctBuf, radius, thickness, LY_GAUGE_VALUE_FONT);
    gfx.drawString(pctBuf, cx, cy - (compact ? 4 : 8) + LY_GAUGE_VALUE_NUDGE_Y);

    setFont(gfx, compact ? FONT_SMALL : FONT_BODY);
    setGaugeClearedTextColor(gfx, CLR_TEXT_DIM, bg);
    gfx.drawString(timeBuf, cx, cy + (compact ? 10 : 18));

    if (compact) {
      drawGaugeLabel(gfx, cx, cy, radius, gaugeLabelOr(gaugeLabels.progress, "Progress"), gc.label, bg);
    }
  }
}

// ---------------------------------------------------------------------------
//  Temperature arc gauge
// ---------------------------------------------------------------------------
void drawTempGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                   float current, float target, float maxTemp,
                   uint16_t accentColor, const char* label,
                   const uint8_t* icon, bool forceRedraw,
                   const GaugeColors* colors, float arcValue) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  const int16_t thickness = LY_TEMP_GAUGE_T;
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

  // Optional warning color: follows the ACTUAL reading (the displayed number),
  // not the smoothed arc, so the value text color always matches the number.
  // Recolors the arc fill and the value text. 0 = feature off.
  bool warn = (dispSettings.warnThresholdPct > 0 && maxTemp > 0 &&
               (current / maxTemp) * 100.0f >= (float)dispSettings.warnThresholdPct);
  uint16_t tempColor = arcColor;
  if (warn) { tempColor = dispSettings.warnColor; valColor = dispSettings.warnColor; }

  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(gfx, cx, cy, radius, thickness, drawFill, tempColor, forceRedraw);

  // Build display strings
  char tempBuf[12], targetBuf[12];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", current);
  bool hasTarget = (target > 0.5f);
  if (hasTarget) snprintf(targetBuf, sizeof(targetBuf), "/%.0f", target);
  else targetBuf[0] = '\0';

  // Fold warn state into the change-detection key so a threshold crossing
  // repaints the value text even when the rounded number is unchanged
  // (e.g. 270.4 -> 270.6 both render "270" but cross a 90%-of-300 threshold).
  char keyBuf[13];
  snprintf(keyBuf, sizeof(keyBuf), "%s%s", tempBuf, warn ? "!" : "");

  // Only clear center + redraw text when the displayed string or warn state changes
  if (gaugeTextChanged(cx, cy, keyBuf, targetBuf, forceRedraw)) {
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    gfx.setTextDatum(MC_DATUM);
    fitValueFont(gfx, tempBuf, radius, thickness, LY_GAUGE_VALUE_FONT);
    // Direct panels draw transparently after the center clear. The helper keeps
    // an explicit background on the rotated JC sprite, where alpha blending
    // leaves hollow-looking glyphs (bright outline, dark interior).
    setGaugeClearedTextColor(gfx, valColor, bg);
    gfx.drawString(tempBuf, cx, hasTarget ? (cy - 4 + LY_GAUGE_VALUE_NUDGE_Y) : cy);

    if (hasTarget) {
      setFont(gfx, FONT_SMALL);
      setGaugeClearedTextColor(gfx, CLR_TEXT_DIM, bg);
      gfx.drawString(targetBuf, cx, cy + 10);
    }

    drawGaugeLabel(gfx, cx, cy, radius, label, lblColor, bg);
  }
}

// ---------------------------------------------------------------------------
//  Fan speed gauge (0-100%)
// ---------------------------------------------------------------------------
void drawFanGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                  uint8_t percent, uint16_t accentColor, const char* label,
                  bool forceRedraw, const GaugeColors* colors,
                  float arcPercent) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  const int16_t thickness = LY_TEMP_GAUGE_T;
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
  drawArcFill(gfx, cx, cy, radius, thickness, drawFill, fanColor, forceRedraw);

  // Build display string
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", percent);

  // Only clear center + redraw text when displayed value actually changes
  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    gfx.setTextDatum(MC_DATUM);
    fitValueFont(gfx, buf, radius, thickness, LY_GAUGE_VALUE_FONT);
    setGaugeClearedTextColor(gfx, valColor, bg);
    gfx.drawString(buf, cx, cy);

    drawGaugeLabel(gfx, cx, cy, radius, label, lblColor, bg);
  }
}

// ---------------------------------------------------------------------------
//  Tasmota power gauge (live watts; flips to kW past the full-scale point)
// ---------------------------------------------------------------------------
// Arc full-scale is user-configurable (dispSettings.powerScaleW, default 1000 W).
// The arc fills 0..powerScaleW and saturates above it. The "W"/"kW" readout split
// stays at 1000 W (a unit boundary), independent of the arc scale: normal printing
// (100-300 W) fills a small slice, a bed-heat spike saturates the ring.

void drawPowerGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    float watts, bool active, const char* label, bool forceRedraw) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  const int16_t thickness = LY_TEMP_GAUGE_T;
  uint16_t bg = dispSettings.bgColor;
  const float fullScale = (float)dispSettings.powerScaleW;

  float w = (active && watts > 0.0f) ? watts : 0.0f;
  float ratio = (fullScale > 0.0f) ? (w / fullScale) : 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  uint16_t fillEnd = startAngle + (uint16_t)(ratio * 240.0f);
  if (fillEnd > 300) fillEnd = 300;

  bool hasPower = active && w > 0.5f;
  uint16_t arcColor = hasPower ? dispSettings.power.arc : CLR_TEXT_DIM;
  uint16_t labelColor = hasPower ? dispSettings.power.label : CLR_TEXT_DIM;
  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(gfx, cx, cy, radius, thickness, drawFill, arcColor, forceRedraw);

  // Build the cached form (full string) and the value/suffix split for drawing.
  char buf[10], valueBuf[8];
  const char* suffix;
  if (!active) {
    strlcpy(buf, "--", sizeof(buf));
    valueBuf[0] = '\0';
    suffix = "";
  } else if (watts >= 1000.0f) {
    snprintf(valueBuf, sizeof(valueBuf), "%.1f", watts / 1000.0f);
    suffix = "kW";
    snprintf(buf, sizeof(buf), "%s%s", valueBuf, suffix);
  } else {
    snprintf(valueBuf, sizeof(valueBuf), "%.0f", w);
    suffix = "W";
    snprintf(buf, sizeof(buf), "%s%s", valueBuf, suffix);
  }

  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    if (active) {
      // Pick the largest value font that leaves room for the suffix inside the
      // clearable inner circle (mirrors fitHumidityValueFont but measures the
      // actual "W"/"kW" suffix, which is wider than "%").
      const int16_t suffixGap = 1;
      setFont(gfx, FONT_SMALL);
      const int16_t suffixW = gfx.textWidth(suffix);
      const int16_t innerW  = 2 * (radius - thickness - 1) - 2;
      FontID candidates[] = { LY_GAUGE_VALUE_FONT, FONT_LARGE, FONT_BODY, FONT_SMALL };
      FontID valueFont = FONT_SMALL;
      for (FontID f : candidates) {
        if (radius < 30 && (f == FONT_LARGE || f == FONT_XLARGE)) continue;
        setFont(gfx, f);
        if (gfx.textWidth(valueBuf) + suffixGap + suffixW <= innerW) { valueFont = f; break; }
      }

      setFont(gfx, valueFont);
      const int16_t valueW = gfx.textWidth(valueBuf);
      const int16_t splitX = cx - (valueW + suffixGap + suffixW) / 2 + valueW;

      gfx.setTextDatum(MR_DATUM);
      setGaugeClearedTextColor(gfx, dispSettings.power.value, bg);
      gfx.drawString(valueBuf, splitX, cy);

      setFont(gfx, FONT_SMALL);
      gfx.setTextDatum(ML_DATUM);
      setGaugeClearedTextColor(gfx, dispSettings.power.value, bg);
      gfx.drawString(suffix, splitX + suffixGap, cy);
    } else {
      gfx.setTextDatum(MC_DATUM);
      fitValueFont(gfx, buf, radius, thickness, LY_GAUGE_VALUE_FONT);
      setGaugeClearedTextColor(gfx, CLR_TEXT_DIM, bg);
      gfx.drawString(buf, cx, cy);
    }

    drawGaugeLabel(gfx, cx, cy, radius, label, labelColor, bg);
  }
}

uint16_t amsHumidityColor(uint8_t humidityRaw, uint8_t humidityLevel, bool present) {
  if (!present) return CLR_TEXT_DIM;

  // Raw RH is consistent across mixed AMS generations; humidity level is not.
  if (humidityRaw >= 1 && humidityRaw <= 100) {
    if (humidityRaw <= 35) return CLR_GREEN;
    if (humidityRaw <= 50) return CLR_YELLOW;
    if (humidityRaw <= 65) return CLR_ORANGE;
    return CLR_RED;
  }

  if (humidityLevel == 0) return CLR_TEXT_DIM;
  if (humidityLevel <= 2) return CLR_GREEN;
  if (humidityLevel == 3) return CLR_YELLOW;
  if (humidityLevel == 4) return CLR_ORANGE;
  return CLR_RED;
}

// ---------------------------------------------------------------------------
//  AMS humidity gauge (percentage from humidityRaw, color from raw RH/level)
// ---------------------------------------------------------------------------
void drawHumidityGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                       uint8_t humidityRaw, uint8_t humidityLevel, bool present,
                       const char* label, bool forceRedraw) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  const int16_t thickness = LY_TEMP_GAUGE_T;
  uint16_t bg = dispSettings.bgColor;

  uint8_t pct = present ? humidityRaw : 0;
  if (pct > 100) pct = 100;

  uint16_t fillEnd = startAngle + (uint16_t)(pct * 240 / 100);
  if (fillEnd > 300) fillEnd = 300;

  uint16_t arcColor = amsHumidityColor(humidityRaw, humidityLevel, present);

  uint16_t drawFill = (pct > 0) ? fillEnd : startAngle;
  drawArcFill(gfx, cx, cy, radius, thickness, drawFill, arcColor, forceRedraw);

  // Build display strings. Cache the fully rendered form, but draw the "%"
  // separately so it can use a smaller suffix font.
  char buf[8], valueBuf[8];
  if (present) {
    snprintf(buf, sizeof(buf), "%d%%", humidityRaw);
    snprintf(valueBuf, sizeof(valueBuf), "%d", humidityRaw);
  } else {
    strlcpy(buf, "--", sizeof(buf));
    valueBuf[0] = '\0';
  }

  if (gaugeTextChanged(cx, cy, buf, "", forceRedraw)) {
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    if (present) {
      const int16_t suffixGap = 1;
      FontID valueFont = fitHumidityValueFont(
          gfx, valueBuf, radius, thickness, LY_GAUGE_VALUE_FONT, suffixGap);
      const int16_t valueW = gfx.textWidth(valueBuf);
      setFont(gfx, FONT_SMALL);
      const int16_t suffixW = gfx.textWidth("%");
      const int16_t splitX = cx - (valueW + suffixGap + suffixW) / 2 + valueW;

      setFont(gfx, valueFont);
      gfx.setTextDatum(MR_DATUM);
      setGaugeClearedTextColor(gfx, CLR_TEXT, bg);
      gfx.drawString(valueBuf, splitX, cy);

      setFont(gfx, FONT_SMALL);
      gfx.setTextDatum(ML_DATUM);
      setGaugeClearedTextColor(gfx, CLR_TEXT, bg);
      gfx.drawString("%", splitX + suffixGap, cy);
    } else {
      gfx.setTextDatum(MC_DATUM);
      fitValueFont(gfx, buf, radius, thickness, LY_GAUGE_VALUE_FONT);
      setGaugeClearedTextColor(gfx, CLR_TEXT_DIM, bg);
      gfx.drawString(buf, cx, cy);
    }

    drawGaugeLabel(gfx, cx, cy, radius, label, arcColor, bg);
  }
}

// ---------------------------------------------------------------------------
//  Layer progress gauge (current / total)
// ---------------------------------------------------------------------------
void drawLayerGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                    int16_t thickness, uint16_t layerNum, uint16_t totalLayers,
                    bool forceRedraw) {
  ScopedWrite sw(gfx);
  const uint16_t startAngle = 60;
  uint16_t bg = dispSettings.bgColor;
  uint16_t arcColor = dispSettings.layer.arc;

  float ratio = (totalLayers > 0) ? ((float)layerNum / totalLayers) : 0;
  if (ratio > 1.0f) ratio = 1.0f;

  uint16_t fillEnd = startAngle + (uint16_t)(ratio * 240);
  if (fillEnd > 300) fillEnd = 300;

  uint16_t drawFill = (ratio > 0.01f) ? fillEnd : startAngle;
  drawArcFill(gfx, cx, cy, radius, thickness, drawFill, arcColor, forceRedraw);

  // Build display strings - use smaller font for large numbers
  char layerBuf[12], totalBuf[12];
  snprintf(layerBuf, sizeof(layerBuf), "%d", layerNum);
  if (totalLayers > 0) {
    snprintf(totalBuf, sizeof(totalBuf), "/%d", totalLayers);
  } else {
    totalBuf[0] = '\0';
  }

  if (gaugeTextChanged(cx, cy, layerBuf, totalBuf, forceRedraw)) {
    clearGaugeCenter(gfx, cx, cy, radius, thickness);

    gfx.setTextDatum(MC_DATUM);

    // Pick font size based on digit count to fit inside gauge
    bool hasTot = (totalLayers > 0);
    int digits = strlen(layerBuf) + strlen(totalBuf);
    bool useSmall = (digits > 7);

    fitValueFont(gfx, layerBuf, radius, thickness, useSmall ? FONT_BODY : LY_GAUGE_VALUE_FONT);
    setGaugeClearedTextColor(gfx, dispSettings.layer.value, bg);
    gfx.drawString(layerBuf, cx, hasTot ? (cy - 4 + LY_GAUGE_VALUE_NUDGE_Y) : cy);

    if (hasTot) {
      // Secondary "/total" line uses FONT_SMALL, matching the temp gauge's
      // target line. FONT_BODY here was a taller glyph box at the same tight
      // cy+10 offset, so its top climbed up into the main layer number — and
      // in compact mode (main capped at FONT_BODY) the two lines collided.
      fitValueFont(gfx, totalBuf, radius, thickness, FONT_SMALL);
      setGaugeClearedTextColor(gfx, CLR_TEXT_DIM, bg);
      gfx.drawString(totalBuf, cx, cy + (useSmall ? 8 : 10));
    }

    drawGaugeLabel(gfx, cx, cy, radius, gaugeLabelOr(gaugeLabels.layer, "Layer"), dispSettings.layer.label, bg);
  }
}

// ---------------------------------------------------------------------------
//  Clock widget - shows current time HH:MM inside a track ring
// ---------------------------------------------------------------------------
void drawClockWidget(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                     int16_t thickness, bool forceRedraw) {
  ScopedWrite sw(gfx);
  uint16_t bg = dispSettings.bgColor;

  if (forceRedraw) {
    gfx.fillCircle(cx, cy, radius + 2, bg);
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
    gfx.fillCircle(cx, cy, radius - 1, bg);

    gfx.setTextDatum(MC_DATUM);
    // Clock clears the full radius-1 disc above, so budget the wider inner
    // width (thickness=0) instead of the arc-inset radius-thickness-1.
    fitValueFont(gfx, timeBuf, radius, 0, LY_GAUGE_VALUE_FONT);
    setGaugeClearedTextColor(gfx, dispSettings.clockTimeColor, bg);
    gfx.drawString(timeBuf, cx, cy);

    drawGaugeLabel(gfx, cx, cy, radius, gaugeLabelOr(gaugeLabels.clock, "Clock"), dispSettings.clockDateColor, bg);
  }
}

// -----------------------------------------------------------------------------
//  Filament type shorthand mapping
// -----------------------------------------------------------------------------
const char* getFilamentTypeLabel(const char* fullType) {
  if (!fullType || !fullType[0]) return "---";
  // Match Bambu-specific type codes to shorthand.
  // Ordered most-specific first — strstr("PA-CF", "PA") is true, so
  // PA-CF must be checked before PA to avoid false matches.
  // Similarly, "PLA" contains "PA" as a substring, so PLA is checked before PA.
  if (strstr(fullType, "GFPA-CF") || strstr(fullType, "PA-CF")) return "PA-CF";
  if (strstr(fullType, "GFL99") || strstr(fullType, "PLA")) return "PLA";
  if (strstr(fullType, "GFG99") || strstr(fullType, "PETG")) return "PETG";
  if (strstr(fullType, "GFCR") || strstr(fullType, "CF") || strstr(fullType, "Carbon")) return "CF";
  if (strstr(fullType, "GFT99") || strstr(fullType, "TPU")) return "TPU";
  if (strstr(fullType, "GFAB") || strstr(fullType, "ABS")) return "ABS";
  if (strstr(fullType, "GFPA") || strstr(fullType, "PA")) return "PA";
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
//  Renders all 4 trays of the selected AMS unit (unitIndex).
//
//  480×480 (SenseCAP): Full design with 4 colored quadrants, type labels,
//  remaining percentages, separator lines, and center humidity indicator.
//  Slot numbers are drawn inside each quadrant to stay within bounds.
//
//  240×240: Simplified — 4 colored quadrants + center humidity only.
//  The 32px radius is too small for readable text inside quadrants.
//
//  Absent unit or unparsed tray renders as a diagonal X cross-out
//  (matches the 240x320 layout style for empty slots).
// -----------------------------------------------------------------------------
void drawAmsFilamentAllGauge(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                             int16_t thickness, const struct AmsState& ams,
                             uint8_t unitIndex, bool forceRedraw) {
  ScopedWrite sw(gfx);
  uint16_t bg = dispSettings.bgColor;
  uint16_t dim = CLR_TEXT_DIM;

  if (forceRedraw) {
    gfx.fillCircle(cx, cy, radius + 2, bg);
  }

  const int16_t innerR = radius - 2;

  // Fill circle background
  gfx.fillCircle(cx, cy, innerR, bg);

  // Stale-cache guard: MQTT parser keeps cached trays on partial updates
  // (bambu_mqtt.cpp ~line 508). When the selected unit drops, trays[ui*4..ui*4+3]
  // may still hold stale present=true entries. Without unitPresent, the gauge
  // would render a vanished AMS as if filaments were still loaded. NEVER fall
  // back to unit 0 — selecting a non-existent AMS must show all-X, not unit 0's data.
  const bool unitPresent = ams.present
                        && unitIndex < AMS_MAX_UNITS
                        && unitIndex < ams.unitCount
                        && ams.units[unitIndex].present;

  uint16_t hColor = unitPresent
      ? amsHumidityColor(ams.units[unitIndex].humidityRaw,
                         ams.units[unitIndex].humidity,
                         true)
      : CLR_TEXT_DIM;

  // Quadrant geometry: TL=Slot1, TR=Slot2, BR=Slot3, BL=Slot4
  static const int8_t qSlotX[4] = {-1, 1, 1, -1};
  static const int8_t qSlotY[4] = {-1, -1, 1, 1};
  const int16_t halfR = innerR;

  // Draw 4 colored quadrants
  for (int qi = 0; qi < AMS_TRAYS_PER_UNIT; qi++) {
    int16_t fx, fy, fw, fh;
    if (qi == 0)        { fx = cx - halfR;  fy = cy - halfR;  fw = halfR;  fh = halfR; }
    else if (qi == 1)   { fx = cx;           fy = cy - halfR;  fw = halfR;  fh = halfR; }
    else if (qi == 2)   { fx = cx;           fy = cy;          fw = halfR;  fh = halfR; }
    else                { fx = cx - halfR;  fy = cy;          fw = halfR;  fh = halfR; }

    bool slotPresent = false;
    int trayIdx = -1;
    if (unitPresent) {
      trayIdx = unitIndex * AMS_TRAYS_PER_UNIT + qi;
      slotPresent = ams.trays[trayIdx].present;
    }

    if (slotPresent) {
      const AmsTray& tray = ams.trays[trayIdx];
      uint16_t swatchColor = tray.colorRgb565;
      gfx.fillRect(fx + 1, fy + 1, fw - 2, fh - 2, swatchColor);

#if SCREEN_W >= 480
      // Full layout: draw type label + % + slot number inside each quadrant
      uint16_t borderCol = alphaBlend565(60, 0xFFFF, swatchColor);
      gfx.drawRoundRect(fx + 1, fy + 1, fw - 2, fh - 2, 2, borderCol);

      uint16_t txtColor = contrastTextColor565(swatchColor);
      int16_t qCenterX = cx + qSlotX[qi] * (halfR / 2);
      int16_t qCenterY = cy + qSlotY[qi] * (halfR / 2);
      const int16_t textInset = 5;
      int16_t tCX = qCenterX - qSlotX[qi] * textInset;
      int16_t tCY = qCenterY - qSlotY[qi] * textInset;

      // Slot number at quadrant outer corner (small, inside the quadrant)
      char slotNumBuf[2] = { (char)('0' + qi + 1), '\0' };
      int16_t snX = fx + (qSlotX[qi] > 0 ? fw - 8 : 6);
      int16_t snY = fy + (qSlotY[qi] > 0 ? fh - 8 : 4);
      gfx.setTextDatum(TL_DATUM);
      setFont(gfx, FONT_SMALL);
      gfx.setTextColor(txtColor, swatchColor);
      gfx.drawString(slotNumBuf, snX, snY);

      // Type label
      const char* typeLabel = getFilamentTypeLabel(tray.type);
      gfx.setTextDatum(MC_DATUM);
      setFont(gfx, FONT_SMALL);
      gfx.setTextColor(txtColor, swatchColor);
      gfx.drawString(typeLabel, tCX, tCY - 6);

      // Remaining %
      char remainBuf[8];
      if (tray.remain >= 0) {
        snprintf(remainBuf, sizeof(remainBuf), "%d%%", tray.remain);
      } else {
        strlcpy(remainBuf, "--%", sizeof(remainBuf));
      }
      setFont(gfx, FONT_SMALL);
      gfx.setTextColor(txtColor, swatchColor);
      gfx.drawString(remainBuf, tCX, tCY + 8);
#endif
    } else {
      // Empty slot or absent unit: diagonal X cross-out matches the 240x320
      // AMS slot style (display_ui.cpp ~line 1851). Outer corners get trimmed
      // by the circle-clip pass below.
      gfx.drawLine(fx + 1,      fy + 1,      fx + fw - 2, fy + fh - 2, dim);
      gfx.drawLine(fx + fw - 2, fy + 1,      fx + 1,      fy + fh - 2, dim);
    }
  }

  // Clip quadrant fill rectangles to circle boundary
  for (int16_t y = cy - innerR; y <= cy + innerR; y++) {
    int16_t dy = y - cy;
    int32_t d2 = (int32_t)dy * dy;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 >= r2) continue;
    int16_t halfChord = (int16_t)sqrtf((float)(r2 - d2));
    gfx.drawFastHLine(cx - innerR, y, innerR - halfChord, bg);
    gfx.drawFastHLine(cx + halfChord, y, innerR - halfChord, bg);
  }
  gfx.fillRect(cx - innerR, cy - innerR - 1, innerR * 2 + 1, 2, bg);
  gfx.fillRect(cx - innerR, cy + innerR, innerR * 2 + 1, 2, bg);

  // Circle boundary ring
  uint16_t ringColor = alphaBlend565(80, 0xFFFF, bg);
  gfx.drawCircle(cx, cy, innerR, ringColor);

  // Cross-hair separator lines (all dotted, quadrant colors make it clear)
  for (int16_t dx = 1; dx < innerR - 2; dx += 3) {
    int32_t d2 = (int32_t)dx * dx;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 < r2) gfx.drawPixel(cx + dx, cy, dim);
    if (d2 < r2) gfx.drawPixel(cx - dx, cy, dim);
  }
  for (int16_t dy = 3; dy < innerR - 2; dy += 3) {
    int32_t d2 = (int32_t)dy * dy;
    int32_t r2 = (int32_t)innerR * innerR;
    if (d2 < r2) gfx.drawPixel(cx, cy + dy, dim);
    if (d2 < r2) gfx.drawPixel(cx, cy - dy, dim);
  }

  // Center humidity indicator (only when unit actually present)
  const int16_t humCircleR = (radius >= 60) ? 16 : 11;
  gfx.fillCircle(cx, cy, humCircleR, bg);
  gfx.drawCircle(cx, cy, humCircleR, dim);
  if (unitPresent) {
    char humBuf[4];
    snprintf(humBuf, sizeof(humBuf), "H%d", ams.units[unitIndex].humidity);
    gfx.setTextDatum(MC_DATUM);
    setFont(gfx, radius >= 60 ? FONT_BODY : FONT_SMALL);
    gfx.setTextColor(hColor, bg);
    gfx.drawString(humBuf, cx, cy);
  }
}
