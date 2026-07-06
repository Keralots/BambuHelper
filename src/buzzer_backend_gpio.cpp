#include "buzzer_backend.h"
#include "buzzer.h"
#include "settings.h"
#include "config.h"

// I2S audio boards (ES8311 codec, NS4168 amp) have their own backends
#if !defined(BOARD_HAS_ES8311_AUDIO) && !defined(BOARD_HAS_NS4168_AUDIO)

// CYD ESP32-32E clone: the speaker amp (FM8002E) is gated by a control pin on
// GPIO4 that is ACTIVE LOW - per lcdwiki "Audio enable signal, low level
// enable, high level disable". Hold it low for the whole time the buzzer is
// enabled instead of toggling per tone - amp power-up takes ~100ms, which
// would swallow short beeps. This matches the classic CYD experience, whose
// amp is hardwired on.
static void cyd32eAmpEnable(bool on) {
#if defined(DISPLAY_CYD)
  if (!dispSettings.cyd32eVariant) return;
  pinMode(CYD32E_AMP_EN_PIN, OUTPUT);
  digitalWrite(CYD32E_AMP_EN_PIN, on ? LOW : HIGH);
#else
  (void)on;
#endif
}

void buzzerBackendInit() {
  sanitizeBuzzerPin();
  if (buzzerSettings.pin == 0) {
    // No usable tone pin: park the 32E amp muted so GPIO4 doesn't float
    cyd32eAmpEnable(false);
    return;
  }
  pinMode(buzzerSettings.pin, OUTPUT);
  digitalWrite(buzzerSettings.pin, LOW);
  cyd32eAmpEnable(true);
}

void buzzerBackendApplyStep(uint16_t freq) {
  if (buzzerSettings.pin == 0) return;
  if (freq > 0) {
    tone(buzzerSettings.pin, freq);
  } else {
    noTone(buzzerSettings.pin);
    digitalWrite(buzzerSettings.pin, LOW);
  }
}

void buzzerBackendStop() {
  sanitizeBuzzerPin();
  if (buzzerSettings.pin == 0) return;
  noTone(buzzerSettings.pin);
  digitalWrite(buzzerSettings.pin, LOW);
}

void buzzerBackendTick() {
}

void buzzerBackendShutdown() {
  buzzerBackendStop();
  cyd32eAmpEnable(false);
}

#endif // !BOARD_HAS_ES8311_AUDIO && !BOARD_HAS_NS4168_AUDIO
