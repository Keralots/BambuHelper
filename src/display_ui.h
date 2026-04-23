#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

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
// Convenience reference — all callers use `tft.method()` unchanged.
extern lgfx::LovyanGFX& tft;

// Direct pointer to the AXS15231B panel wrapper; only non-null on
// BOARD_IS_JC3248W535 builds. Used by the sprite direct-push diagnostic.
extern lgfx::Panel_AXS15231B_AGFX* g_axs_panel;

void initDisplay();
void updateDisplay();
void setScreenState(ScreenState state);
ScreenState getScreenState();
void setBacklight(uint8_t level);
void applyDisplaySettings();  // re-apply rotation, bg, force redraw
void triggerDisplayTransition(); // start printer-name overlay on rotation
void checkNightMode();        // apply scheduled brightness dimming
uint8_t getEffectiveBrightness(); // current brightness (night or normal)

#endif // DISPLAY_UI_H
