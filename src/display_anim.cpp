#include "display_anim.h"
#include "config.h"
#include "settings.h"
#include "icons.h"
#include "fonts.h"

// Use user-configured bg color instead of hardcoded CLR_BG
#undef  CLR_BG
#define CLR_BG  (dispSettings.bgColor)

// ---------------------------------------------------------------------------
//  Rotating arc spinner
// ---------------------------------------------------------------------------
static uint16_t spinnerAngle = 0;

void drawSpinner(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                 uint16_t color) {
  // Erase previous arc segment (handle wrap-around)
  uint16_t prevStart = (spinnerAngle + 360 - 12) % 360;
  uint16_t prevEnd = (prevStart + 60) % 360;
  if (prevEnd > prevStart) {
    gfx.drawArc(cx, cy, radius, radius - 4,
                prevStart, prevEnd, CLR_BG);
  } else {
    gfx.drawArc(cx, cy, radius, radius - 4,
                prevStart, 360, CLR_BG);
    gfx.drawArc(cx, cy, radius, radius - 4,
                0, prevEnd, CLR_BG);
  }

  // Advance angle
  spinnerAngle = (spinnerAngle + 12) % 360;
  uint16_t arcStart = spinnerAngle;
  uint16_t arcEnd = (spinnerAngle + 60) % 360;

  // Draw arc segment (handle wrap-around)
  if (arcEnd > arcStart) {
    gfx.drawArc(cx, cy, radius, radius - 4,
                arcStart, arcEnd, color);
  } else {
    gfx.drawArc(cx, cy, radius, radius - 4,
                arcStart, 360, color);
    gfx.drawArc(cx, cy, radius, radius - 4,
                0, arcEnd, color);
  }
}

// ---------------------------------------------------------------------------
//  Animated dots "..."
// ---------------------------------------------------------------------------
void drawAnimDots(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, uint16_t color) {
  unsigned long ms = millis();
  int phase = (ms / 400) % 4;

  setFont(gfx, LY_F_BODY);
  gfx.setTextDatum(TL_DATUM);

  for (int i = 0; i < 3; i++) {
    uint16_t dotColor = (i < phase) ? color : CLR_TEXT_DARK;
    gfx.setTextColor(dotColor, CLR_BG);
    gfx.drawString(".", x + i * LY_SC(8), y);
  }
}

// ---------------------------------------------------------------------------
//  Indeterminate slide bar — a glowing segment slides back and forth
// ---------------------------------------------------------------------------
void drawSlideBar(lgfx::LovyanGFX& gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint16_t trackColor) {
  // Draw track (also erases previous segment position)
  gfx.fillRoundRect(x, y, w, h, h / 2, trackColor);

  // Segment: 25% of bar width, bounces smoothly using sine
  const int16_t segW = w / 4;
  float t = (millis() % 1600) / 1600.0f;
  float pos = (sinf(t * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f; // 0..1
  int16_t segX = x + (int16_t)(pos * (float)(w - segW));

  gfx.fillRoundRect(segX, y, segW, h, h / 2, color);
}
