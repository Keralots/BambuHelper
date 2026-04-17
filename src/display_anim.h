#ifndef DISPLAY_ANIM_H
#define DISPLAY_ANIM_H

#include <LovyanGFX.hpp>

// Rotating arc spinner for connecting screens
void drawSpinner(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, int16_t radius,
                 uint16_t color);

// Animated dots "..." that cycle (call each frame, uses millis())
void drawAnimDots(lgfx::LovyanGFX& tft, int16_t x, int16_t y, uint16_t color);

// Pulsing glow on arc edge (returns brightness factor 0.5-1.0)
float getPulseFactor();

// Indeterminate slide bar (call each frame — uses millis() internally)
void drawSlideBar(lgfx::LovyanGFX& tft, int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint16_t trackColor);

// Completion animation: expanding checkmark ring
void drawCompletionAnim(lgfx::LovyanGFX& tft, int16_t cx, int16_t cy, bool reset);

#endif // DISPLAY_ANIM_H
