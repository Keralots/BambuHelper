#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

void initButton();
bool wasButtonPressed();  // returns true once per press (edge-detected, debounced)
void sanitizeButtonPin();  // zero buttonPin if it conflicts with a reserved
                           // subsystem (backlight, touch bus, buzzer). No-op
                           // for touchscreen type.

// Hold-state polling. Pure getters - they reflect whatever the most recent
// wasButtonPressed() call observed and do NOT consume edge events. The main
// loop calls wasButtonPressed() once per iteration, so these stay fresh.
bool isButtonHeld();              // post-debounce stable pressed state
uint32_t buttonHoldDurationMs();  // 0 if not held, else millis() - press start

#endif // BUTTON_H
