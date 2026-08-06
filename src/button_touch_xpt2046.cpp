// XPT2046 resistive touch backend (cyd, tzt_2432). See button_touch_backend.h.
#include "button_touch_backend.h"

#if defined(USE_XPT2046)

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
static bool touchReady = false;

// Phantom-touch tolerance for resistive XPT2046 panels - CYD and TZT L1435-2.4
// (issue #109). A pinched touch flex can squeeze the resistive layers together,
// so the XPT2046 reports a constant weak "touch" from power-on. Left unfiltered
// that ramps and saves the LED brightness to max (hold-to-dim) within ~2 s and
// blocks screensaver wake (no fresh press edge ever arrives). Both guards apply to
// every XPT2046 board (resistive only); capacitive panels report clean release
// between taps and need neither.
//
// Guard 1 - raise the pressure bar. PaulStoffregen's Z_THRESHOLD is 300; the
//   pre-LovyanGFX TFT_eSPI build ran these same panels at 600, so match that
//   long-tested value to reject the weakest phantom contacts.
#ifndef XPT_TOUCH_Z_THRESHOLD
#define XPT_TOUCH_Z_THRESHOLD 600
#endif

void touchInit() {
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  touchReady = true;
  Serial.println("XPT2046 touch initialized (separate SPI)");
}

TouchPoll touchPoll() {
  if (!touchReady) return {TouchEvent::Unavailable, false};
  // Guard 1: pressure threshold.
  bool sensorTouch = (ts.getPoint().z >= XPT_TOUCH_Z_THRESHOLD);
  // Guard 2 - boot release-gate. A real touch always starts from a released
  //   panel; a stuck phantom is asserted from the first read and never lets go.
  //   Ignore all contact until at least one genuine release (no-touch read) has
  //   been observed since boot.
  static bool touchArmed = false;
  if (!sensorTouch) {
    if (!touchArmed) Serial.println("XPT2046: release seen, touch armed");
    touchArmed = true;
  }
  return {TouchEvent::None, (bool)(sensorTouch && touchArmed)};
}

#endif  // USE_XPT2046
