#include "display_edge_glow.h"
#include "config.h"
#include "settings.h"

// The controller logic (latching, phases, dismissal, timing) is identical on
// every panel; only the renderer differs. Rectangular panels push full-width
// line buffers for four straight strips; round panels fill an annular ring with
// native fillArc wedges. drawBand()/clearBand() are the two platform-specific
// hooks, selected by DISPLAY_IS_ROUND below; everything else is shared.

static const uint8_t GLOW_MAX_SLOTS = 4;

enum GlowPhase : uint8_t {
  PHASE_IDLE,     // nothing armed for the displayed slot
  PHASE_ACTIVE,   // band animating
  PHASE_FADE,     // brightness ramping out over GLOW_FADE_MS
  PHASE_REMIND,   // dark pause between reminder pulses (still armed)
};

static uint8_t   latchMask = 0;                    // bit per slot: event pending
static GlowEvent latchEvent[GLOW_MAX_SLOTS];
static uint16_t  latchColor[GLOW_MAX_SLOTS];       // GLOW_EV_ERROR severity colour
static uint16_t  activeColor = 0;
static GlowPhase phase = PHASE_IDLE;
static uint8_t   activeSlot = 0xFF;
static GlowEvent activeEvent = GLOW_EV_FINISH;
static bool      remindEpisode = false;            // current ACTIVE run is a short reminder pulse
static unsigned long phaseStartMs = 0;
static unsigned long lastFrameMs = 0;
static bool      cleanupPending = false;
static bool      testMode = false;                 // web-UI preview episode
static bool      primeBand = false;                // erase the base ring once at episode start

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
  primeBand = true; // wipe the base ring (e.g. the gold finished rim) on frame 1
}

static void stopDrawing(bool armReminder) {
  if (phase == PHASE_ACTIVE || phase == PHASE_FADE) cleanupPending = true;
  phase = armReminder ? PHASE_REMIND : PHASE_IDLE;
  phaseStartMs = millis();
  if (!armReminder) activeSlot = 0xFF;
  testMode = false;
}

void glowNotifyEvent(uint8_t slot, GlowEvent ev, uint16_t color) {
  if (dispSettings.glowMode == 0) return;
  if (slot >= GLOW_MAX_SLOTS) return;
  // A print outcome outranks an error report: never let a new error overwrite
  // a FINISH or FAILED that has not been shown yet.
  if (ev == GLOW_EV_ERROR && (latchMask & (1u << slot)) &&
      latchEvent[slot] != GLOW_EV_ERROR) return;
  // The same precedence has to hold against an error episode already RUNNING,
  // not just a pending error latch. glowTick() consumes a latch only from
  // PHASE_IDLE, and an error episode does not end on its own in the reminder
  // duration modes - so without this the band stayed red under the finish
  // screen and the finish glow never played at all (issue #165).
  if (ev != GLOW_EV_ERROR && phase != PHASE_IDLE && activeEvent == GLOW_EV_ERROR &&
      activeSlot == slot) {
    stopDrawing(false);
  }
  latchMask |= (uint8_t)(1u << slot);
  latchEvent[slot] = ev;
  latchColor[slot] = color;
}

// Only error episodes are cancelled by their cause going away: a FINISH or
// FAILED announces something that already happened and stays until seen, but an
// error is a live condition, and once the printer has recovered the glow is
// describing a state that no longer exists.
void glowClearError(uint8_t slot) {
  if (slot >= GLOW_MAX_SLOTS) return;
  if ((latchMask & (1u << slot)) && latchEvent[slot] == GLOW_EV_ERROR)
    latchMask &= (uint8_t)~(1u << slot);
  if (activeSlot == slot && phase != PHASE_IDLE && activeEvent == GLOW_EV_ERROR)
    stopDrawing(false);   // covers PHASE_REMIND too - the pause is still armed
}

