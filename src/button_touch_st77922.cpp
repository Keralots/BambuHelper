// ST77922 integrated touch backend (lcdwiki_es3c35p). See button_touch_backend.h.
// The touch block sits inside the display controller (I2C 0x55) and is NOT
// FT6x36-compatible: register addresses are 16 bits wide (MSB first) and reads
// use a repeated start. BambuHelper only needs "finger down", so this is a level
// backend: touchPoll() reports whether the controller currently has a valid
// touch record. Register layout (from the vendor's esp_lcd_touch_st77922):
//   0x0010  info byte: bit 3 = "a coordinate report is available"
//   0x0009  max simultaneous touches
//   0x0014  8-byte record per touch, bit 7 of byte 0 = valid
// The whole maxTouches*8 report block MUST be read in one transfer, otherwise the
// controller never refreshes its buffer and keeps reporting a stale touch.
#include "button_touch_backend.h"

#if defined(USE_ST77922_TOUCH)

#include <Wire.h>

#define ST77922_TOUCH_ADDR     0x55
#define ST77922_REG_MAX_TOUCH  0x0009
#define ST77922_REG_INFO       0x0010
#define ST77922_REG_REPORT     0x0014
#define ST77922_INFO_HAS_COORD 0x08
#define ST77922_REPORT_VALID   0x80
#define ST77922_MAX_TOUCHES    10

static bool busReady = false;
static bool seen = false;

static bool stReadReg(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(ST77922_TOUCH_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;  // repeated start, no STOP
  if (Wire.requestFrom((uint8_t)ST77922_TOUCH_ADDR, len) < len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

void touchInit() {
  // The touch half of the IC holds in reset until RST (GPIO48) is pulsed; the
  // display half does not need it.
  pinMode(ST77922_TOUCH_RST, OUTPUT);
  digitalWrite(ST77922_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(ST77922_TOUCH_RST, HIGH);
  delay(50);
  Wire.begin(ST77922_TOUCH_SDA, ST77922_TOUCH_SCL);
  Wire.setClock(400000);
  busReady = true;
  pinMode(ST77922_TOUCH_INT, INPUT_PULLUP);
  uint8_t info = 0;
  if (stReadReg(ST77922_REG_INFO, &info, 1)) {
    Serial.printf("ST77922 touch initialized (I2C SDA=%d SCL=%d INT=%d RST=%d, addr 0x%02X)\n",
                  ST77922_TOUCH_SDA, ST77922_TOUCH_SCL, ST77922_TOUCH_INT,
                  ST77922_TOUCH_RST, ST77922_TOUCH_ADDR);
    seen = true;
  } else {
    Serial.printf("ST77922 touch did not answer at init (addr 0x%02X); will keep retrying at runtime\n",
                  ST77922_TOUCH_ADDR);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  uint8_t info = 0;
  if (!stReadReg(ST77922_REG_INFO, &info, 1)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.println("ST77922 touch became responsive at runtime");
    seen = true;
  }
  if (!(info & ST77922_INFO_HAS_COORD)) return {TouchEvent::None, false};

  uint8_t maxTouches = 0;
  if (!stReadReg(ST77922_REG_MAX_TOUCH, &maxTouches, 1)) return {TouchEvent::Unavailable, false};
  if (maxTouches == 0 || maxTouches > ST77922_MAX_TOUCHES) maxTouches = ST77922_MAX_TOUCHES;
  uint8_t r[ST77922_MAX_TOUCHES * 8];
  if (!stReadReg(ST77922_REG_REPORT, r, (size_t)maxTouches * 8))
    return {TouchEvent::Unavailable, false};
  for (uint8_t i = 0; i < maxTouches; i++) {
    if (r[i * 8] & ST77922_REPORT_VALID) return {TouchEvent::None, true};
  }
  return {TouchEvent::None, false};
}

#endif  // USE_ST77922_TOUCH
