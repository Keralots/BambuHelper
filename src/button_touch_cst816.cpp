// CST816 capacitive touch backend (ws_lcd_200, ws_lcd_154). See button_touch_backend.h.
#include "button_touch_backend.h"

#if defined(USE_CST816)

#include <Wire.h>

#define CST816_ADDR          0x15
#define CST816_TOUCH_NUM_REG 0x02

static bool busReady = false;
static bool seen = false;

static bool cst816Probe() {
  Wire.beginTransmission(CST816_ADDR);
  return Wire.endTransmission(true) == 0;
}

static bool cst816ReadReg(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

void touchInit() {
#ifdef CST816_RST
  // Hardware reset - required for CST816 to respond on I2C
  pinMode(CST816_RST, OUTPUT);
  digitalWrite(CST816_RST, LOW);
  delay(20);
  digitalWrite(CST816_RST, HIGH);
  delay(50);  // wait for controller to boot after reset
#endif
  Wire.begin(CST816_SDA, CST816_SCL);
  Wire.setClock(400000);
  busReady = true;
  if (cst816Probe()) {
    uint8_t touchNum = 0;
    if (cst816ReadReg(CST816_TOUCH_NUM_REG, touchNum)) {
      Serial.printf("CST816 touch initialized (I2C SDA=%d SCL=%d, reg0x%02X=0x%02X)\n",
                    CST816_SDA, CST816_SCL, CST816_TOUCH_NUM_REG, touchNum);
      seen = true;
    } else {
      Serial.printf("CST816 detected on I2C addr 0x%02X, but register reads failed (SDA=%d SCL=%d)\n",
                    CST816_ADDR, CST816_SDA, CST816_SCL);
    }
  } else {
    Serial.printf("CST816 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                  CST816_ADDR, CST816_SDA, CST816_SCL);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  uint8_t touchNum = 0;
  if (!cst816ReadReg(CST816_TOUCH_NUM_REG, touchNum)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.printf("CST816 touch became responsive at runtime (addr 0x%02X)\n", CST816_ADDR);
    seen = true;
  }
  return {TouchEvent::None, (bool)(touchNum > 0)};
}

#endif  // USE_CST816
