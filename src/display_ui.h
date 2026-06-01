#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdint.h>
#include <LovyanGFX.hpp>

// Forward-declare the panel type so callers can use the pointer without
// pulling in the full header (which includes Arduino_GFX headers).
namespace lgfx { inline namespace v1 { class Panel_AXS15231B_AGFX; } }

enum ScreenState {
  SCREEN_SPLASH,
  SCREEN_AP_MODE,
  SCREEN_CONNECTING_WIFI,
  SCREEN_WIFI_CONNECTED,
  SCREEN_CONNECTING_MQTT,
  SCREEN_IDLE,
  SCREEN_PRINTING,
  SCREEN_FINISHED,
  SCREEN_CLOCK,
  SCREEN_OFF,
  SCREEN_OTA_UPDATE
};

extern lgfx::LovyanGFX* tft_ptr;
// Macro (NOT a reference) so callers' `tft.method()` always dereferences the
// current value of `tft_ptr`. On JC3248W535 we retarget this pointer to a
// PSRAM sprite at runtime; a C++ reference would have been permanently
// bound to the panel at static-init time, defeating the redirection.
#define tft (*tft_ptr)

// Direct pointer to the AXS15231B panel wrapper; only non-null on
// BOARD_IS_JC3248W535 builds. Used by the sprite direct-push diagnostic.
extern lgfx::Panel_AXS15231B_AGFX* g_axs_panel;

void initDisplay();
void updateDisplay();

// Flush the off-screen framebuffer sprite to the panel in one contiguous
// raster write. No-op on boards that draw directly (all except
// BOARD_IS_JC3248W535, which uses a full-screen PSRAM sprite to work around
// the AXS15231B QSPI-mode addressing limits). Call once per loop tick after
// UI draws to commit the frame.
void flushFrame();

// Mark the off-screen framebuffer sprite as dirty so the next flushFrame()
// actually pushes to the panel. No-op on boards that draw directly (all
// except BOARD_IS_JC3248W535). Call from any code path that writes pixels
// into the sprite; a keepalive tick in flushFrame() guarantees the panel
// still gets a refresh even if a dirty mark is missed.
void markFrameDirty();

void setScreenState(ScreenState state);
ScreenState getScreenState();
void setBacklight(uint8_t level);
void applyDisplaySettings();  // re-apply rotation, bg, force redraw
void triggerDisplayTransition(); // start printer-name overlay on rotation
void checkNightMode();        // apply scheduled brightness dimming
uint8_t getEffectiveBrightness(); // current brightness (night or normal)

// Runtime display control (HTTP API) — not persisted to flash
void setDisplayForcedOff(bool off);
bool isDisplayForcedOff();
void setDisplayBrightnessPercent(uint8_t percent);

// Pixel orbit — slow coordinate shift to spread OLED wear
void checkOrbit();
void setOrbitEnabled(bool en);
bool isOrbitEnabled();

#endif // DISPLAY_UI_H
