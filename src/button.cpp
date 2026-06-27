#include "button.h"
#include "settings.h"
#include "buzzer.h"

#if defined(USE_XPT2046)
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>
  static SPIClass touchSPI(HSPI);
  static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
  static bool touchReady = false;
  // Phantom-touch tolerance for resistive XPT2046 panels - CYD and TZT
  // L1435-2.4 (issue #109). A pinched touch flex can squeeze the resistive
  // layers together, so the XPT2046 reports a constant weak "touch" from
  // power-on. Left unfiltered that ramps and saves the LED brightness to max
  // (hold-to-dim) within ~2 s and blocks screensaver wake (no fresh press edge
  // ever arrives). Both guards apply to every XPT2046 board (resistive only);
  // capacitive panels report clean release between taps and need neither.
  //
  // Guard 1 - raise the pressure bar. PaulStoffregen's Z_THRESHOLD is 300; the
  //   pre-LovyanGFX TFT_eSPI build ran these same panels at 600, so match that
  //   long-tested value to reject the weakest phantom contacts.
  #ifndef XPT_TOUCH_Z_THRESHOLD
    #define XPT_TOUCH_Z_THRESHOLD 600
  #endif
  // Guard 2 - boot release-gate. A real touch always starts from a released
  //   panel; a stuck phantom is asserted from the first read and never lets
  //   go. So ignore all contact until at least one genuine release (no-touch
  //   read) has been observed since boot. A solid-on phantom is then rejected
  //   immediately - the LED never ramps, so it cannot be saved to max, and
  //   wake resumes the instant a real tap+release occurs. Honest limit: a
  //   phantom that *flickers* below threshold produces a release and re-arms
  //   touch; that is a wiring short with no robust software fix (see #109).
#elif defined(USE_CST816)
  #include <Wire.h>
  #define CST816_ADDR          0x15
  #define CST816_TOUCH_NUM_REG 0x02
  static bool cst816BusReady = false;
  static bool cst816Seen = false;

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
#elif defined(USE_CST328)
  // CST328 (Waveshare 2.8" board). Differences vs CST816:
  //   - I2C address 0x1A
  //   - 16-bit register addresses (high byte first on the wire)
  //   - Touch report block starts at register 0xD000:
  //       byte 0  : finger0 state  (0x06 = finger down)
  //       byte 5  : reported finger count
  //     Reading just byte 0 is enough for tap detection.
  #include <Wire.h>
  #define CST328_ADDR              0x1A
  #define CST328_REG_TOUCH_INFO_H  0xD0
  #define CST328_REG_TOUCH_INFO_L  0x00
  #define CST328_FINGER_DOWN_STATE 0x06
  static bool cst328BusReady = false;
  static bool cst328Seen = false;

  static bool cst328Probe() {
    Wire.beginTransmission(CST328_ADDR);
    return Wire.endTransmission(true) == 0;
  }

  // Reads the finger0 state byte from 0xD000. Non-zero == finger present;
  // 0x06 is the canonical "touch active" value per Hynitron CST328 datasheet.
  static bool cst328ReadFingerState(uint8_t& value) {
    Wire.beginTransmission(CST328_ADDR);
    Wire.write(CST328_REG_TOUCH_INFO_H);
    Wire.write(CST328_REG_TOUCH_INFO_L);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)CST328_ADDR, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
  }
