// Hynitron CST9217 capacitive touch backend (ws_amoled_175). See
// button_touch_backend.h. Report protocol from the vendor driver
// (SensorLib TouchDrvCST92xx::getPoint), which Waveshare's demos use:
//   1. write the 16-bit register 0xD000, read a 15-byte report
//      (2 fingers * 5 + 5)
//   2. write [0xD0, 0x00, 0xAB] to acknowledge, so the next frame is published
//   3. a frame is valid when report[6] == 0xAB and the low status nibble of
//      report[0] is 0x06; the finger count is report[5] & 0x7F
// Only "finger down" is needed, so the X/Y parse is skipped.
#include "button_touch_backend.h"

#if defined(USE_CST92XX)

#include <Wire.h>

#ifndef CST92XX_ADDR
#define CST92XX_ADDR 0x5A
#endif
#define CST92XX_ACK         0xAB
#define CST92XX_REPORT_LEN  15
#define CST92XX_MAX_FINGERS 2

static bool busReady = false;
static bool seen     = false;
// Between report frames the controller hands back a frame without the ACK
// byte. The shared debouncer expects a level, so the last valid frame's level
// stands until the next one (same reasoning as the GT911 backend), with a stale
// guard so a vanished controller can never latch a permanent hold.
static bool     lastDown    = false;
static uint32_t lastFrameMs = 0;
static const uint32_t DOWN_STALE_MS = 1000;

static bool cst92xxProbe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission(true) == 0;
}

// Returns false on an I2C failure. `valid` says whether a fresh frame arrived.
static bool cst92xxRead(bool& valid, bool& down) {
  uint8_t rb[CST92XX_REPORT_LEN] = {0};
  Wire.beginTransmission(CST92XX_ADDR);
  Wire.write(0xD0);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST92XX_ADDR, (uint8_t)CST92XX_REPORT_LEN) != CST92XX_REPORT_LEN)
    return false;
  for (uint8_t i = 0; i < CST92XX_REPORT_LEN; i++) rb[i] = Wire.read();

  Wire.beginTransmission(CST92XX_ADDR);
  Wire.write(0xD0);
  Wire.write(0x00);
  Wire.write(CST92XX_ACK);
  Wire.endTransmission(true);

  valid = (rb[6] == CST92XX_ACK);
  if (!valid) return true;
  const uint8_t fingers = rb[5] & 0x7F;
  down = fingers > 0 && fingers <= CST92XX_MAX_FINGERS && (rb[0] & 0x0F) == 0x06;
  return true;
}

void touchInit() {
  lastDown    = false;
  lastFrameMs = 0;
#ifdef CST92XX_RST
  pinMode(CST92XX_RST, OUTPUT);
  digitalWrite(CST92XX_RST, LOW);
  delay(20);
  digitalWrite(CST92XX_RST, HIGH);
  delay(100);   // controller boot after reset
#endif
#ifdef CST92XX_INT
  pinMode(CST92XX_INT, INPUT_PULLUP);
#endif
  Wire.begin(CST92XX_SDA, CST92XX_SCL);   // shared with AXP2101 / ES8311
  Wire.setClock(400000);
  busReady = true;

  // One-shot bus scan - the first thing to look at on untested hardware.
  Serial.print("CST92xx: I2C scan ->");
  uint8_t found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (cst92xxProbe(a)) { Serial.printf(" 0x%02X", a); found++; }
  }
  if (!found) Serial.print(" (nothing)");
  Serial.println();

  if (cst92xxProbe(CST92XX_ADDR)) {
    seen = true;
    Serial.printf("CST92xx touch found (addr 0x%02X, SDA=%d SCL=%d)\n",
                  CST92XX_ADDR, CST92XX_SDA, CST92XX_SCL);
  } else {
    Serial.printf("CST92xx touch did not answer at init (addr 0x%02X); will keep retrying at runtime\n",
                  CST92XX_ADDR);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  bool valid = false, down = false;
  if (!cst92xxRead(valid, down)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.printf("CST92xx touch became responsive at runtime (addr 0x%02X)\n", CST92XX_ADDR);
    seen = true;
  }
  if (!valid) {
    if (lastDown && (millis() - lastFrameMs) > DOWN_STALE_MS) lastDown = false;
    return {TouchEvent::None, lastDown};
  }
  lastDown    = down;
  lastFrameMs = millis();
  return {TouchEvent::None, lastDown};
}

#endif  // USE_CST92XX
