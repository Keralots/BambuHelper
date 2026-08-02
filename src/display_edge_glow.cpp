#include "display_edge_glow.h"
#include "config.h"
#include "settings.h"

#if defined(DISPLAY_ROUND_240)

// Round panels: no rectangular border to glow. Ring variant is a follow-up;
// until then the controller is inert so shared call sites need no guards.
void glowNotifyEvent(uint8_t, GlowEvent) {}
void glowClearSlot(uint8_t) {}
void glowDismiss() {}
bool glowIsActive() { return false; }
bool glowIsArmed() { return false; }
bool glowTick(lgfx::LovyanGFX&, uint8_t, bool) { return false; }
bool glowConsumeCleanup() { return false; }
void glowStartTest(uint8_t) {}
bool glowTestRunning() { return false; }

#else

static const uint8_t GLOW_MAX_SLOTS = 4;

enum GlowPhase : uint8_t {
  PHASE_IDLE,     // nothing armed for the displayed slot
  PHASE_ACTIVE,   // band animating
  PHASE_FADE,     // brightness ramping out over GLOW_FADE_MS
  PHASE_REMIND,   // dark pause between reminder pulses (still armed)
};

static uint8_t   latchMask = 0;                    // bit per slot: event pending
static GlowEvent latchEvent[GLOW_MAX_SLOTS];
static GlowPhase phase = PHASE_IDLE;
static uint8_t   activeSlot = 0xFF;
static GlowEvent activeEvent = GLOW_EV_FINISH;
static bool      remindEpisode = false;            // current ACTIVE run is a short reminder pulse
static unsigned long phaseStartMs = 0;
static unsigned long lastFrameMs = 0;
static bool      cleanupPending = false;
static bool      testMode = false;                 // web-UI preview episode

// Widest supported panel dimension (320x480 layouts).
static uint16_t lineBuf[480];

static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
  uint32_t fr = (fg >> 11) & 0x1F, fgg = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  uint32_t br = (bg >> 11) & 0x1F, bgg = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  uint32_t inv = 255 - alpha;
  return (uint16_t)((((fr * alpha + br * inv) / 255) << 11) |
                    (((fgg * alpha + bgg * inv) / 255) << 5) |
                     ((fb * alpha + bb * inv) / 255));
}

