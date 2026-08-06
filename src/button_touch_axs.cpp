// AXS15231B integrated touch backend (jc3248w535). See button_touch_backend.h.
// This is the one EDGE-MANAGED backend: it detects presses from INT-line ISR
// activity and releases from ISR quiescence, so it reports Pressed/Released
// events that bypass the shared 50 ms debounce. It keeps its own held-state;
// button.cpp mirrors it into the public hold getters via Pressed/Released.
//
// INT line: per manufacturer demo code the AXS15231B touch INT is on GPIO 3,
// active-low, and pulses low->high->low ~20-30 times while a finger is held
// (sub-100 ms gaps). Level-polling the I2C state misses sub-loop-rate taps, so we
// use the INT edge as the trigger.
#include "button_touch_backend.h"

#if defined(USE_AXS_TOUCH)

#include <Wire.h>

#define AXS_TOUCH_ADDR 0x3B  // AXS_TOUCH_INT default comes from button_touch_backend.h

static bool busReady = false;
static bool seen = false;
static bool held = false;                           // "finger currently down"
static volatile uint32_t axsIntFallingCount = 0;    // incremented by the ISR
static uint32_t axsIntFallingSeen = 0;              // last value drained by poller
static unsigned long lastIsrMs = 0;

static void IRAM_ATTR axsTouchIsr() {
  axsIntFallingCount++;
}

static bool axsTouchProbe() {
  Wire.beginTransmission(AXS_TOUCH_ADDR);
  return Wire.endTransmission(true) == 0;
}

void touchInit() {
  Wire.begin(AXS_TOUCH_SDA, AXS_TOUCH_SCL);
  Wire.setClock(400000);
  busReady = true;
  pinMode(AXS_TOUCH_INT, INPUT_PULLUP);
  // Wire INT on FALLING edge - chip pulses low on touch-down for ~us-ms, shorter
  // than the main loop period, so level-polling misses fast taps.
  attachInterrupt(digitalPinToInterrupt(AXS_TOUCH_INT), axsTouchIsr, FALLING);
  if (axsTouchProbe()) {
    Serial.printf("AXS15231B touch initialized (I2C SDA=%d SCL=%d INT=%d, addr 0x%02X)\n",
                  AXS_TOUCH_SDA, AXS_TOUCH_SCL, AXS_TOUCH_INT, AXS_TOUCH_ADDR);
    seen = true;
  } else {
    Serial.printf("AXS15231B touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d INT=%d); will keep retrying at runtime\n",
                  AXS_TOUCH_ADDR, AXS_TOUCH_SDA, AXS_TOUCH_SCL, AXS_TOUCH_INT);
  }
  Serial.printf("AXS15231B touch INT(GPIO%d) initial level=%d (ISR attached, FALLING)\n",
                AXS_TOUCH_INT, digitalRead(AXS_TOUCH_INT));
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  // The AXS15231B pulses INT low->high->low while a finger is held (the ISR fires
  // 20-30 times per contact, separated by sub-100 ms gaps). Detecting release via
  // INT level is therefore unreliable - a HIGH observation in a gap looks like a
  // real release. Use ISR quiescence as the release signal instead: as long as the
  // ISR keeps firing, treat the finger as still down; only declare release once no
  // edge has happened for RELEASE_MS.
  //
  // Acceptable benign race: the ISR can fire and increment axsIntFallingCount
  // between our read into `cnt` and our write to axsIntFallingSeen, in which case
  // that one edge is "consumed" without producing a press. Because the AXS emits
  // 20-30 edges per held finger, missing one boundary edge has no observable
  // effect - the next one fires the press, and the quiescence detector still works.
  uint32_t cnt = axsIntFallingCount;
  bool newEdge = (cnt != axsIntFallingSeen);
  axsIntFallingSeen = cnt;

  unsigned long nowMs = millis();
  if (newEdge) lastIsrMs = nowMs;

  const unsigned long RELEASE_MS = 200;
  bool released = (nowMs - lastIsrMs > RELEASE_MS);

  if (released && held) {
    held = false;
    return {TouchEvent::Released, false};
  }

  if (newEdge && !held) {
    if (!seen) {
      Serial.printf("AXS15231B touch became responsive at runtime (addr 0x%02X)\n", AXS_TOUCH_ADDR);
      seen = true;
    }
    held = true;
    return {TouchEvent::Pressed, true};
  }

  return {TouchEvent::None, held};
}

#endif  // USE_AXS_TOUCH
