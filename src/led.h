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

#endif // LED_H
