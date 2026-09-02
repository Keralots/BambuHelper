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

// Authoritative finger-down state read from the touch IC. INT-quiescence alone
// cannot separate a fast double-tap from one held finger (a re-tap inside
// RELEASE_MS looks like continuous contact), so the multi-click gesture used to
// collapse to a single press. Command + parse verified identical in two
// independent drivers (ESPHome axs15231 + me-processware JC3248W535): write the
// 8-byte read command, read 8 bytes, finger present when data[0]==0 && data[1]!=0.
// Returns 1 = finger down, 0 = finger up, -1 = read failed (state unknown).
static int axsReadFinger() {
  static const uint8_t cmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08};
  Wire.beginTransmission(AXS_TOUCH_ADDR);
  Wire.write(cmd, sizeof(cmd));
  if (Wire.endTransmission() != 0) return -1;
  uint8_t data[8] = {0};
  uint8_t got = Wire.requestFrom((int)AXS_TOUCH_ADDR, (int)sizeof(data));
  uint8_t i = 0;
  while (Wire.available() && i < sizeof(data)) data[i++] = Wire.read();
  if (got < 2 || i < 2) return -1;
  return (data[0] == 0 && data[1] != 0) ? 1 : 0;
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
  // 20-30 times per contact, separated by sub-100 ms gaps). The INT edge is the
  // press trigger (a level poll would miss sub-loop-rate taps); release is taken
  // from an authoritative I2C finger read (axsReadFinger), because INT quiescence
  // alone can't tell a fast double-tap from one held finger.
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

  // Release the instant the IC reports finger-up (gated by a short quiescence so
  // a single stray read can't drop a real hold). This gives clean per-tap
  // boundaries, so a fast double-tap registers as two presses instead of merging
  // into one held contact. If the read fails, fall back to the legacy timer.
  int finger = axsReadFinger();
  const unsigned long RELEASE_MS       = 200;   // fallback: no finger read
  const unsigned long RELEASE_GUARD_MS = 40;    // finger-up debounce
  bool released = (finger < 0)
                  ? (nowMs - lastIsrMs > RELEASE_MS)
                  : (finger == 0 && nowMs - lastIsrMs > RELEASE_GUARD_MS);

  if (released && held) {
    held = false;
    return {TouchEvent::Released, false};
  }

  // Press on the INT edge (catches sub-loop taps) or a positive finger read.
  if ((newEdge || finger == 1) && !held) {
    if (!seen) {
      Serial.printf("AXS15231B touch became responsive at runtime (addr 0x%02X)\n", AXS_TOUCH_ADDR);
      seen = true;
    }
    held = true;
    lastIsrMs = nowMs;
    return {TouchEvent::Pressed, true};
  }

  return {TouchEvent::None, held};
}

#endif  // USE_AXS_TOUCH
