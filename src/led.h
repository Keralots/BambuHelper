#ifndef LED_H
#define LED_H

#include <Arduino.h>

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
void shutdownLed();
void ledTick();

// Configured-brightness path (slider Save). Persists in caller via saveLedSettings().
void commitLedBrightness(uint8_t brightness);

// Transient-duty path (used by effect engine internally).
// Public for backward compat with any future direct callers.
void applyLedDuty(uint8_t duty);
void restoreLedDuty();

// Pin validation. sanitizeLedPin() is called from saveLedSettings() before NVS write.
void sanitizeLedPin();
bool isLedPinAllowed(uint8_t pin);

// Live preview from web UI: reconfigures LED with form values. NVS untouched.
void previewLed(bool enabled, uint8_t pin, uint8_t brightness);

// Effect engine
void ledSetActivity(LedActivity act);
void ledStartFinishEffect();
void ledStopFinishEffect();
bool ledFinishEffectActive();

// Triggered from web UI "Test effect" button. Plays the chosen mode for
// LED_TEST_DURATION_S seconds without waiting for a real print finish.
// Pass mode=0 to use ledSettings.finishMode. Returns true on success;
// false if no PWM channel is attached (LED neither enabled nor previewed)
// or the resolved mode is OFF/invalid - so the handler can report it.
bool ledTriggerTestEffect(uint8_t mode, uint16_t seconds, uint8_t peakBrightness);

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
