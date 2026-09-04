// TCA9554 IO-expander buzzer backend (ws_lcd_28c). See buzzer_backend.h.
//
// The buzzer sits on one output bit of the display's IO expander, so it is an
// on/off line, not a tone pin. Frequency and duration are data in buzzer.cpp
// and the backend is called once per step transition, so mapping any nonzero
// frequency to "on" keeps every pause and duration and loses only pitch -
// melodies become rhythm. Do not try to bit-bang a tone over I2C: a 400 kHz
// round trip is ~60 us, so the ceiling is a few hundred Hz of heavy jitter.
//
// Every write goes through the shared shadow in io_expander_tca9554.cpp. The
// output register is written as a whole byte, and the other bits are LCD reset,
// LCD CS, touch reset and SD CS - a raw write here blanks the panel.
#include "buzzer_backend.h"
#include "config.h"

#if BUZZER_BACKEND_TCA9554

#include "io_expander_tca9554.h"

static void set(bool on) {
  // initBuzzer() calls buzzerBackendShutdown() at startup even when the buzzer
  // is disabled, which can land here before initDisplay() has claimed the bus.
  // ioExpanderSet() is a no-op until then, and the idle byte already has the
  // buzzer bit low, so there is nothing to do.
  ioExpanderSet(WS28C_EXP_BUZZER, on);
}

void buzzerBackendInit() { set(false); }

void buzzerBackendApplyStep(uint16_t freq) { set(freq != 0); }

void buzzerBackendStop() { set(false); }

void buzzerBackendTick() {}

void buzzerBackendShutdown() { set(false); }

#endif  // BUZZER_BACKEND_TCA9554
