#include "led.h"
#include "settings.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
//  Pin attachment + duty tracking
// ---------------------------------------------------------------------------
static int8_t  attachedPin = -1;
static int16_t lastWrittenDuty = -1;  // -1 = unknown, force first write

// ---------------------------------------------------------------------------
//  Effect engine state
// ---------------------------------------------------------------------------
static LedActivity   currentActivity      = LED_ACT_IDLE;
static bool          finishEffectActive   = false;
static unsigned long finishEffectStartMs  = 0;
static unsigned long finishEffectEndMs    = 0;
static uint8_t       finishEffectMode     = LED_FINISH_OFF;
static uint8_t       finishEffectPeak     = 255;
static unsigned long lastTickMs           = 0;

// Live-preview override. While set, ledTick() uses previewBrightness as the
// resting/peak duty instead of ledSettings.brightness, so the 60Hz tick stops
// fighting rapid slider previews with the stale saved value. Cleared on
// initLed() (called after save) and shutdownLed().
static bool          previewActive        = false;
static uint8_t       previewBrightness    = 0;

static inline uint8_t restingBrightness() {
  return previewActive ? previewBrightness : ledSettings.brightness;
}

// ---------------------------------------------------------------------------
//  Pin deny-list (board-specific peripheral conflicts)
// ---------------------------------------------------------------------------
bool isLedPinAllowed(uint8_t pin) {
  if (pin == 0) return false;

  // Generic dynamic conflicts (all boards)
  if (pin == BACKLIGHT_PIN) return false;
  if (buzzerSettings.enabled && pin == buzzerSettings.pin) return false;
  if (buttonType != BTN_DISABLED && pin == buttonPin) return false;

#if defined(DISPLAY_CYD)
  // CYD (classic ESP32, esp32dev)
  if (pin == 2 || pin == 12 || pin == 13 || pin == 14 || pin == 15) return false;  // display SPI
  if (pin == 25 || pin == 32 || pin == 33 || pin == 36 || pin == 39) return false; // XPT2046 touch
  if (pin == 4 || pin == 16 || pin == 17) return false;                            // onboard RGB
  if (pin == 26) return false;                                                     // onboard speaker amp
  if (pin >= 6 && pin <= 11) return false;                                         // SPI flash
  if (pin >= 34 && pin <= 39) return false;                                        // input-only
  if (pin > 39) return false;

#elif defined(BOARD_IS_S3)
  // LOLIN S3 mini + ST7789 240x240
  if (pin == 8 || pin == 9 || pin == 10 || pin == 11 || pin == 12) return false;   // display SPI
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM (qio_qspi)
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS200)
  // Waveshare ESP32-S3-Touch-LCD-2.0"
  if (pin == 38 || pin == 39 || pin == 40 || pin == 42 || pin == 45) return false; // display SPI
  if (pin == 47 || pin == 48) return false;                                        // CST816D I2C
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS154)
  // Waveshare ESP32-S3-Touch-LCD-1.54"
  if (pin == 21 || pin == 38 || pin == 39 || pin == 40 || pin == 45) return false; // display SPI
  if (pin == 41 || pin == 42 || pin == 47 || pin == 48) return false;              // CST816 I2C + RST/IRQ
  if (pin == 8 || pin == 9 || pin == 10 || pin == 12) return false;                // ES8311 I2S
  if (pin == 7) return false;                                                      // audio PA ctrl
  if (pin == 2) return false;                                                      // BAT_EN
  if (pin == 0 || pin == 4 || pin == 5) return false;                              // board buttons
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_C3)
  // LOLIN C3 mini
  if (pin == 6 || pin == 7 || pin == 10 || pin == 20 || pin == 21) return false;   // display SPI
  if (pin == 18 || pin == 19) return false;                                        // USB CDC D-/D+
  if (pin >= 11 && pin <= 17) return false;                                        // flash/PSRAM
  if (pin > 21) return false;
#endif

  return true;
}

void sanitizeLedPin() {
  // Clamp effect fields first - independent of pin/enabled state, so NVS-loaded
  // garbage gets normalized even on the default disabled+pin=0 path.
  if (ledSettings.finishMode > LED_FINISH_HEARTBEAT) ledSettings.finishMode = LED_FINISH_OFF;
  if (ledSettings.finishSeconds < 5)   ledSettings.finishSeconds = 5;
  if (ledSettings.finishSeconds > 600) ledSettings.finishSeconds = 600;

  // Default unset state (disabled + pin=0) is valid - skip pin validation.
  if (!ledSettings.enabled && ledSettings.pin == 0) return;
  if (!isLedPinAllowed(ledSettings.pin)) {
    Serial.printf("LED: pin %d not allowed, disabling\n", ledSettings.pin);
    ledSettings.enabled = false;
    ledSettings.pin = 0;
  }
}

