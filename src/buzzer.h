#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

enum BuzzerEvent : uint8_t {
  BUZZ_PRINT_FINISHED = 0,
  BUZZ_ERROR          = 1,
  BUZZ_CONNECTED      = 2,
  BUZZ_CLICK          = 3,
  BUZZ_BED_COOLDOWN   = 4,
};

void initBuzzer();
void buzzerPlay(BuzzerEvent event);
void buzzerPlayClick();  // short click, ignores quiet hours and playing state
void buzzerTick();  // call from loop() for non-blocking playback
bool buzzerIsQuietHour();
bool buzzerIsPlaying();
void sanitizeBuzzerPin();  // zero pin in RAM if it conflicts with BACKLIGHT_PIN

#endif // BUZZER_H
