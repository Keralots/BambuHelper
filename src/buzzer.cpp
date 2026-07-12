#include "buzzer.h"
#include "buzzer_backend.h"
#include "settings.h"
#include "config.h"
#include <time.h>

void sanitizeBuzzerPin() {
  // Pin 0 = disabled (no buzzer). Pin 255 is invalid on ESP32 — clamp to 0.
  // This also handles stale NVS values from firmware that used 255 as "disabled".
  if (buzzerSettings.pin == 0 || buzzerSettings.pin == 255) {
    buzzerSettings.pin = 0;
    return;
  }
#if defined(BOARD_IS_WS350)
  // ws_lcd_350 has no buzzer hardware and the GPIO backend drives the pin LOW
  // on init/stop even when the buzzer is disabled. The display SPI (SCLK=5,
  // MOSI=1, MISO=2, DC=3) and shared I2C (SDA=8, SCL=7) lines must never be
  // driven as a buzzer GPIO or the panel freezes. The default pin is 0, but a
  // stale/manual NVS value (e.g. 5 from a pre-fix build) can still land here.
  if (buzzerSettings.pin == 1 || buzzerSettings.pin == 2 ||
      buzzerSettings.pin == 3 || buzzerSettings.pin == 5 ||
      buzzerSettings.pin == 6 || buzzerSettings.pin == 7 ||
      buzzerSettings.pin == 8) {
    Serial.printf("Buzzer: pin %d reserved on WS350 (display/I2C), disabling\n", buzzerSettings.pin);
    buzzerSettings.pin = 0;
    return;
  }
#endif
#if defined(BOARD_IS_ES3N28P)
  // QD ES3N28P has no GPIO buzzer; the GPIO backend drives the pin LOW on
  // init/stop even when disabled. The default pin is 0, but a stale/manual NVS
  // value can still land here. Reject the full reserved set (kept in sync with
  // the ES3N28P LED deny-list in led.cpp and the button guard in button.cpp):
  // display SPI 10/11/12/13/46, audio+amp 1/4/5/6/7/8, touch 15/16/17/18,
  // battery 9, WS2812 42, microSD 38/39/40/41/47/48, USB CDC 19/20, and the
  // octal flash/PSRAM range 26-37 (driving USB or PSRAM pins can wedge CDC or
  // corrupt PSRAM). Backlight 45 is caught by the BACKLIGHT_PIN check below.
  {
    uint8_t p = buzzerSettings.pin;
    bool reserved =
      (p == 10 || p == 11 || p == 12 || p == 13 || p == 46) ||  // display SPI
      (p == 1 || p == 4 || p == 5 || p == 6 || p == 7 || p == 8) ||  // audio + amp
      (p == 15 || p == 16 || p == 17 || p == 18) ||             // touch I2C/INT/RST
      (p == 9) ||                                                // battery ADC
      (p == 42) ||                                               // WS2812
      (p == 38 || p == 39 || p == 40 || p == 41 || p == 47 || p == 48) || // microSD
      (p == 19 || p == 20) ||                                    // USB CDC
      (p >= 26 && p <= 37);                                      // flash/PSRAM
    if (reserved) {
      Serial.printf("Buzzer: pin %d reserved on ES3N28P, disabling\n", p);
      buzzerSettings.pin = 0;
      return;
    }
  }
#endif
#if defined(BOARD_IS_SC05X)
  // Panlee SC05_X / ZX2D80CE02S has no GPIO buzzer in this target. Reject the
  // full reserved set (kept in sync with led.cpp and button.cpp): 8080 LCD
  // bus/control 1/2/7/15/16/17/18/40/41/42, reset 3, touch 8/9/48, TE 38,
  // RS485 4/5/6, USB CDC 19/20, and the flash/PSRAM range 26-37.
  {
    uint8_t p = buzzerSettings.pin;
    bool reserved =
      (p == 1 || p == 2 || p == 7 || p == 15 || p == 16 ||
       p == 17 || p == 18 || p == 40 || p == 41 || p == 42) ||
      (p == 3) ||                                                // LCD reset
      (p == 8 || p == 9 || p == 48) ||                           // touch I2C/INT
      (p == 38) ||                                               // LCD_TE
      (p == 4 || p == 5 || p == 6) ||                            // RS485
      (p == 19 || p == 20) ||                                    // USB CDC
      (p >= 26 && p <= 37);                                      // flash/PSRAM
    if (reserved) {
      Serial.printf("Buzzer: pin %d reserved on SC05_X, disabling\n", p);
      buzzerSettings.pin = 0;
      return;
    }
  }
#endif
#if defined(DISPLAY_CYD)
  // ESP32-32E clone variant: GPIO4 is the speaker amp enable, not a tone
  // output - the GPIO backend would hijack it and mute the amp.
  if (dispSettings.cyd32eVariant && buzzerSettings.pin == CYD32E_AMP_EN_PIN) {
    Serial.printf("Buzzer: pin %d is the 32E amp enable, disabling\n", buzzerSettings.pin);
    buzzerSettings.pin = 0;
    return;
  }
#endif
#if defined(BACKLIGHT_PIN)
  if (buzzerSettings.pin == BACKLIGHT_PIN) {
    Serial.printf("Buzzer: pin %d conflicts with backlight, disabling\n", buzzerSettings.pin);
    buzzerSettings.pin = 0;
  }
#endif
}

