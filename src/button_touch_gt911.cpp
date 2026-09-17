// GT911 capacitive touch backend (Guition JC4827W543 "C" variant).
// See button_touch_backend.h for the interface. This is a LEVEL backend: each
// poll reads the GT911 status register (0x814E), reports finger-down when the
// touch-point count is non-zero, and clears the buffer-ready flag so the chip
// keeps updating. button.cpp applies its shared debounce/hold logic on top.
//
// I2C address: the GT911 comes up at 0x5D or 0x14 depending on the INT-line
// level during reset. We drive the RST/INT sequence for 0x5D, then probe both
// addresses so a board that strapped it differently still works.
#include "button_touch_backend.h"

#if defined(USE_GT911)

#include <Wire.h>

#ifndef GT911_SDA
#define GT911_SDA 8
#endif
#ifndef GT911_SCL
#define GT911_SCL 4
#endif
#ifndef GT911_INT
#define GT911_INT 3
#endif
#ifndef GT911_RST
#define GT911_RST 38
#endif

#define GT911_REG_STATUS 0x814E
#define GT911_REG_PID    0x8140

static uint8_t gtAddr = 0x5D;
static bool busReady = false;
static bool seen = false;

static bool gtWriteReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static int gtReadReg(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return -1;
  uint8_t got = Wire.requestFrom((int)gtAddr, (int)len);
  uint8_t i = 0;
  while (Wire.available() && i < len) buf[i++] = Wire.read();
  return (got == len && i == len) ? (int)len : -1;
}

static bool gtProbe(uint8_t addr) {
  gtAddr = addr;
  uint8_t pid[4] = {0};
  if (gtReadReg(GT911_REG_PID, pid, 4) != 4) return false;
  // Product ID is ASCII "911" followed by a NUL on genuine parts.
  return pid[0] == '9' && pid[1] == '1' && pid[2] == '1';
}

void touchInit() {
  // Reset sequence that selects address 0x5D: INT low during RST release.
  pinMode(GT911_INT, OUTPUT);
  pinMode(GT911_RST, OUTPUT);
  digitalWrite(GT911_INT, LOW);
  digitalWrite(GT911_RST, LOW);
  delay(10);
  digitalWrite(GT911_RST, HIGH);
  delay(10);
  pinMode(GT911_INT, INPUT);   // release INT; chip drives it from here on
  delay(60);

  Wire.begin(GT911_SDA, GT911_SCL);
  Wire.setClock(400000);
  busReady = true;

  if (gtProbe(0x5D) || gtProbe(0x14)) {
    seen = true;
    Serial.printf("Touch: GT911 found at 0x%02X (SDA=%d SCL=%d INT=%d RST=%d)\n",
                  gtAddr, GT911_SDA, GT911_SCL, GT911_INT, GT911_RST);
  } else {
    seen = false;
    Serial.println("Touch: GT911 not detected on 0x5D/0x14 - touchscreen disabled");
  }
}

TouchPoll touchPoll() {
  if (!busReady || !seen) return {TouchEvent::Unavailable, false};
  uint8_t st = 0;
  if (gtReadReg(GT911_REG_STATUS, &st, 1) != 1) return {TouchEvent::Unavailable, false};
  // Bit 7 = buffer status (new data), bits 3:0 = number of touch points.
  if (st & 0x80) {
    gtWriteReg(GT911_REG_STATUS, 0);   // acknowledge so the chip refreshes
  }
  return {TouchEvent::None, (st & 0x0F) != 0};
}

#endif  // USE_GT911
