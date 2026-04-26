#pragma once

#include <stdint.h>

namespace Battery {

#if defined(BOARD_HAS_BATTERY)

void begin();
void tick();
bool isPresent();
uint8_t percent();
float voltage();
bool isCharging();
bool isLow();
bool isCritical();

#else

inline void begin() {}
inline void tick() {}
inline bool isPresent() { return false; }
inline uint8_t percent() { return 0; }
inline float voltage() { return 0.0f; }
inline bool isCharging() { return false; }
inline bool isLow() { return false; }
inline bool isCritical() { return false; }

#endif

}
