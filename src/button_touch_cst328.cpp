// CST328 capacitive touch backend (ws_lcd_280). See button_touch_backend.h.
// Differences vs CST816:
//   - I2C address 0x1A
//   - 16-bit register addresses (high byte first on the wire)
//   - Touch report block starts at register 0xD000:
//       byte 0 : finger0 state (0x06 = finger down)
//     Reading just byte 0 is enough for tap detection.
#include "button_touch_backend.h"

#if defined(USE_CST328)

#include <Wire.h>

#define CST328_ADDR              0x1A
#define CST328_REG_TOUCH_INFO_H  0xD0
#define CST328_REG_TOUCH_INFO_L  0x00
#define CST328_FINGER_DOWN_STATE 0x06

static bool busReady = false;
static bool seen = false;

static bool cst328Probe() {
  Wire.beginTransmission(CST328_ADDR);
  return Wire.endTransmission(true) == 0;
}

// Reads the finger0 state byte from 0xD000. Non-zero == finger present; 0x06 is
// the canonical "touch active" value per Hynitron CST328 datasheet.
static bool cst328ReadFingerState(uint8_t& value) {
  Wire.beginTransmission(CST328_ADDR);
  Wire.write(CST328_REG_TOUCH_INFO_H);
  Wire.write(CST328_REG_TOUCH_INFO_L);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST328_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

void touchInit() {
#ifdef CST328_RST
  pinMode(CST328_RST, OUTPUT);
  digitalWrite(CST328_RST, LOW);
  delay(20);
  digitalWrite(CST328_RST, HIGH);
  delay(100);  // CST328 needs ~100ms to boot after reset
#endif
  Wire.begin(CST328_SDA, CST328_SCL);
  Wire.setClock(400000);
  busReady = true;
  if (cst328Probe()) {
    uint8_t fingerState = 0;
    if (cst328ReadFingerState(fingerState)) {
      Serial.printf("CST328 touch initialized (I2C SDA=%d SCL=%d IRQ=%d, reg0xD000=0x%02X)\n",
                    CST328_SDA, CST328_SCL,
#ifdef CST328_IRQ
                    CST328_IRQ,
#else
                    -1,
#endif
                    fingerState);
      seen = true;
    } else {
      Serial.printf("CST328 detected on I2C addr 0x%02X, but register reads failed (SDA=%d SCL=%d)\n",
                    CST328_ADDR, CST328_SDA, CST328_SCL);
    }
  } else {
    Serial.printf("CST328 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                  CST328_ADDR, CST328_SDA, CST328_SCL);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  uint8_t fingerState = 0;
  if (!cst328ReadFingerState(fingerState)) return {TouchEvent::Unavailable, false};
  if (!seen) {
    Serial.printf("CST328 touch became responsive at runtime (addr 0x%02X)\n", CST328_ADDR);
    seen = true;
  }
  // Byte at 0xD000 packs finger ID (high nibble) + state (low nibble). Per
  // Hynitron datasheet / CSE_CST328 reference, low nibble == 0x06 is "finger
  // present". Checking the full byte is wrong because a lifted finger can still
  // report a non-zero ID.
  return {TouchEvent::None, (bool)((fingerState & 0x0F) == CST328_FINGER_DOWN_STATE)};
}

#endif  // USE_CST328