#elif defined(USE_FT5X06) || defined(USE_FT6336)
  // FocalTech capacitive touch family. FT6336 (ws_lcd_350) and FT5X06
  // (SenseCAP) share the same register map: addr 0x38, TD_STATUS at 0x02
  // (low nibble = number of active touch points). BambuHelper only needs the
  // boolean "finger down", so the same code drives both - they differ only in
  // which I2C pins the bus is brought up on (handled in initButton below).
  #include <Wire.h>
  #ifndef TOUCH_SLAVE_ADDRESS
    #define TOUCH_SLAVE_ADDRESS 0x38
  #endif
  #define FT5X06_TOUCH_POINTS_REG 0x02  // TD_STATUS register
  static bool ft5x06BusReady = false;
  static bool ft5x06Seen = false;

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
#elif defined(USE_AXS_TOUCH)
  // AXS15231B integrated touch controller. I2C slave at 0x3B.
  // Protocol (per axs15231b-lovyangfx skill): write 8-byte command, read 8
  // bytes back. Touch is active when rx[0] == 0 (no gesture) AND rx[1] != 0
  // (finger count > 0). Coordinates arrive pre-scaled to panel resolution,
  // NOT raw 0-4095. Single-touch only.
  //
  // INT line: per manufacturer demo code, the AXS15231B touch INT is on
  // GPIO 3, active-low, asserted while a finger is on the panel. Polling
  // the I2C state every main-loop tick misses sub-loop-rate taps (the
  // chip only reports a finger for a brief window if you release fast),
  // so we use the INT pin as the edge trigger and only do the I2C read
  // when the level is low.
  #include <Wire.h>
  #define AXS_TOUCH_ADDR 0x3B
  #ifndef AXS_TOUCH_INT
    #define AXS_TOUCH_INT 3
  #endif
  static bool axsTouchBusReady = false;
  static bool axsTouchSeen = false;
  static volatile uint32_t axsIntFallingCount = 0;  // incremented by the ISR
  static uint32_t axsIntFallingSeen = 0;            // last value drained by poller

  static void IRAM_ATTR axsTouchIsr() {
    axsIntFallingCount++;
  }

  static bool axsTouchProbe() {
    Wire.beginTransmission(AXS_TOUCH_ADDR);
    return Wire.endTransmission(true) == 0;
  }

  // Note: a per-tap I2C read of the touchpad register was removed once
  // we switched to the INT-pin ISR — the firmware only needs "finger
  // down NOW", and the INT line carries that. If a future gesture
  // (multi-touch, drag, coords) is needed, write 8 bytes to 0x3B
  // {0xB5,0xAB,0xA5,0x5A,0,0,0,8} then read 8: rx[0]=gesture,
  // rx[1]=finger count, rx[2..5]=x_h,x_l,y_h,y_l, coords are
  // panel-scaled (NOT raw 0-4095).
#elif defined(TOUCH_CS)
  #include "display_ui.h"  // extern tft for getTouch()
#endif

static bool lastRaw = false;
static bool stableState = false;
static unsigned long lastChangeMs = 0;
static unsigned long pressStartMs = 0;
static const unsigned long DEBOUNCE_MS = 50;

void sanitizeButtonPin() {
  // Only the GPIO-backed button types use buttonPin. Touchscreen talks over
  // a bus defined elsewhere and has no single pin to conflict.
  if (buttonType != BTN_PUSH && buttonType != BTN_TOUCH) return;
  if (buttonPin == 0) return;

  auto clash = [&](const char* what) {
    Serial.printf("Button: pin %u conflicts with %s, disabling\n",
                  (unsigned)buttonPin, what);
    buttonPin = 0;
  };

#if defined(BACKLIGHT_PIN) && BACKLIGHT_PIN >= 0
  if (buttonPin == BACKLIGHT_PIN) { clash("backlight"); return; }
#endif
#if defined(USE_AXS_TOUCH)
  if (buttonPin == AXS_TOUCH_SDA) { clash("AXS touch SDA"); return; }
  if (buttonPin == AXS_TOUCH_SCL) { clash("AXS touch SCL"); return; }
  if (buttonPin == AXS_TOUCH_INT) { clash("AXS touch INT"); return; }
#endif
#if defined(USE_FT6336)
  if (buttonPin == FT6336_SDA) { clash("FT6336 touch SDA"); return; }
  if (buttonPin == FT6336_SCL) { clash("FT6336 touch SCL"); return; }
#endif
#if defined(BOARD_IS_WS350)
  // Display SPI lines - driving any as a button GPIO disturbs the panel bus.
  // (Backlight 6 and I2C 7/8 are already covered above / by FT6336 checks.)
  if (buttonPin == 1) { clash("WS350 display MOSI"); return; }
  if (buttonPin == 2) { clash("WS350 display MISO"); return; }
  if (buttonPin == 3) { clash("WS350 display DC");   return; }
  if (buttonPin == 5) { clash("WS350 display SCLK"); return; }
#endif
#if defined(BOARD_IS_SC01PLUS)
  // ST7796 8-bit 8080 parallel bus + control - driving any as a button GPIO
  // disturbs the panel. (Backlight 45 and touch SDA/SCL 6/5 are already covered
  // above / by FT6336 checks.)
  if (buttonPin == 0)  { clash("SC01PLUS LCD_RS/DC");  return; }
  if (buttonPin == 3 || buttonPin == 8 || buttonPin == 9 ||
      buttonPin == 15 || buttonPin == 16 || buttonPin == 17 ||
      buttonPin == 18 || buttonPin == 46) { clash("SC01PLUS LCD data bus"); return; }
  if (buttonPin == 47) { clash("SC01PLUS LCD_WR");     return; }
  if (buttonPin == 4)  { clash("SC01PLUS LCD/touch RST"); return; }
  if (buttonPin == 7)  { clash("SC01PLUS touch INT");  return; }
  if (buttonPin == 48) { clash("SC01PLUS LCD_TE");     return; }
#endif
#if defined(USE_CST816)
  if (buttonPin == CST816_SDA) { clash("CST816 touch SDA"); return; }
  if (buttonPin == CST816_SCL) { clash("CST816 touch SCL"); return; }
  #if defined(CST816_IRQ)
  if (buttonPin == CST816_IRQ) { clash("CST816 touch IRQ"); return; }
  #endif
  #if defined(CST816_RST)
  if (buttonPin == CST816_RST) { clash("CST816 touch RST"); return; }
  #endif
#endif
#if defined(USE_CST328)
  if (buttonPin == CST328_SDA) { clash("CST328 touch SDA"); return; }
  if (buttonPin == CST328_SCL) { clash("CST328 touch SCL"); return; }
  #if defined(CST328_IRQ)
  if (buttonPin == CST328_IRQ) { clash("CST328 touch IRQ"); return; }
  #endif
  #if defined(CST328_RST)
  if (buttonPin == CST328_RST) { clash("CST328 touch RST"); return; }
  #endif
#endif
#if defined(USE_XPT2046)
  if (buttonPin == TOUCH_CS)   { clash("XPT2046 CS");   return; }
  if (buttonPin == TOUCH_IRQ)  { clash("XPT2046 IRQ");  return; }
  if (buttonPin == TOUCH_MOSI) { clash("XPT2046 MOSI"); return; }
  if (buttonPin == TOUCH_MISO) { clash("XPT2046 MISO"); return; }
  if (buttonPin == TOUCH_CLK)  { clash("XPT2046 CLK");  return; }
#endif
  if (buzzerSettings.pin != 0 && buttonPin == buzzerSettings.pin) {
    clash("buzzer"); return;
  }
}