bool glowIsErrorEpisode() {
  return phase != PHASE_IDLE && activeEvent == GLOW_EV_ERROR;
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

// ---------------------------------------------------------------------------
//  Renderer: round ring (DISPLAY_IS_ROUND)
// ---------------------------------------------------------------------------
#if DISPLAY_IS_ROUND

// Integer sqrt / atan2 - the C3 (RISC-V) has no FPU, so the per-pixel band
// rasterizer below must avoid sqrtf/atan2f. isqrt32 gives the row half-widths;
// iatan2deg gives each pixel's angle (0..359, ~+-2 deg, plenty for a glow).
static inline uint16_t isqrt32(uint32_t x) {
  uint32_t r = 0, b = 1u << 30;
  while (b > x) b >>= 2;
  while (b) {
    if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
    else            { r >>= 1; }
    b >>= 2;
  }
  return (uint16_t)r;
}
static inline int iatan2deg(int dy, int dx) {
  if (dx == 0 && dy == 0) return 0;
  int ax = dx < 0 ? -dx : dx;
  int ay = dy < 0 ? -dy : dy;
  int a = (ax >= ay) ? (45 * ay) / (ax ? ax : 1)
                     : 90 - (45 * ax) / (ay ? ay : 1);
  if (dx >= 0) return (dy >= 0) ? a         : 360 - a;
  else         return (dy >= 0) ? 180 - a   : 180 + a;
}

// Sweep and Storm, rasterized in a SINGLE band pass. fillArc cannot do this
// cleanly: a partial arc's angular boundary drops slivers that the dark bg
// remainder does not reliably re-cover, so bright pixels accumulate at the
// band's inner edge every lap (the growing specks); a full-ring bg clear
// removes them but strobes the bright comet. Coloring each band pixel exactly
// once from a continuous function has neither failure: no seams, no
// double-paint, and nothing stale survives a frame.
//
// Head laps clockwise; `d` is a pixel's angular distance behind it.
//   Sweep: brightness fades quadratically over the tail.
//   Storm: the rectangular Storm's bolt model in polar coordinates - the pixel's
//          arc length along the rim is the perimeter coordinate q, its distance
//          from the outer radius is the band row t. Two bolts pick a random row
//          per 8 px of rim and jump every segment, giving jagged streaks that
//          run parallel to the edge with a bright core, a dim halo and a
//          linearly decaying tail. Reseeded ~20 Hz so they crackle.
static void drawBandRing(lgfx::LovyanGFX& gfx, int16_t cx, int16_t cy,
                         int16_t ir, int16_t orr, uint16_t bg, bool rainbow,
                         uint16_t baseColor, uint16_t hueShift,
                         int head, int tail, uint8_t fade,
                         bool storm, uint32_t stormSeed) {
  static uint16_t rowBuf[248];
  const int32_t orr2 = (int32_t)orr * orr;
  const int32_t ir2  = (int32_t)ir * ir;
  // Row boundaries as squared radii: band row t covers [orr-t-1 .. orr-t], so a
  // pixel's row is the first t whose inner bound its r^2 still clears. Cheaper
  // than an isqrt per pixel.
  const int16_t T = (int16_t)(orr - ir);
  int32_t rowR2[GLOW_RING_T + 1];
  for (int16_t k = 0; k <= T && k <= GLOW_RING_T; k++) {
    int32_t rad = orr - k;
    rowR2[k] = rad * rad;
  }
  // Arc length per degree at the band's mid radius, x1000: q = a * degToPx.
  const int32_t degToPx1000 = ((int32_t)(ir + orr) * 8727) / 1000;  // (pi/360)*1e6
  const bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);   // rowBuf is native LE; panel wants byte-swapped
  for (int16_t y = (int16_t)(cy - orr); y <= (int16_t)(cy + orr); y++) {
    int32_t dy  = y - cy;
    int32_t dy2 = dy * dy;
    if (dy2 > orr2) continue;
    int16_t dxo = (int16_t)isqrt32((uint32_t)(orr2 - dy2));
    int16_t dxi = (dy2 < ir2) ? (int16_t)isqrt32((uint32_t)(ir2 - dy2)) : -1;
    // Up to two horizontal runs per row: [cx-dxo..cx-dxi] and [cx+dxi..cx+dxo];
    // one wide run [cx-dxo..cx+dxo] where the row clears the inner hole.
    for (int side = 0; side < 2; side++) {
      int16_t xs, xe;
      if (dxi < 0) { if (side) continue; xs = cx - dxo; xe = cx + dxo; }
      else if (side == 0) { xs = cx - dxo; xe = cx - dxi; }
      else                { xs = cx + dxi; xe = cx + dxo; }
      int len = 0;
      // The angle changes monotonically along a run, so the bolt hashes only
      // need recomputing when the 8 px rim segment changes.
      int32_t  lastSeg = INT32_MIN;
      uint32_t h1 = 0, h2 = 0;
      for (int16_t x = xs; x <= xe; x++) {
        int dx = (int)(x - cx);
        int a = iatan2deg((int)dy, dx);
        int d = head - a; if (d < 0) d += 360;
        uint16_t px;
        if (d > tail) {
          px = bg;
        } else if (!storm) {
          uint32_t f = 255 - (uint32_t)d * 255 / (uint32_t)tail;  // 255 at head
          uint32_t br = (f * f) >> 8;
          br = (br * fade) >> 8;
          uint16_t col = rainbow ? hueToRgb565((uint16_t)((a + hueShift) % 360))
                                 : baseColor;
          px = blend565(col, bg, (uint8_t)br);
        } else {
          int32_t seg = ((int32_t)a * degToPx1000 / 1000) >> 3;
          if (seg != lastSeg) {
            lastSeg = seg;
            h1 = ((uint32_t)seg * 2654435761u) ^ stormSeed;
            h1 ^= h1 >> 15; h1 *= 0x85EBCA6Bu; h1 ^= h1 >> 13;
            h2 = ((uint32_t)seg * 0x9E3779B9u) ^ (stormSeed * 3u);
            h2 ^= h2 >> 15; h2 *= 0x85EBCA6Bu; h2 ^= h2 >> 13;
          }
          int32_t r2 = (int32_t)dx * dx + dy2;
          int16_t t = 0;
          while (t < T - 1 && r2 < rowR2[t + 1]) t++;
          int b1 = t - (int)(h1 % (uint32_t)T); if (b1 < 0) b1 = -b1;
          int b2 = t - (int)(h2 % (uint32_t)T); if (b2 < 0) b2 = -b2;
          uint32_t nearHash = (b1 <= b2) ? h1 : h2;
          int dmin = (b1 <= b2) ? b1 : b2;
          uint32_t bolt = (dmin == 0) ? 255u : (dmin == 1) ? 130u : 20u;
          uint32_t tailA = (uint32_t)(tail - d) * 255 / (uint32_t)tail;
          uint32_t br = (tailA * bolt) >> 8;
          br = (br * fade) >> 8;
          uint16_t col = rainbow ? hueToRgb565((uint16_t)(nearHash % 360))
                                 : baseColor;
          px = blend565(col, bg, (uint8_t)br);
        }
        rowBuf[len++] = px;
      }
      if (len) gfx.pushImage(xs, y, len, 1, rowBuf);
    }
  }
  gfx.setSwapBytes(oldSwap);
}

