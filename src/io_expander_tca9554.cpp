#include "io_expander_tca9554.h"

#if PANEL_HAS_IO_EXPANDER

#include <Arduino.h>
#include <Wire.h>

// TCA9554 register map
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;   // 1 = input, 0 = output

static uint8_t s_addr    = 0;
static uint8_t s_shadow  = 0;
static bool    s_present = false;
static bool    s_begun   = false;

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool ioExpanderBegin(uint8_t addr, int8_t sda, int8_t scl, uint32_t freqHz,
                     uint8_t initialOut) {
  if (s_begun) return s_present;
  s_begun  = true;
  s_addr   = addr;
  s_shadow = initialOut;

  Wire.begin(sda, scl, freqHz);
  // Latch the outputs before switching the pins over, so no bit glitches to
  // the chip's power-on state (all high) on the way.
  bool ok = writeReg(REG_OUTPUT, s_shadow);
  ok = writeReg(REG_CONFIG, 0x00) && ok;
  s_present = ok;
  if (!ok) {
    Serial.printf("IOEXP: TCA9554 @0x%02X not responding\n", addr);
  }
  return ok;
}

void ioExpanderSet(uint8_t bit, bool high) {
  if (!s_present || bit > 7) return;
  uint8_t next = high ? (uint8_t)(s_shadow | (1 << bit))
                      : (uint8_t)(s_shadow & ~(1 << bit));
  if (next == s_shadow) return;
  s_shadow = next;
  writeReg(REG_OUTPUT, s_shadow);
}

uint8_t ioExpanderOutputs() { return s_shadow; }

bool ioExpanderPresent() { return s_present; }

#endif // PANEL_HAS_IO_EXPANDER
