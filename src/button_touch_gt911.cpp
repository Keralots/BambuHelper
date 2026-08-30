// GT911 capacitive touch backend (ws_lcd_28c). See button_touch_backend.h.
// Differences vs CST328:
//   - I2C address is latched at reset from the INT level: low = 0x5D, high = 0x14
//   - status register 0x814E: bit7 = data ready, low nibble = point count
//   - the status byte MUST be written back as 0 after every read, or the
//     controller never raises another frame
//
// The reset + INT-low latch does NOT happen here. It runs in initDisplay()
// (src/display_ui.cpp), because the touch reset line is a TCA9554 expander bit
// that the display bring-up already owns, and touchInit() only runs from
// handleSplashPhase() about two seconds later - far too late to influence the
// address. Do not "clean that up" into this file.
#include "button_touch_backend.h"

#if defined(USE_GT911)

#include <Wire.h>

// Primary address (INT held low through reset). 0x14 is the INT-high variant;
// probed as a fallback so a board that latched the other way still works.
#define GT911_ADDR_LOW    0x5D
#define GT911_ADDR_HIGH   0x14
#define GT911_REG_STATUS  0x814E
#define GT911_REG_PRODUCT 0x8140
#define GT911_MAX_POINTS  5

static uint8_t addr     = GT911_ADDR_LOW;
static bool    busReady = false;
static bool    seen     = false;

// GT911 register addresses are 16-bit, MSB first (CST816/CST328 use 8-bit).
static bool gt911Read(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool gt911Write(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

static bool gt911Probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission(true) == 0;
}

void touchInit() {
  // The display bring-up already pulsed reset with INT held low and released
  // INT as an input. Re-assert the input mode defensively - nothing else should
  // be driving GPIO16, and a floating INT makes the first read look dead.
  pinMode(GT911_INT, INPUT);

  Wire.begin(GT911_SDA, GT911_SCL);
  Wire.setClock(400000);
  busReady = true;

  // One-shot bus scan: the address depends on a reset-time pin level, so a
  // wrong answer here is the first thing to look at on new hardware.
  Serial.print("GT911: I2C scan ->");
  uint8_t found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (gt911Probe(a)) { Serial.printf(" 0x%02X", a); found++; }
  }
  if (!found) Serial.print(" (nothing)");
  Serial.println();

  if (!gt911Probe(addr) && gt911Probe(GT911_ADDR_HIGH)) {
    addr = GT911_ADDR_HIGH;
    Serial.println("GT911: reset latched the INT-high address");
  }

  uint8_t id[4] = {0};
  if (gt911Probe(addr) && gt911Read(GT911_REG_PRODUCT, id, 4)) {
    Serial.printf("GT911 touch initialized (addr 0x%02X, SDA=%d SCL=%d INT=%d, id %c%c%c%c)\n",
                  addr, GT911_SDA, GT911_SCL, GT911_INT,
                  id[0] ? id[0] : '?', id[1] ? id[1] : '?',
                  id[2] ? id[2] : '?', id[3] ? id[3] : '?');
    seen = true;
    gt911Write(GT911_REG_STATUS, 0);   // start from a clean status byte
  } else {
    Serial.printf("GT911 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                  addr, GT911_SDA, GT911_SCL);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};

  uint8_t status = 0;
  if (!gt911Read(GT911_REG_STATUS, &status, 1)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.printf("GT911 touch became responsive at runtime (addr 0x%02X)\n", addr);
    seen = true;
  }

  // bit7 clear = the controller has not published a new frame; the last known
  // level stands, so report "not down" rather than an error.
  if ((status & 0x80) == 0) {
    gt911Write(GT911_REG_STATUS, 0);
    return {TouchEvent::None, false};
  }

  const uint8_t points = status & 0x0F;
  // Always acknowledge, whatever the count - skipping this wedges the
  // controller on its current frame and touch dies after one tap.
  gt911Write(GT911_REG_STATUS, 0);
  return {TouchEvent::None, (bool)(points > 0 && points <= GT911_MAX_POINTS)};
}

#endif  // USE_GT911
