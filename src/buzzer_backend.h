#ifndef BUZZER_BACKEND_H
#define BUZZER_BACKEND_H

#include <Arduino.h>

void buzzerBackendInit();
void buzzerBackendApplyStep(uint16_t freq);
void buzzerBackendStop();
void buzzerBackendTick();
void buzzerBackendShutdown();

#endif // BUZZER_BACKEND_H