// Hue (0-359) at full saturation/value to RGB565.
static uint16_t hueToRgb565(uint16_t h) {
  uint8_t seg = h / 60;
  uint8_t rem = (uint8_t)(((h % 60) * 255) / 60);
  uint8_t r, g, b;
  switch (seg) {
    case 0:  r = 255;       g = rem;       b = 0;         break;
    case 1:  r = 255 - rem; g = 255;       b = 0;         break;
    case 2:  r = 0;         g = 255;       b = rem;       break;
    case 3:  r = 0;         g = 255 - rem; b = 255;       break;
    case 4:  r = rem;       g = 0;         b = 255;       break;
    default: r = 255;       g = 0;         b = 255 - rem; break;
  }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void startEpisode(uint8_t slot, bool remind) {
  activeSlot = slot;
  remindEpisode = remind;
  phase = PHASE_ACTIVE;
  phaseStartMs = millis();
  lastFrameMs = 0;  // draw on the next tick
}

static void stopDrawing(bool armReminder) {
  if (phase == PHASE_ACTIVE || phase == PHASE_FADE) cleanupPending = true;
  phase = armReminder ? PHASE_REMIND : PHASE_IDLE;
  phaseStartMs = millis();
  if (!armReminder) activeSlot = 0xFF;
  testMode = false;
}

void glowNotifyEvent(uint8_t slot, GlowEvent ev) {
  if (dispSettings.glowMode == 0) return;
  if (slot >= GLOW_MAX_SLOTS) return;
  latchMask |= (uint8_t)(1u << slot);
  latchEvent[slot] = ev;
}

void glowClearSlot(uint8_t slot) {
  if (slot >= GLOW_MAX_SLOTS) return;
  latchMask &= (uint8_t)~(1u << slot);
  if (activeSlot == slot && phase != PHASE_IDLE) stopDrawing(false);
}

void glowDismiss() {
  if (activeSlot < GLOW_MAX_SLOTS) latchMask &= (uint8_t)~(1u << activeSlot);
  if (phase != PHASE_IDLE) stopDrawing(false);
}

bool glowIsActive() {
  return phase == PHASE_ACTIVE || phase == PHASE_FADE;
}

bool glowIsArmed() {
  return phase != PHASE_IDLE;
}

bool glowConsumeCleanup() {
  if (!cleanupPending) return false;
  cleanupPending = false;
  return true;
}

void glowStartTest(uint8_t slot) {
  if (dispSettings.glowMode == 0) return;
  if (slot >= GLOW_MAX_SLOTS) slot = 0;
  activeEvent = GLOW_EV_FINISH;
  testMode = true;
  startEpisode(slot, true);  // reminder-length episode: ~5 s, then fade + stop
}

bool glowTestRunning() { return testMode; }

// Draw the four border strips. Perimeter coordinate q runs clockwise from the
// top-left corner along the outer edge; sweep intensity is a decaying tail
// behind a head that laps the perimeter, pulse breathes the whole band.
// Every visible row/column is fully redrawn each frame - the band owns its
// pixels while active, whatever the base screen painted under it.
static void drawBand(lgfx::LovyanGFX& gfx, unsigned long now, uint8_t fade) {
  const int16_t w = (int16_t)gfx.width();
  const int16_t h = (int16_t)gfx.height();
  const int16_t T = GLOW_THICKNESS_PX;
  const int32_t perim = 2 * (int32_t)(w + h);
  const uint16_t bg = dispSettings.bgColor;

  const bool rainbow = (activeEvent == GLOW_EV_FINISH) && (dispSettings.glowMode == 2);
  const uint16_t baseColor = (activeEvent == GLOW_EV_FAILED) ? CLR_RED
                                                             : dispSettings.glowColor;
  const uint8_t style = dispSettings.glowStyle;  // 0 Sweep, 1 Pulse, 2 Storm

  // Sweep head position + tail length in perimeter units.
  const int32_t headQ = (int32_t)(((uint64_t)(now % GLOW_SWEEP_LAP_MS) * perim) / GLOW_SWEEP_LAP_MS);
  const int32_t tailLen = perim / 4;
  // Pulse brightness: raised cosine, never fully dark so the band stays visible.
  uint8_t pulseA = 255;
  if (style == 1) {
    float ph = (float)(now % GLOW_PULSE_PERIOD_MS) / (float)GLOW_PULSE_PERIOD_MS;
    pulseA = (uint8_t)(40.0f + 215.0f * (0.5f - 0.5f * cosf(ph * 2.0f * (float)M_PI)));
  }
  // Slow uniform hue drift for rainbow pulse (sweep spreads hue spatially).
  const uint16_t hueShift = (uint16_t)((now / 16) % 360);
  // Storm: the tail shatters into 4 px shards, each with its own flickering
  // brightness (and scrambled hue in rainbow mode). Reseeded ~20 Hz so the
  // shards crackle - a deliberate recreation of the pre-byte-swap-fix glitch
  // look the user asked to keep.
  const uint32_t stormSeed = (uint32_t)(now / 50) * 0x9E3779B9u;

  // lineBuf holds native little-endian RGB565; pushImage sends it verbatim
  // unless byte swapping is on, which paints e.g. green 0x07E0 as red 0xE007.
  const bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.startWrite();
  for (uint8_t side = 0; side < 4; side++) {
    const bool horizontal = (side == 0 || side == 2);
    const int16_t len = horizontal ? w : (int16_t)(h - 2 * T);
    if (len <= 0) continue;
    for (int16_t t = 0; t < T; t++) {
      // Inward fade: outer row full strength, inner row blends into the bg.
      const uint8_t rowA = (uint8_t)(255 - ((uint16_t)t * 200) / (T > 1 ? T - 1 : 1));
      for (int16_t i = 0; i < len; i++) {
        int32_t q;
        switch (side) {
          case 0:  q = i;                       break;  // top, left->right
          case 1:  q = w + T + i;               break;  // right, top->bottom
          case 2:  q = w + h + (w - 1 - i);     break;  // bottom, right->left
          default: q = 2 * w + h + (h - T - 1 - i); break;  // left, bottom->top
        }
        uint16_t color;
        uint32_t a = rowA;
        if (style == 0) {
          int32_t d = headQ - q;
          if (d < 0) d += perim;
          if (d >= tailLen) { lineBuf[i] = bg; continue; }
          uint32_t tail = (uint32_t)(tailLen - d) * 255 / (uint32_t)tailLen;
          a = a * ((tail * tail) >> 8) >> 8;
          color = rainbow ? hueToRgb565((uint16_t)(((q * 360) / perim + hueShift) % 360))
                          : baseColor;
        } else if (style == 2) {
          int32_t d = headQ - q;
          if (d < 0) d += perim;
          if (d >= tailLen) { lineBuf[i] = bg; continue; }
          uint32_t tail = (uint32_t)(tailLen - d) * 255 / (uint32_t)tailLen;
          // Two lightning bolts run along the band, each jumping to a random
          // row every 8 px: jagged horizontal streaks (parallel to the edge),
          // bright core + dim halo, black between. Reseeded ~20 Hz to crackle.
          // The inward row fade is skipped here - a bolt flashes at full
          // strength wherever it sits in the band.
          uint32_t seg = (uint32_t)(q >> 3);
          uint32_t h1 = (seg * 2654435761u) ^ stormSeed;
          h1 ^= h1 >> 15; h1 *= 0x85EBCA6Bu; h1 ^= h1 >> 13;
          uint32_t h2 = (seg * 0x9E3779B9u) ^ (stormSeed * 3u);
          h2 ^= h2 >> 15; h2 *= 0x85EBCA6Bu; h2 ^= h2 >> 13;
          int b1 = (int)t - (int)(h1 % (uint32_t)T);
          int b2 = (int)t - (int)(h2 % (uint32_t)T);
          if (b1 < 0) b1 = -b1;
          if (b2 < 0) b2 = -b2;
          uint32_t nearHash = (b1 <= b2) ? h1 : h2;
          int dmin = (b1 <= b2) ? b1 : b2;
          uint32_t bolt = (dmin == 0) ? 255 : (dmin == 1) ? 130 : 20;
          a = (tail * bolt) >> 8;
          color = rainbow ? hueToRgb565((uint16_t)(nearHash % 360)) : baseColor;
        } else {
          a = (a * pulseA) >> 8;
          color = rainbow ? hueToRgb565(hueShift) : baseColor;
        }
        a = (a * fade) >> 8;
        lineBuf[i] = blend565(color, bg, (uint8_t)a);
      }
      switch (side) {
        case 0:  gfx.pushImage(0, t, w, 1, lineBuf);               break;
        case 1:  gfx.pushImage(w - 1 - t, T, 1, h - 2 * T, lineBuf); break;
        case 2:  gfx.pushImage(0, h - 1 - t, w, 1, lineBuf);       break;
        default: gfx.pushImage(t, T, 1, h - 2 * T, lineBuf);       break;
      }
    }
  }
  gfx.endWrite();
  gfx.setSwapBytes(oldSwap);
}

// Paint the band area back to the background color so the cleanup repaint
// starts from a clean base (margins outside any widget never repaint).
static void clearBand(lgfx::LovyanGFX& gfx) {
  const int16_t w = (int16_t)gfx.width();
  const int16_t h = (int16_t)gfx.height();
  const int16_t T = GLOW_THICKNESS_PX;
  const uint16_t bg = dispSettings.bgColor;
  gfx.startWrite();
  gfx.fillRect(0, 0, w, T, bg);
  gfx.fillRect(0, h - T, w, T, bg);
  gfx.fillRect(0, T, T, h - 2 * T, bg);
  gfx.fillRect(w - T, T, T, h - 2 * T, bg);
  gfx.endWrite();
}

bool glowTick(lgfx::LovyanGFX& gfx, uint8_t slot, bool force) {
  unsigned long now = millis();

  // Live-disable (web UI) while armed or animating.
  if (dispSettings.glowMode == 0) {
    if (phase != PHASE_IDLE) glowDismiss();
    latchMask = 0;
    if (cleanupPending) { clearBand(gfx); return true; }
    return false;
  }

  // Displayed slot changed away from the animating one: drop that episode,
  // its latch is already consumed. A latch for the new slot starts below.
  if (phase != PHASE_IDLE && activeSlot != slot) stopDrawing(false);

  // Start a latched event for the slot now on screen.
  if (phase == PHASE_IDLE && slot < GLOW_MAX_SLOTS && (latchMask & (1u << slot))) {
    latchMask &= (uint8_t)~(1u << slot);
    activeEvent = latchEvent[slot];
    startEpisode(slot, false);
  }

  if (cleanupPending) {
    // Band just stopped: hand the edge pixels back before the base repaint.
    clearBand(gfx);
  }

  switch (phase) {
    case PHASE_IDLE:
      return false;

    case PHASE_REMIND:
      if (now - phaseStartMs >= GLOW_REMIND_EVERY_MS) startEpisode(activeSlot, true);
      return false;

    case PHASE_ACTIVE: {
      unsigned long limit = remindEpisode ? GLOW_REMIND_ON_MS
                          : (dispSettings.glowDuration == 1) ? GLOW_CONT_CEILING_MS
                                                             : GLOW_BURST_MS;
      if (now - phaseStartMs >= limit) {
        phase = PHASE_FADE;
        phaseStartMs = now;
      }
      break;
    }

    case PHASE_FADE:
      if (now - phaseStartMs >= GLOW_FADE_MS) {
        // Burst mode and the test preview end for good; the other modes keep
        // pulsing reminders.
        stopDrawing(!testMode && dispSettings.glowDuration != 0);
        clearBand(gfx);
        return true;
      }
      break;
  }

  if (!force && lastFrameMs != 0 && now - lastFrameMs < GLOW_ANIM_MS) return false;
  lastFrameMs = now;

  uint8_t fade = 255;
  if (phase == PHASE_FADE) {
    unsigned long el = now - phaseStartMs;
    fade = (uint8_t)(255 - (el * 255) / GLOW_FADE_MS);
  }
  drawBand(gfx, now, fade);
  return true;
}

#endif // DISPLAY_ROUND_240
