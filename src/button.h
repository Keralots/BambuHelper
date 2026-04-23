#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

void initButton();
bool wasButtonPressed();  // returns true once per press (edge-detected, debounced)
void sanitizeButtonPin();  // zero buttonPin if it conflicts with a reserved
                           // subsystem (backlight, touch bus, buzzer). No-op
                           // for touchscreen type.

#endif // BUTTON_H