void initButton() {
  if (buttonType == BTN_DISABLED) return;
  sanitizeButtonPin();
#if defined(USE_XPT2046)
  if (buttonType == BTN_TOUCHSCREEN) {
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    touchReady = true;
    Serial.println("XPT2046 touch initialized (separate SPI)");
    return;
  }
#elif defined(USE_CST816)
  if (buttonType == BTN_TOUCHSCREEN) {
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
    cst816BusReady = true;
    if (cst816Probe()) {
      uint8_t touchNum = 0;
      if (cst816ReadReg(CST816_TOUCH_NUM_REG, touchNum)) {
        Serial.printf("CST816 touch initialized (I2C SDA=%d SCL=%d, reg0x%02X=0x%02X)\n",
                      CST816_SDA, CST816_SCL, CST816_TOUCH_NUM_REG, touchNum);
        cst816Seen = true;
      } else {
        Serial.printf("CST816 detected on I2C addr 0x%02X, but register reads failed (SDA=%d SCL=%d)\n",
                      CST816_ADDR, CST816_SDA, CST816_SCL);
      }
    } else {
      Serial.printf("CST816 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                    CST816_ADDR, CST816_SDA, CST816_SCL);
    }
    return;
  }
#elif defined(USE_CST328)
  if (buttonType == BTN_TOUCHSCREEN) {
#ifdef CST328_RST
    pinMode(CST328_RST, OUTPUT);
    digitalWrite(CST328_RST, LOW);
    delay(20);
    digitalWrite(CST328_RST, HIGH);
    delay(100);  // CST328 needs ~100ms to boot after reset
#endif
    Wire.begin(CST328_SDA, CST328_SCL);
    Wire.setClock(400000);
    cst328BusReady = true;
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
        cst328Seen = true;
      } else {
        Serial.printf("CST328 detected on I2C addr 0x%02X, but register reads failed (SDA=%d SCL=%d)\n",
                      CST328_ADDR, CST328_SDA, CST328_SCL);
      }
    } else {
      Serial.printf("CST328 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                    CST328_ADDR, CST328_SDA, CST328_SCL);
    }
    return;
  }
#elif defined(USE_FT5X06) || defined(USE_FT6336)
  if (buttonType == BTN_TOUCHSCREEN) {
#if defined(USE_FT6336)
    // ws_lcd_350: FT6336 shares the I2C bus that also carries the TCA9554
    // LCD-reset expander (SDA=8, SCL=7). Reset is handled by the board POR;
    // the retry-at-runtime path covers any startup delay.
    Wire.begin(FT6336_SDA, FT6336_SCL);
#else
    // SenseCAP: FT5X06 shares the PCA9535PW IO-expander I2C bus (SDA=39,
    // SCL=40). Touch RST is pulsed via PCA9535 pin 7 during display init.
    Wire.begin(39, 40);
#endif
    Wire.setClock(400000);
    ft5x06BusReady = true;
    delay(50);  // Wait for touch controller to be ready after RST release
    if (ft5x06Probe()) {
      uint8_t touchPoints = 0;
      if (ft5x06ReadReg(FT5X06_TOUCH_POINTS_REG, touchPoints)) {
        Serial.printf("FocalTech touch initialized (I2C addr 0x%02X, touchPoints=%d)\n",
                      TOUCH_SLAVE_ADDRESS, touchPoints);
        ft5x06Seen = true;
      } else {
        Serial.printf("FocalTech touch detected on I2C addr 0x%02X, but register reads failed\n",
                      TOUCH_SLAVE_ADDRESS);
      }
    } else {
      Serial.printf("FocalTech touch did not answer at init (addr 0x%02X); will keep retrying at runtime\n",
                    TOUCH_SLAVE_ADDRESS);
    }
    return;
  }
#elif defined(USE_AXS_TOUCH)
  if (buttonType == BTN_TOUCHSCREEN) {
    Wire.begin(AXS_TOUCH_SDA, AXS_TOUCH_SCL);
    Wire.setClock(400000);
    axsTouchBusReady = true;
    pinMode(AXS_TOUCH_INT, INPUT_PULLUP);
    // Wire INT on FALLING edge — chip pulses low on touch-down for ~us-ms,
    // shorter than the main loop period, so level-polling misses fast taps.
    attachInterrupt(digitalPinToInterrupt(AXS_TOUCH_INT), axsTouchIsr, FALLING);
    if (axsTouchProbe()) {
      Serial.printf("AXS15231B touch initialized (I2C SDA=%d SCL=%d INT=%d, addr 0x%02X)\n",
                    AXS_TOUCH_SDA, AXS_TOUCH_SCL, AXS_TOUCH_INT, AXS_TOUCH_ADDR);
      axsTouchSeen = true;
    } else {
      Serial.printf("AXS15231B touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d INT=%d); will keep retrying at runtime\n",
                    AXS_TOUCH_ADDR, AXS_TOUCH_SDA, AXS_TOUCH_SCL, AXS_TOUCH_INT);
    }
    Serial.printf("AXS15231B touch INT(GPIO%d) initial level=%d (ISR attached, FALLING)\n",
                  AXS_TOUCH_INT, digitalRead(AXS_TOUCH_INT));
    return;
  }
#endif
  if (buttonType == BTN_TOUCHSCREEN) return;
  if (buttonPin == 0) return;
  if (buttonType == BTN_PUSH) {
    pinMode(buttonPin, INPUT_PULLUP);
  } else {  // BTN_TOUCH (TTP223)
    pinMode(buttonPin, INPUT);
  }
  lastRaw = false;
  stableState = false;
  lastChangeMs = 0;
  pressStartMs = 0;
}