// ---------------------------------------------------------------------------
//  Low-level duty write (with change-detection to skip redundant SPI traffic)
// ---------------------------------------------------------------------------
static void writeDuty(uint8_t duty) {
  if (attachedPin < 0) return;
  if ((int16_t)duty == lastWrittenDuty) return;
  ledcWrite(LED_PWM_CH, duty);
  lastWrittenDuty = duty;
}

static void detachAndForceLow() {
  if (attachedPin < 0) return;
  ledcWrite(LED_PWM_CH, 0);
  ledcDetachPin(attachedPin);
  pinMode(attachedPin, OUTPUT);
  digitalWrite(attachedPin, LOW);
  attachedPin = -1;
  lastWrittenDuty = -1;
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
void initLed() {
  // Cancel any in-flight effect from previous config (e.g. user changed pin
  // while the finish-pulse was still running).
  finishEffectActive = false;
  currentActivity = LED_ACT_IDLE;
  previewActive = false;

  detachAndForceLow();
  // Disabled with a saved pin: drive it LOW instead of leaving it high-Z, so the
  // BJT/MOSFET gate is held off by firmware (independent of any external pulldown).
  // Only if the pin is allowed - never poke peripherals (display SPI, touch, etc.).
  if (!ledSettings.enabled) {
    if (ledSettings.pin != 0 && isLedPinAllowed(ledSettings.pin)) {
      pinMode(ledSettings.pin, OUTPUT);
      digitalWrite(ledSettings.pin, LOW);
    }
    return;
  }
  if (!isLedPinAllowed(ledSettings.pin)) return;
  ledcSetup(LED_PWM_CH, LED_PWM_FREQ, LED_PWM_RES);
  ledcAttachPin(ledSettings.pin, LED_PWM_CH);
  attachedPin = ledSettings.pin;
  lastWrittenDuty = -1;
  writeDuty(ledSettings.brightness);
}

void shutdownLed() {
  finishEffectActive = false;
  previewActive = false;
  detachAndForceLow();
}

void commitLedBrightness(uint8_t brightness) {
  ledSettings.brightness = brightness;
  // Don't fight an active effect - tick will use the new brightness once effect ends.
  if (finishEffectActive) return;
  if (attachedPin >= 0 && ledSettings.enabled) writeDuty(brightness);
}

void applyLedDuty(uint8_t duty) {
  if (attachedPin >= 0 && ledSettings.enabled) writeDuty(duty);
}

void restoreLedDuty() {
  if (attachedPin >= 0 && ledSettings.enabled) writeDuty(ledSettings.brightness);
}

void previewLed(bool enabled, uint8_t pin, uint8_t brightness) {
  // Preview overrides any active effect - user is actively configuring.
  finishEffectActive = false;

  if (!enabled || !isLedPinAllowed(pin)) {
    previewActive = false;
    detachAndForceLow();
    return;
  }
  if (attachedPin != (int8_t)pin) {
    detachAndForceLow();
    ledcSetup(LED_PWM_CH, LED_PWM_FREQ, LED_PWM_RES);
    ledcAttachPin(pin, LED_PWM_CH);
    attachedPin = pin;
    lastWrittenDuty = -1;
  }
  // Latch the preview brightness so ledTick()'s resting-duty path uses it
  // instead of the stale saved value (which would otherwise overwrite the
  // preview every 16ms and flicker the LED).
  previewActive = true;
  previewBrightness = brightness;
  writeDuty(brightness);
}

// ---------------------------------------------------------------------------
//  Effect engine
// ---------------------------------------------------------------------------
void ledSetActivity(LedActivity act) {
  currentActivity = act;
}

void ledStartFinishEffect() {
  if (!ledSettings.enabled) return;
  if (ledSettings.finishMode == LED_FINISH_OFF) return;
  if (attachedPin < 0) return;
  finishEffectMode    = ledSettings.finishMode;
  finishEffectPeak    = ledSettings.finishBrightness;
  finishEffectStartMs = millis();
  finishEffectEndMs   = finishEffectStartMs + (unsigned long)ledSettings.finishSeconds * 1000UL;
  finishEffectActive  = true;
}

void ledStopFinishEffect() {
  if (!finishEffectActive) return;
  finishEffectActive = false;
  // Force tick to recompute resting duty (idle/auto-on/etc.) on next call.
  lastWrittenDuty = -1;
}

bool ledFinishEffectActive() {
  return finishEffectActive;
}

bool ledTriggerTestEffect(uint8_t mode, uint16_t seconds, uint8_t peakBrightness) {
  // attachedPin is the right gate (not ledSettings.enabled) so the test works
  // for a previewed-but-unsaved LED config; ledTick() also runs during preview.
  if (attachedPin < 0) return false;
  if (mode == 0) mode = ledSettings.finishMode;
  if (mode == LED_FINISH_OFF) return false;
  if (mode > LED_FINISH_HEARTBEAT) return false;
  if (seconds < 1) seconds = 1;
  if (seconds > 600) seconds = 600;
  finishEffectMode    = mode;
  finishEffectPeak    = peakBrightness;
  finishEffectStartMs = millis();
  finishEffectEndMs   = finishEffectStartMs + (unsigned long)seconds * 1000UL;
  finishEffectActive  = true;
  return true;
}

void ledOnUserInteraction() {
  if (finishEffectActive) ledStopFinishEffect();
}

// ---------------------------------------------------------------------------
//  Pattern generators (all return 0..peak)
// ---------------------------------------------------------------------------
static uint8_t patternBreath(unsigned long phaseMs, uint16_t periodMs, uint8_t peak) {
  if (periodMs == 0) return 0;
  float t = (float)(phaseMs % periodMs) / (float)periodMs;       // 0..1
  float v = 0.5f - 0.5f * cosf(t * 2.0f * (float)M_PI);          // 0..1 sinus
  return (uint8_t)(v * (float)peak);
}

// Heartbeat: two short pulses then a pause, repeating.
// Within a 1500ms window (LED_HEARTBEAT_PERIOD_MS):
//   0..150   ramp up to peak  (first beat rising)
//   150..300 ramp down to 0   (first beat falling)
//   300..450 ramp up to peak  (second beat rising)
//   450..600 ramp down to 0   (second beat falling)
//   600..1500 silence
static uint8_t patternHeartbeat(unsigned long phaseMs, uint16_t periodMs, uint8_t peak) {
  if (periodMs == 0) return 0;
  unsigned long p = phaseMs % periodMs;
  if (p < 150)  return (uint8_t)((p * peak) / 150);
  if (p < 300)  return (uint8_t)(((300 - p) * peak) / 150);
  if (p < 450)  return (uint8_t)(((p - 300) * peak) / 150);
  if (p < 600)  return (uint8_t)(((600 - p) * peak) / 150);
  return 0;
}

static uint8_t patternStrobe(unsigned long phaseMs, uint16_t halfMs, uint8_t peak) {
  if (halfMs == 0) return peak;
  return ((phaseMs / halfMs) & 1) ? 0 : peak;
}

// ---------------------------------------------------------------------------
//  Tick (called from main loop)
// ---------------------------------------------------------------------------
void ledTick() {
  // Run when saved-on OR while a preview is active, so the test effect can
  // animate for a previewed-but-unsaved LED config.
  if (!ledSettings.enabled && !previewActive) return;
  if (attachedPin < 0) return;

  unsigned long now = millis();
  if (now - lastTickMs < LED_TICK_MIN_INTERVAL_MS) return;
  lastTickMs = now;

  // Auto-stop finish effect on timeout.
  if (finishEffectActive && (long)(now - finishEffectEndMs) >= 0) {
    finishEffectActive = false;
    lastWrittenDuty = -1;  // force resting duty on this tick
  }

  uint8_t duty;

  // Priority order, high to low:
  // 1) Error strobe (FAILED) - real printer fault, beats config UX
  // 2) Finish effect (one-shot timed) - and the "Test effect" button uses it
  // 3) Live preview - user is actively dragging the brightness slider; must
  //    bypass auto-on/pause-breath so they don't gate the LED to 0 mid-drag
  // 4) Pause breathing (continuous while paused)
  // 5) Auto on/off based on activity
  // 6) Idle: configured brightness
  uint8_t baseBrightness = restingBrightness();
  if (ledSettings.errorStrobe && currentActivity == LED_ACT_FAILED) {
    duty = patternStrobe(now, LED_ERROR_STROBE_MS, baseBrightness);
  } else if (finishEffectActive) {
    unsigned long phase = now - finishEffectStartMs;
    if (finishEffectMode == LED_FINISH_HEARTBEAT) {
      duty = patternHeartbeat(phase, LED_HEARTBEAT_PERIOD_MS, finishEffectPeak);
    } else {
      duty = patternBreath(phase, LED_BREATH_PERIOD_MS, finishEffectPeak);
    }
  } else if (previewActive) {
    duty = previewBrightness;
  } else if (ledSettings.pauseBreathing && currentActivity == LED_ACT_PAUSED) {
    duty = patternBreath(now, LED_PAUSE_PERIOD_MS, baseBrightness);
  } else if (ledSettings.autoOnWhilePrinting) {
    duty = (currentActivity == LED_ACT_PRINTING) ? baseBrightness : 0;
  } else {
    duty = baseBrightness;
  }

  writeDuty(duty);
}
