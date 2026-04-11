#include "button.h"
#include "settings.h"

#if defined(USE_XPT2046)
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>
  static SPIClass touchSPI(HSPI);
  static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
  static bool touchReady = false;
#elif defined(USE_CST816)
  #include <Wire.h>
  #define CST816_ADDR          0x15
  #define CST816_TOUCH_NUM_REG 0x02
  static bool cst816Ready = false;
#elif defined(TOUCH_CS)
  #include "display_ui.h"  // extern tft for getTouch()
#elif defined(DISPLAY_RAK14014)
#include "RAK14014_FT6336U.h"
/** Touch screen driver instance */
FT6336U ft6336u;
bool wait_for_touch = true;
bool btnDebugLog = false;
#define BTN_LOG(fmt, ...)                                   \
	  do                                                      \
	  {                                                       \
		  if (btnDebugLog)                                    \
			  Serial.printf("BTN: " fmt "\n", ##__VA_ARGS__); \
	  } while (0)

static void keyIntHandle(void)
{
	// Conditional debug print
	BTN_LOG("BTN INT");
	wait_for_touch = false;
	detachInterrupt(digitalPinToInterrupt(WB_IO6));
}
#endif

  static bool lastRaw = false;
  static bool stableState = false;
  static unsigned long lastChangeMs = 0;
  static const unsigned long DEBOUNCE_MS = 50;

  void initButton() {
	  if (buttonType == BTN_DISABLED) return;
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
		  Wire.begin(CST816_SDA, CST816_SCL);
		  Wire.setClock(400000);
		  cst816Ready = true;
		  Serial.println("CST816 touch initialized (I2C)");
		  return;
	  }
#elif defined(DISPLAY_RAK14014)
	  if (buttonType == BTN_TOUCHSCREEN) { // RAK14014 touch screen
		  ft6336u.begin();
		  attachInterrupt(digitalPinToInterrupt(WB_IO6), keyIntHandle, FALLING);
		  BTN_LOG("BTN initialized");
		  buttonType = BTN_PUSH;
		  return;
	  }
#endif
	  if (buttonType == BTN_TOUCHSCREEN) return;
	  if (buttonPin == 0) return;
	  if (buttonType == BTN_PUSH) {
		  pinMode(buttonPin, INPUT_PULLUP);
	  } else { // BTN_TOUCH (TTP223)
		  pinMode(buttonPin, INPUT);
	  }
	  lastRaw = false;
	  stableState = false;
	  lastChangeMs = 0;
  }

bool wasButtonPressed() {
  if (buttonType == BTN_DISABLED) return false;

  bool raw;
  if (buttonType == BTN_TOUCHSCREEN) {
#if defined(USE_XPT2046)
    if (!touchReady) return false;
    raw = ts.touched();
#elif defined(USE_CST816)
    if (!cst816Ready) return false;
    uint8_t touchNum = 0;
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(CST816_TOUCH_NUM_REG);
    if (Wire.endTransmission(true) == 0) {
      Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)1);
      if (Wire.available()) touchNum = Wire.read();
    }
    raw = (touchNum > 0);
#elif defined(TOUCH_CS)
    uint16_t tx, ty;
    raw = tft.getTouch(&tx, &ty);
#eiif defined(DISPLAY_RAK14014)
	if (wait_for_touch) {
		return false;
	} else {
		wait_for_touch = true;
		BTN_LOG("BTN touch return");
		attachInterrupt(digitalPinToInterrupt(WB_IO6), keyIntHandle, FALLING);
		return true;
	}
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
  }
  stableState = raw;

  return result;
}
