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
  SCREEN_OTA_UPDATE,
  SCREEN_SPLIT,         // two printers side-by-side (top/bottom bands)
  SCREEN_CAMERA,        // fullscreen P1/A1 chamber image (#120); tap to exit
  SCREEN_POWER_CONFIRM  // fullscreen plug on/off confirmation (#136); hold to confirm
};

// Read-only snapshot of the plug power-confirm modal, filled by main.cpp so the
// renderer (display_ui.cpp) never reaches into main's statics. phase mirrors the
// PowerConfirmPhase enum in main.cpp: 0=wait-release, 1=armed, 2=sending, 3=result.
struct PowerConfirmView {
  const char* name;    // target printer name
  bool  desiredOn;     // true = turning plug ON, false = OFF
  bool  warn;          // printer is currently printing -> red screen
  bool  offline;       // plug reading is stale/offline (live)
  float progress;      // 0..1 hold-to-confirm ring fill
  int   phase;         // see above
  bool  resultOk;      // valid in phase 3: command succeeded
};
// Returns false when the modal is not active. Defined in main.cpp.
bool powerConfirmGetView(PowerConfirmView* out);
// Called by the renderer once it has drawn the "Sending..." frame, so main.cpp
// only fires the (blocking) relay command after that frame is on screen.
void powerConfirmMarkSendingDrawn();

// Forward declaration so split-related declarations below can take an AmsState
// by reference without pulling in bambu_state.h here.
struct AmsState;

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

// True only on portrait layout profiles that define LAYOUT_HAS_SPLIT and are not
// currently in a landscape rotation. Gates the split (dual-printer) screen so it
// never engages on 480x480 / landscape boards.
bool displaySupportsSplit();

// True during a frame that must fully repaint (screen change or
// triggerDisplayTransition). The split renderer reads this to force both bands.
bool isDisplayForceRedraw();

// AMS "bars" gauge tile (one rounded bar per loaded tray, no humidity). Defined
// in display_ui.cpp; exported so the split renderer can draw it inside a band.
void drawAmsBarsGauge(int16_t cx, int16_t cy, int16_t radius,
                      const AmsState& ams, uint8_t unitIndex, bool forceRedraw);

// Camera thumbnail tile (#120). Draws the latest chamber-image still into the
// slot, or a placeholder when not streaming. Exported so the split renderer can
// draw the inert placeholder inside a band. No-op visual on non-camera boards.
void drawCameraGauge(int16_t cx, int16_t cy, int16_t radius, bool forceRedraw);

struct BambuState;

// Shared configurable-slot tile dispatcher (defined in display_ui.cpp).
// gaugeTileValueChanged mirrors the per-type change detection of the printing
// screen's slot loop; drawGaugeTile renders one slot of any GaugeType.
// smooth=true uses the global smoothed arc values - only valid when the tile
// shows the currently displayed printer (round Rim skin); the split screen
// passes false because the smooth floats cannot track two printers at once.
bool gaugeTileValueChanged(uint8_t gt, const BambuState& s, const BambuState& p);
void drawGaugeTile(uint8_t gt, const BambuState& s, uint8_t slotIndex,
                   int16_t cx, int16_t cy, int16_t r, int16_t t, bool fr,
                   bool smooth);

// Nozzle label honoring custom overrides + R/L side ('R'/'L'/0). Exported so the
// split renderer formats nozzle labels identically. Returns a static buffer.
const char* nozzleSideLabel(char side);

// AMS label formatters. All honor the custom gaugeLabels.amsBase override
// (default "AMS"), keeping the numeric/letter/HT suffix. Exported so the split
// renderer formats AMS labels identically. Each writes a NUL-terminated string.
void formatAmsNumberLabel(char* out, size_t len, uint8_t unitIndex);   // "<base> 1".."4"
void formatAmsLetterLabel(char* out, size_t len, uint8_t unitIndex);   // "<base> A".."D"
void formatAmsDryName(char* out, size_t len, bool isHT, uint8_t displayNum,
                      uint8_t dryDisplayIdx, uint8_t dryCount);        // "<base>[ HT] N  (x/y)"

// Build the finish-time line shared by the printing, split, round and drying
// screens, and return the color it should be drawn in.
//   mode           - a dispSettings.timeDisplayMode value: 0 = wall-clock ETA,
//                    1 = remaining duration, 2 = both on one line. Callers that
//                    must pin one form (the drying screen, whose layout already
//                    carries the duration) pass it literally instead of reading
//                    the setting.
//   labelRemaining - false emits a bare "2h 05m" for the tight split bands.
//   maxW           - when > 0, step down to the most compact form that fits this
//                    pixel budget at the currently loaded font. Only the curved
//                    round skins need it; everyone else passes 0.
// Falls back to the duration whenever NTP has not synced, whatever mode asks for.
uint16_t formatEtaLine(uint16_t remainingMin, uint8_t mode, bool labelRemaining,
                       int16_t maxW, char* buf, size_t n);

#endif // DISPLAY_UI_H