// The glow owns an annular band at the panel rim. Solid fillArc wedges only -
// no per-pixel gradient like the rectangular strips - so each style is a small
// number of fills per frame. The band is fully repainted every frame: it owns
// its pixels while active, whatever the base screen drew under it.
static void drawBand(lgfx::LovyanGFX& gfx, unsigned long now, uint8_t fade) {
  const int16_t w   = (int16_t)gfx.width();
  const int16_t h   = (int16_t)gfx.height();
  const int16_t cx  = w / 2;
  const int16_t cy  = h / 2;
  const int16_t orr = (int16_t)(w / 2 - GLOW_RING_MARGIN);
  const int16_t ir  = (int16_t)(orr - GLOW_RING_T);
  const uint16_t bg = dispSettings.bgColor;

  const bool rainbow = (activeEvent == GLOW_EV_FINISH) && (dispSettings.glowMode == 2);
  const uint16_t baseColor = (activeEvent == GLOW_EV_FAILED) ? CLR_RED
                           : (activeEvent == GLOW_EV_ERROR)  ? activeColor
                                                             : dispSettings.glowColor;
  const uint8_t style = dispSettings.glowStyle;  // 0 Sweep, 1 Pulse, 2 Storm
  const uint16_t hueShift = (uint16_t)((now / 16) % 360);

  gfx.startWrite();
  if (style == 1) {
    // Pulse: the whole ring breathes. One full-circle fill.
    float ph = (float)(now % GLOW_PULSE_PERIOD_MS) / (float)GLOW_PULSE_PERIOD_MS;
    uint8_t pulseA = (uint8_t)(40.0f + 215.0f * (0.5f - 0.5f * cosf(ph * 2.0f * (float)M_PI)));
    uint8_t a = (uint8_t)(((uint16_t)pulseA * fade) >> 8);
    uint16_t col = rainbow ? hueToRgb565(hueShift) : baseColor;
    gfx.fillArc(cx, cy, ir, orr, 0.0f, 360.0f, blend565(col, bg, a));
  } else {
    // Sweep and Storm: single-pass band rasterizer (see drawBandRing) - no
    // fillArc, so no angular-seam specks and no whole-ring strobe. Both are the
    // same travelling comet window; Storm shatters the tail into bolts. Head
    // laps every LAP_MS.
    // On the episode's first frame, wipe the whole band once with a full-circle
    // fillArc: the finished screen painted a gold rim under our band, and the
    // sweep's dark remainder must not let it peek through. A one-shot wipe (not
    // per frame) leaves nothing gold to show, without the per-frame strobe.
    if (primeBand) gfx.fillArc(cx, cy, ir, orr, 0.0f, 360.0f, bg);
    int head = (int)((now % GLOW_SWEEP_LAP_MS) * 360UL / GLOW_SWEEP_LAP_MS);
    drawBandRing(gfx, cx, cy, ir, orr, bg, rainbow, baseColor, hueShift,
                 head, GLOW_SWEEP_TAIL_DEG, fade, style == 2,
                 (uint32_t)(now / 50) * 0x9E3779B9u);
  }
  primeBand = false;  // one-shot: consumed on the first frame of the episode
  gfx.endWrite();
}

// Repaint the ring band to the background so the cleanup repaint (which redraws
// the gold rim / progress ring on top) starts from a clean base.
static void clearBand(lgfx::LovyanGFX& gfx) {
  const int16_t w   = (int16_t)gfx.width();
  const int16_t h   = (int16_t)gfx.height();
  const int16_t orr = (int16_t)(w / 2 - GLOW_RING_MARGIN);
  const int16_t ir  = (int16_t)(orr - GLOW_RING_T);
  gfx.startWrite();
  gfx.fillArc(w / 2, h / 2, ir, orr, 0.0f, 360.0f, dispSettings.bgColor);
  gfx.endWrite();
}

// ---------------------------------------------------------------------------
//  Renderer: rectangular border strips (all non-round layouts)
// ---------------------------------------------------------------------------
#else

// Widest supported panel dimension (320x480 layouts).
static uint16_t lineBuf[480];

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
                           : (activeEvent == GLOW_EV_ERROR)  ? activeColor
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

#endif // DISPLAY_IS_ROUND

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
    activeColor = latchColor[slot];
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
