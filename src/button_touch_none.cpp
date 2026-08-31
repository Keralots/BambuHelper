// Fallback touch backend: compiled when NO explicit USE_* driver is selected.
// Two sub-cases (see button_touch_backend.h):
//   - a board that exposes a LovyanGFX-handled resistive panel via TOUCH_CS but
//     no dedicated driver -> poll tft.getTouch(). This is the last-resort fallback
//     and is NOT reached by any current env (cyd/tzt define USE_XPT2046, which
//     wins over TOUCH_CS), so it is unverified on hardware.
//   - no touchscreen at all (esp32s3, esp32c3, the round/DIY envs) -> a true no-op.
#include "button_touch_backend.h"

#if (defined(USE_XPT2046) + defined(USE_CST816) + defined(USE_CST328) \
     + defined(USE_FT5X06) + defined(USE_FT6336) + defined(USE_AXS_TOUCH)) == 0

#if defined(TOUCH_CS)

#include "display_ui.h"  // displayDevice() for getTouch()

void touchInit() {}

TouchPoll touchPoll() {
  uint16_t tx, ty;
  bool down = displayDevice()->getTouch(&tx, &ty);
  // Boot release-gate: ignore contact until one genuine release is seen, so a
  // touch stuck-asserted from power-on (#109) cannot latch a phantom press.
  // Ported from the HSPI backend; Touch_XPT2046 has no pressure threshold.
  static bool armed = false;
  if (!down) armed = true;
  return {TouchEvent::None, (bool)(down && armed)};
}

#else  // no touchscreen at all

void touchInit() {}

TouchPoll touchPoll() {
  return {TouchEvent::None, false};
}

#endif  // TOUCH_CS

#endif  // no explicit driver
