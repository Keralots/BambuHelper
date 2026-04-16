#include "buzzer.h"
#include "buzzer_backend.h"
#include "settings.h"
#include <time.h>

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
