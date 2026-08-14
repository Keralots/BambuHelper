#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include "config.h"   // HAS_HMS_UI

// Printer activity for state-driven LED behaviors (auto on/off, pause, error)
enum LedActivity : uint8_t {
  LED_ACT_IDLE     = 0,
  LED_ACT_PRINTING = 1,
  LED_ACT_PAUSED   = 2,
  LED_ACT_FINISHED = 3,
  LED_ACT_FAILED   = 4,
};

// Lifecycle
void initLed();
void ledTick();

// Transient-duty path (used by effect engine internally). On a colour driver the
// duty is the intensity envelope and the colour comes from the current activity.
// Public for backward compat with any future direct callers.
void applyLedDuty(uint8_t duty);

// Pin validation. sanitizeLedPin() is called from saveLedSettings() before NVS write.
void sanitizeLedPin();
bool isLedPinAllowed(uint8_t pin);

// Same question for a colour driver, which is allowed to claim the board's own
// onboard RGB pins - the deny-list reserves those precisely so nothing else does.
bool isRgbLedPinAllowed(uint8_t pin);

// Onboard RGB pin defaults for the portal's "use the onboard LED" shortcut.
// Returns false when the board has no onboard RGB, leaving the outputs untouched.
// On the CYD the red pin depends on the ESP32-32E runtime variant.
bool onboardRgbPins(uint8_t &r, uint8_t &g, uint8_t &b, bool &anode);

// Live preview from web UI: reconfigures LED with form values. NVS untouched.
// color is 0xRRGGBB and is ignored by LED_DRV_SINGLE.
void previewLed(bool enabled, uint8_t driver, uint8_t pinR, uint8_t pinG,
                uint8_t pinB, bool commonAnode, uint8_t brightness, uint32_t color);

// Effect engine
void ledSetActivity(LedActivity act);
void ledStartFinishEffect();
void ledStopFinishEffect();

#if HAS_HMS_UI
// One-shot error strobe for a printer error report. The existing strobe is
// level-driven off LED_ACT_FAILED, which a printer raising an HMS mid-print
// never enters, so this arms the same pattern for a bounded window instead.
// Silenced early by ledOnUserInteraction(), like the FAILED strobe.
void ledStartErrorEpisode();

// End the episode because the fault itself cleared. The window is bounded, but
// waiting it out after the printer has recovered leaves the LED strobing over a
// resumed print - and re-arming on every reappearance of a flapping code made
// that look permanent (issue #165). No-op when no episode is running.
void ledStopErrorEpisode();
#endif

// Triggered from web UI "Test effect" button. Plays the chosen mode for
// LED_TEST_DURATION_S seconds without waiting for a real print finish.
// Pass mode=0 to use ledSettings.finishMode. Returns true on success;
// false if no PWM channel is attached (LED neither enabled nor previewed)
// or the resolved mode is OFF/invalid - so the handler can report it.
// color is 0xRRGGBB for the colour drivers; pass 0xFFFFFFFF to use the saved
// finished colour.
bool ledTriggerTestEffect(uint8_t mode, uint16_t seconds, uint8_t peakBrightness,
                          uint32_t color);

// User interaction (button/touch) hook - aborts any active finish effect.
void ledOnUserInteraction();

// Display-sleep coupling: true while the panel is in SCREEN_OFF - the status
// LED goes dark and stays dark until resumed. Driven from the main loop when
// the screen state crosses the OFF boundary.
void ledSetSuspended(bool suspended);

// Hold-to-dim. Must be called every loop iteration regardless of hold state -
// it owns the 2 s NVS save debounce. Returns true while this press should be
// treated as a hold and tap actions suppressed (throughout DIM_ACTIVE including
// forced aborts, plus the tick that transitions IDLE -> ACTIVE); false otherwise.
//   heldNow     - any tracked input currently held
//   holdMs      - duration of the held source (0 if none)
//   suppressDim - caller-asserted abort (e.g. BOARD_BTN_3 co-pressed = shutdown combo)
bool ledHoldDimUpdate(bool heldNow, uint32_t holdMs, bool suppressDim);

#endif // LED_H
