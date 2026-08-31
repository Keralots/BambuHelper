#include "button.h"
#include "settings.h"
#include "buzzer.h"
#include "button_touch_backend.h"

// Touch-driver specifics live in button_touch_<driver>.cpp behind the
// button_touch_backend.h interface (mirrors the buzzer backends). This file owns
// pin sanitizing, the shared debounce/hold state, and the GPIO button types.

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

#if defined(BOARD_IS_DIY)
  // A pin carried over from another board's config could drive a DIY display
  // line, or one of the chip's flash / USB pins - see isDiyReservedPin().
  if (isDiyReservedPin(buttonPin)) { clash("a reserved DIY pin"); return; }
#endif
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
#if defined(USE_FT5X06)
  #if defined(FT5X06_SDA)
  if (buttonPin == FT5X06_SDA) { clash("FT5X06 touch SDA"); return; }
  #endif
  #if defined(FT5X06_SCL)
  if (buttonPin == FT5X06_SCL) { clash("FT5X06 touch SCL"); return; }
  #endif
  #if defined(FT5X06_IRQ)
  if (buttonPin == FT5X06_IRQ) { clash("FT5X06 touch IRQ"); return; }
  #endif
#endif
#if defined(BOARD_IS_WS350)
  // Display SPI lines - driving any as a button GPIO disturbs the panel bus.
  // (Backlight 6 and I2C 7/8 are already covered above / by FT6336 checks.)
  if (buttonPin == 1) { clash("WS350 display MOSI"); return; }
  if (buttonPin == 2) { clash("WS350 display MISO"); return; }
  if (buttonPin == 3) { clash("WS350 display DC");   return; }
  if (buttonPin == 5) { clash("WS350 display SCLK"); return; }
#endif
#if defined(BOARD_IS_SC05X)
  // ST7789 8-bit 8080 parallel bus + control - driving any as a button GPIO
  // disturbs the panel. Backlight 47 and touch 8/9/48 are covered above.
  if (buttonPin == 1 || buttonPin == 2 || buttonPin == 7 ||
      buttonPin == 15 || buttonPin == 16 || buttonPin == 17 ||
      buttonPin == 18 || buttonPin == 40 || buttonPin == 41 ||
      buttonPin == 42) { clash("SC05_X LCD bus/control"); return; }
  if (buttonPin == 3)  { clash("SC05_X LCD reset"); return; }
  if (buttonPin == 38) { clash("SC05_X LCD_TE"); return; }
  if (buttonPin == 19 || buttonPin == 20) {
    clash("SC05_X native USB");
    return;
  }
  if (buttonPin >= 26 && buttonPin <= 37) {
    clash("SC05_X flash/PSRAM");
    return;
  }
  if (buttonPin == 4 || buttonPin == 5 || buttonPin == 6) {
    clash("SC05_X RS485");
    return;
  }
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
#if defined(BOARD_IS_ES3N28P)
  // initButton() calls pinMode() on a configured physical-button pin, so a
  // stale/manual value pointing at a reserved peripheral must be rejected.
  // Same reserved set as the LED deny-list (led.cpp) and buzzer sanitizer.
  // (FT6336 SDA/SCL 16/15 and backlight 45 are already caught above.)
  {
    uint8_t p = buttonPin;
    bool reserved =
      (p == 10 || p == 11 || p == 12 || p == 13 || p == 46) ||  // display SPI
      (p == 1 || p == 4 || p == 5 || p == 6 || p == 7 || p == 8) ||  // audio + amp
      (p == 17 || p == 18) ||                                    // touch INT/RST
      (p == 9) ||                                                // battery ADC
      (p == 42) ||                                               // WS2812
      (p == 38 || p == 39 || p == 40 || p == 41 || p == 47 || p == 48) || // microSD
      (p == 19 || p == 20) ||                                    // USB CDC
      (p >= 26 && p <= 37);                                      // flash/PSRAM
    if (reserved) { clash("ES3N28P reserved peripheral"); return; }
  }
#endif
#if defined(BOARD_IS_WS200)
  // Same reserved set as the WS200 LED deny-list (led.cpp) and buzzer
  // sanitizer. initButton() calls pinMode() on the configured pin, so a
  // stale/manual value pointing at the flash/PSRAM bus must be rejected here
  // too. (CST816D SDA/SCL 48/47 and backlight 1 are already caught above.)
  {
    uint8_t p = buttonPin;
    bool reserved =
      (p == 38 || p == 39 || p == 40 || p == 42 || p == 45) ||  // display SPI
      (p == 5) ||                                                // battery ADC
      (p == 19 || p == 20) ||                                    // USB CDC
      (p >= 26 && p <= 37);                                      // flash/PSRAM
    if (reserved) { clash("WS200 reserved peripheral"); return; }
  }
#endif
#if defined(BOARD_IS_WS280)
  // Same reserved set as the WS280 LED deny-list (led.cpp) and buzzer
  // sanitizer. (CST328 SDA/SCL/IRQ/RST 1/3/4/2 and backlight 5 are already
  // caught above.)
  {
    uint8_t p = buttonPin;
    bool reserved =
      (p == 39 || p == 40 || p == 41 || p == 42 || p == 45) ||  // display SPI
      (p == 19 || p == 20) ||                                    // USB CDC
      (p >= 26 && p <= 37);                                      // flash/PSRAM
    if (reserved) { clash("WS280 reserved peripheral"); return; }
  }
#endif
#if defined(BOARD_IS_E32R40T)
  // E32R40T deliberately omits USE_XPT2046, so the generic XPT checks below
  // compile out and the touch/LCD pins stay selectable as a button. Reject the
  // same reserved set as led.cpp. (Backlight 27 caught above.)
  {
    uint8_t p = buttonPin;
    bool reserved =
      (p == 2 || p == 12 || p == 13 || p == 14 || p == 15) ||   // display SPI
      (p == 33 || p == 36) ||                                    // XPT2046 CS/IRQ
      (p == 16 || p == 17 || p == 22) ||                         // onboard RGB
      (p == 4 || p == 26) ||                                     // amp EN + DAC
      (p == 5 || p == 18 || p == 19 || p == 23) ||               // SD card
      (p >= 6 && p <= 11);                                       // flash
    if (reserved) { clash("E32R40T reserved peripheral"); return; }
  }
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
  if (buttonType == BTN_TOUCHSCREEN) {
    touchInit();  // bus/pins + first probe live in the selected backend
    return;
  }
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

  bool raw = false;
  if (buttonType == BTN_TOUCHSCREEN) {
    TouchPoll tp = touchPoll();
    // A failed bus/read must NOT disturb debounce or hold state - it is not a
    // release (a genuinely held finger whose poll failed mid-hold stays held).
    if (tp.ev == TouchEvent::Unavailable) return false;
    // Edge-managed backends (AXS) resolve press/release from ISR activity
    // themselves; map the edge straight to hold state, bypassing the 50 ms
    // debounce. Level backends only ever report None, so they fall through
    // to the shared debouncer below.
    if (tp.ev == TouchEvent::Pressed) {
      stableState = true;
      pressStartMs = millis();
      return true;
    }
    if (tp.ev == TouchEvent::Released) {
      stableState = false;
      pressStartMs = 0;
      return false;
    }
    // None: feed the raw finger-down level into the shared debouncer. (For an
    // edge-managed backend isDown mirrors its held state, so the debouncer
    // never sees a false->true edge it hasn't already reported as Pressed.)
    raw = tp.isDown;
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