bool wasButtonPressed() {
  if (buttonType == BTN_DISABLED) return false;

  bool raw;
  if (buttonType == BTN_TOUCHSCREEN) {
#if defined(USE_XPT2046)
    if (!touchReady) return false;
    // Phantom-touch guards for resistive XPT2046 panels (see top of file, #109).
    bool sensorTouch = (ts.getPoint().z >= XPT_TOUCH_Z_THRESHOLD);
    static bool touchArmed = false;  // set once a genuine release has been seen
    if (!sensorTouch) {
      if (!touchArmed) Serial.println("XPT2046: release seen, touch armed");
      touchArmed = true;
    }
    raw = sensorTouch && touchArmed;
#elif defined(USE_CST816)
    if (!cst816BusReady) return false;
    uint8_t touchNum = 0;
    if (!cst816ReadReg(CST816_TOUCH_NUM_REG, touchNum)) return false;
    if (!cst816Seen) {
      Serial.printf("CST816 touch became responsive at runtime (addr 0x%02X)\n", CST816_ADDR);
      cst816Seen = true;
    }
    raw = (touchNum > 0);
#elif defined(USE_CST328)
    if (!cst328BusReady) return false;
    uint8_t fingerState = 0;
    if (!cst328ReadFingerState(fingerState)) return false;
    if (!cst328Seen) {
      Serial.printf("CST328 touch became responsive at runtime (addr 0x%02X)\n", CST328_ADDR);
      cst328Seen = true;
    }
    // Byte at 0xD000 packs finger ID (high nibble) + state (low nibble).
    // Per Hynitron datasheet / CSE_CST328 reference, low nibble == 0x06
    // is "finger present". Checking the full byte is wrong because a
    // lifted finger can still report a non-zero ID.
    raw = ((fingerState & 0x0F) == CST328_FINGER_DOWN_STATE);
#elif defined(USE_FT5X06) || defined(USE_FT6336)
    if (!ft5x06BusReady) return false;
    uint8_t touchPoints = 0;
    if (!ft5x06ReadReg(FT5X06_TOUCH_POINTS_REG, touchPoints)) return false;
    if (!ft5x06Seen) {
      Serial.printf("FocalTech touch became responsive at runtime (addr 0x%02X)\n", TOUCH_SLAVE_ADDRESS);
      ft5x06Seen = true;
    }
    // TD_STATUS low nibble = active touch points; mask off the reserved high
    // bits so a stray high bit can't be misread as a permanent touch.
    raw = ((touchPoints & 0x0F) > 0);
#elif defined(USE_AXS_TOUCH)
    if (!axsTouchBusReady) return false;
    // The AXS15231B pulses INT low→high→low while a finger is held (the
    // ISR fires 20-30 times per contact, separated by sub-100 ms gaps).
    // Detecting release via INT level is therefore unreliable — a HIGH
    // observation in a gap looks identical to a real release. Use ISR
    // quiescence as the release signal instead: as long as the ISR keeps
    // firing, treat the finger as still down; only declare release once
    // no edge has happened for RELEASE_MS.
    {
      // Acceptable benign race: the ISR can fire and increment
      // axsIntFallingCount between our read into `cnt` and our write to
      // `axsIntFallingSeen`, in which case that one edge is "consumed"
      // without producing a press. Because the AXS15231B emits 20-30
      // edges per held finger, missing one boundary edge has no
      // observable effect — the next one fires the press, and the
      // 200 ms quiescence detector below still works correctly.
      uint32_t cnt = axsIntFallingCount;
      bool newEdge = (cnt != axsIntFallingSeen);
      axsIntFallingSeen = cnt;

      static unsigned long lastIsrMs = 0;
      unsigned long nowMs = millis();
      if (newEdge) lastIsrMs = nowMs;

      const unsigned long RELEASE_MS = 200;
      bool released = (nowMs - lastIsrMs > RELEASE_MS);

      if (released && stableState) {
        stableState = false;
        pressStartMs = 0;
      }

      if (newEdge && !stableState) {
        if (!axsTouchSeen) {
          Serial.printf("AXS15231B touch became responsive at runtime (addr 0x%02X)\n",
                        AXS_TOUCH_ADDR);
          axsTouchSeen = true;
        }
        stableState = true;
        pressStartMs = nowMs;
        return true;
      }
      return false;
    }
#elif defined(TOUCH_CS)
    uint16_t tx, ty;
    raw = tft.getTouch(&tx, &ty);
#else
    return false;
#endif
  } else if (buttonType == BTN_PUSH) {
    if (buttonPin == 0) return false;
    raw = (digitalRead(buttonPin) == LOW);   // active LOW with pull-up
  } else {
    if (buttonPin == 0) return false;
    raw = (digitalRead(buttonPin) == HIGH);  // TTP223: active HIGH
  }

  // Debounce
  if (raw != lastRaw) {
    lastChangeMs = millis();
    lastRaw = raw;
  }
  if ((millis() - lastChangeMs) < DEBOUNCE_MS) return false;

  // Rising edge detection
  bool result = false;
  if (raw && !stableState) {
    result = true;
    pressStartMs = millis();
  } else if (!raw && stableState) {
    pressStartMs = 0;
  }
  stableState = raw;

  return result;
}

bool isButtonHeld() {
  return stableState;
}

uint32_t buttonHoldDurationMs() {
  if (!stableState || pressStartMs == 0) return 0;
  return (uint32_t)(millis() - pressStartMs);
}