// ---------------------------------------------------------------------------
//  Tone patterns (frequency Hz, duration ms) - 0 freq = pause
// ---------------------------------------------------------------------------
struct ToneStep { uint16_t freq; uint16_t ms; };

static const ToneStep melodyFinished[] = {
  {1047, 120}, {0, 40},   // C6
  {1319, 120}, {0, 40},   // E6
  {1568, 120}, {0, 40},   // G6
  {2093, 250},             // C7
};

static const ToneStep melodyError[] = {
  {880, 100}, {0, 80},
  {880, 100}, {0, 80},
  {880, 100},
};

static const ToneStep melodyConnected[] = {
  {1047, 80}, {0, 40},
  {1568, 120},
};

static const ToneStep melodyClick[] = {
  {4000, 8},
};

// Descending tones - bed cooled, second-stage alert (softer than finished)
static const ToneStep melodyBedCooldown[] = {
  {784, 150}, {0, 50},   // G5
  {659, 150}, {0, 50},   // E5
  {523, 200},            // C5
};

// ---------------------------------------------------------------------------
//  Non-blocking playback state
// ---------------------------------------------------------------------------
static const ToneStep* currentMelody = nullptr;
static uint8_t melodyLen = 0;
static uint8_t melodyIdx = 0;
static unsigned long stepStartMs = 0;
static bool playing = false;

void initBuzzer() {
  playing = false;
  currentMelody = nullptr;
  melodyIdx = 0;
  melodyLen = 0;
  if (!buzzerSettings.enabled) {
    buzzerBackendShutdown();
    return;
  }
  buzzerBackendInit();
  buzzerBackendStop();
}

bool buzzerIsPlaying() {
  return playing;
}

bool buzzerIsQuietHour() {
  uint8_t qs = buzzerSettings.quietStartHour;
  uint8_t qe = buzzerSettings.quietEndHour;
  if (qs == qe) return false;

  struct tm now;
  if (!getLocalTime(&now, 0)) return false;
  uint8_t h = now.tm_hour;

  if (qs < qe) return h >= qs && h < qe;
  else          return h >= qs || h < qe;
}

void buzzerPlay(BuzzerEvent event) {
  if (!buzzerSettings.enabled) return;
  if (buzzerIsQuietHour()) return;
  if (playing) return;

  switch (event) {
    case BUZZ_PRINT_FINISHED:
      currentMelody = melodyFinished;
      melodyLen = sizeof(melodyFinished) / sizeof(ToneStep);
      break;
    case BUZZ_ERROR:
      currentMelody = melodyError;
      melodyLen = sizeof(melodyError) / sizeof(ToneStep);
      break;
    case BUZZ_CONNECTED:
      currentMelody = melodyConnected;
      melodyLen = sizeof(melodyConnected) / sizeof(ToneStep);
      break;
    case BUZZ_CLICK:
      currentMelody = melodyClick;
      melodyLen = sizeof(melodyClick) / sizeof(ToneStep);
      break;
    case BUZZ_BED_COOLDOWN:
      currentMelody = melodyBedCooldown;
      melodyLen = sizeof(melodyBedCooldown) / sizeof(ToneStep);
      break;
    default: return;
  }

  melodyIdx = 0;
  playing = true;
  buzzerBackendApplyStep(currentMelody[0].freq);
  stepStartMs = millis();
}

void buzzerPlayClick() {
  if (!buzzerSettings.enabled) return;
  if (!buzzerSettings.buttonClick) return;

  bool wasPlaying = playing;
  buzzerBackendApplyStep(melodyClick[0].freq);
  unsigned long t = millis();
  while (millis() - t < melodyClick[0].ms) {
    buzzerBackendTick();
    delay(1);
  }
  buzzerBackendStop();
  if (wasPlaying && currentMelody && melodyIdx < melodyLen) {
    stepStartMs = millis();
    buzzerBackendApplyStep(currentMelody[melodyIdx].freq);
  }
}

void buzzerTick() {
  buzzerBackendTick();  // always tick - handles idle timeout shutdown
  if (!playing || !currentMelody) return;

  if (millis() - stepStartMs < currentMelody[melodyIdx].ms) return;

  melodyIdx++;
  if (melodyIdx >= melodyLen) {
    playing = false;
    currentMelody = nullptr;
    buzzerBackendStop();
    return;
  }

  buzzerBackendApplyStep(currentMelody[melodyIdx].freq);
  stepStartMs = millis();
}
