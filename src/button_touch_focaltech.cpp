// FocalTech capacitive touch backend - FT6336 (ws_lcd_350, es3n28p, wt32_sc01_plus)
// and FT5X06 (sensecap_indicator, sc05_x). See button_touch_backend.h.
// Both share the same register map: addr 0x38, TD_STATUS at 0x02 (low nibble =
// number of active touch points). BambuHelper only needs the boolean "finger
// down", so the same code drives both - they differ only in which I2C pins the
// bus is brought up on (handled in touchInit below).
#include "button_touch_backend.h"

#if defined(USE_FT5X06) || defined(USE_FT6336)

#include <Wire.h>

#ifndef TOUCH_SLAVE_ADDRESS
#define TOUCH_SLAVE_ADDRESS 0x38
#endif
#define FT5X06_TOUCH_POINTS_REG 0x02  // TD_STATUS register

static bool busReady = false;
static bool seen = false;

static bool ft5x06Probe() {
  Wire.beginTransmission(TOUCH_SLAVE_ADDRESS);
  return Wire.endTransmission(true) == 0;
}

static bool ft5x06ReadReg(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(TOUCH_SLAVE_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_SLAVE_ADDRESS, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

void touchInit() {
#if defined(USE_FT6336)
#if defined(FT6336_RST)
  // Hardware reset before bringing up I2C. The FT6336 RST line is active-LOW; the
  // vendor demo drives HIGH -> LOW -> HIGH with a long settle. Boards that wire
  // RST to a real GPIO (es3n28p, GPIO18) define FT6336_RST; boards that rely on
  // POR (ws_lcd_350) leave it undefined and skip this.
  pinMode(FT6336_RST, OUTPUT);
  digitalWrite(FT6336_RST, HIGH);
  delay(20);
  digitalWrite(FT6336_RST, LOW);
  delay(20);
  digitalWrite(FT6336_RST, HIGH);
  delay(200);  // controller boot after reset release
#endif
  // ws_lcd_350: FT6336 shares the I2C bus that also carries the TCA9554 LCD-reset
  // expander (SDA=8, SCL=7). Reset is handled by the board POR; the retry-at-
  // runtime path covers any startup delay.
  Wire.begin(FT6336_SDA, FT6336_SCL);
#else
  // SenseCAP: FT5X06 shares the PCA9535PW IO-expander I2C bus (SDA=39, SCL=40).
  // Other boards can define FT5X06_SDA/SCL directly.
#if defined(FT5X06_SDA) && defined(FT5X06_SCL)
  Wire.begin(FT5X06_SDA, FT5X06_SCL);
#else
  Wire.begin(39, 40);
#endif
#endif
  Wire.setClock(400000);
  busReady = true;
  delay(50);  // Wait for touch controller to be ready after RST release
  if (ft5x06Probe()) {
    uint8_t touchPoints = 0;
    if (ft5x06ReadReg(FT5X06_TOUCH_POINTS_REG, touchPoints)) {
      Serial.printf("FocalTech touch initialized (I2C addr 0x%02X, touchPoints=%d)\n",
                    TOUCH_SLAVE_ADDRESS, touchPoints);
      seen = true;
    } else {
      Serial.printf("FocalTech touch detected on I2C addr 0x%02X, but register reads failed\n",
                    TOUCH_SLAVE_ADDRESS);
    }
  } else {
    Serial.printf("FocalTech touch did not answer at init (addr 0x%02X); will keep retrying at runtime\n",
                  TOUCH_SLAVE_ADDRESS);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  uint8_t touchPoints = 0;
  if (!ft5x06ReadReg(FT5X06_TOUCH_POINTS_REG, touchPoints)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.printf("FocalTech touch became responsive at runtime (addr 0x%02X)\n", TOUCH_SLAVE_ADDRESS);
    seen = true;
  }
  // TD_STATUS low nibble = active touch points; mask off the reserved high bits so
  // a stray high bit can't be misread as a permanent touch.
  return {TouchEvent::None, (bool)((touchPoints & 0x0F) > 0)};
}

#endif  // USE_FT5X06 || USE_FT6336
