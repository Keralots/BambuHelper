#include "io_expander_tca9554.h"

#if PANEL_HAS_IO_EXPANDER

#include <Arduino.h>
#include <Wire.h>

// TCA9554 register map
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;   // 1 = input, 0 = output

static uint8_t  s_addr      = 0;
static uint8_t  s_shadow    = 0;
static bool     s_present   = false;
static bool     s_begun     = false;
static uint32_t s_lastTryMs = 0;

static const uint32_t RETRY_MS = 200;    // bring-up retry throttle - short
                                        // enough that initDisplay()'s reset
                                        // sequence gets several attempts

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Latch the outputs before switching the pins over, so no bit glitches to the
// chip's power-on state (all high) on the way. Also the recovery path: one NACK
// must not disable the expander for the rest of the session - it carries the
// LCD reset / CS and the buzzer. Throttled, so an absent chip cannot spam the
// bus on every call.
static bool bringUp() {
  uint32_t now = millis();
  if (s_lastTryMs != 0 && (now - s_lastTryMs) < RETRY_MS) return false;
  s_lastTryMs = now ? now : 1;

  bool ok = writeReg(REG_OUTPUT, s_shadow);
  ok = writeReg(REG_CONFIG, 0x00) && ok;
  s_present = ok;
  return ok;
}

bool ioExpanderBegin(uint8_t addr, int8_t sda, int8_t scl, uint32_t freqHz,
                     uint8_t initialOut) {
  if (s_begun) return s_present;
  s_begun  = true;
  s_addr   = addr;
  s_shadow = initialOut;

  Wire.begin(sda, scl, freqHz);
  if (!bringUp()) {
    Serial.printf("IOEXP: TCA9554 @0x%02X not responding, will retry on use\n", addr);
  }
  return s_present;
}

void ioExpanderSet(uint8_t bit, bool high) {
  if (!s_begun || bit > 7) return;
  uint8_t next = high ? (uint8_t)(s_shadow | (1 << bit))
                      : (uint8_t)(s_shadow & ~(1 << bit));
  bool changed = (next != s_shadow);
  s_shadow = next;

  if (!s_present) {
    // Retry the bring-up instead of dropping the write: on success it has
    // already latched s_shadow, this bit included. Until then the shadow
    // carries the intent.
    bringUp();
    return;
  }
  if (!changed) return;
  // A failed write falls back to the retry path above rather than being lost.
  if (!writeReg(REG_OUTPUT, s_shadow)) s_present = false;
}

uint8_t ioExpanderOutputs() { return s_shadow; }

bool ioExpanderPresent() { return s_present; }

#endif // PANEL_HAS_IO_EXPANDER
