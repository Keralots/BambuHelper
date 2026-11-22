// Touch coordinates -> screen space, and the touch test screen state.
#include "touch_zones.h"

#if HAS_TOUCH_ZONES

#include "display_ui.h"
#include "button.h"

void touchRawToScreen(int16_t rx, int16_t ry, int16_t* sx, int16_t* sy) {
  auto& d = displayDevice();
  const uint8_t r = d.getRotation() & 3;
  const int32_t W = d.width(), H = d.height();          // rotated
  const int32_t nw = (r & 1) ? H : W, nh = (r & 1) ? W : H;  // panel native
  int32_t x = rx, y = ry;
  // Optional linear calibration: raw controller span -> full panel axis.
#if TOUCH_SWAP_XY
  const int32_t spanX = nh, spanY = nw;
#else
  const int32_t spanX = nw, spanY = nh;
#endif
#if defined(TOUCH_CAL_X_MIN) && defined(TOUCH_CAL_X_MAX)
  x = (x - TOUCH_CAL_X_MIN) * (spanX - 1) / (TOUCH_CAL_X_MAX - TOUCH_CAL_X_MIN);
#endif
#if defined(TOUCH_CAL_Y_MIN) && defined(TOUCH_CAL_Y_MAX)
  y = (y - TOUCH_CAL_Y_MIN) * (spanY - 1) / (TOUCH_CAL_Y_MAX - TOUCH_CAL_Y_MIN);
#endif
  (void)spanX; (void)spanY;
#if TOUCH_SWAP_XY
  { int32_t t = x; x = y; y = t; }
#endif
#if TOUCH_MIRROR_X
  x = nw - 1 - x;
#endif
#if TOUCH_MIRROR_Y
  y = nh - 1 - y;
#endif
  x = constrain(x, 0, nw - 1);
  y = constrain(y, 0, nh - 1);
  // Same native -> rotated transform LovyanGFX applies to its own touch drivers.
  if (r & 1) { int32_t t = x; x = y; y = t; }
  if (r & 2) x = W - 1 - x;
  if (r == 1 || r == 2) y = H - 1 - y;
  *sx = (int16_t)x;
  *sy = (int16_t)y;
}

// --- Hit zones -------------------------------------------------------------------
struct TouchZone { int16_t x, y, w, h; uint8_t id, arg; };
static const uint8_t TZ_MAX = 24;
static TouchZone tzList[TZ_MAX];
static uint8_t   tzCount = 0;

void touchZonesClear() { tzCount = 0; }

void touchZoneSet(uint8_t id, uint8_t arg, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (id == TZ_NONE || w <= 0 || h <= 0) return;
  TouchZone* z = nullptr;
  for (uint8_t i = 0; i < tzCount; i++)
    if (tzList[i].id == id && tzList[i].arg == arg) { z = &tzList[i]; break; }
  if (!z) {
    if (tzCount >= TZ_MAX) return;
    z = &tzList[tzCount++];
  }
  *z = {x, y, w, h, id, arg};
}

bool touchZoneAt(int16_t x, int16_t y, uint8_t* id, uint8_t* arg) {
  // Newest first: a zone registered later is drawn on top.
  for (int i = tzCount - 1; i >= 0; i--) {
    const TouchZone& z = tzList[i];
    if (x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h) {
      *id = z.id; *arg = z.arg;
      return true;
    }
  }
  return false;
}

bool touchZoneOfPress(uint8_t* id, uint8_t* arg) {
  int16_t rx, ry, x, y;
  if (!buttonPressRaw(&rx, &ry)) return false;
  touchRawToScreen(rx, ry, &x, &y);
  return touchZoneAt(x, y, id, arg);
}

// --- Touch test screen state ----------------------------------------------------
static const uint32_t TOUCH_TEST_IDLE_MS = 180000;
static const uint32_t TOUCH_TEST_EXIT_MS = 2000;   // finger held still this long closes it
static const int16_t  TOUCH_TEST_STILL_PX = 12;
static const uint8_t  TT_MAX = 8;

static bool         ttWanted = false;
static uint32_t     ttActivityMs = 0;
static TouchTestTap ttTaps[TT_MAX];
static uint8_t      ttCount = 0, ttHead = 0;   // ring, ttHead = next write
static int16_t      ttMinX, ttMaxX, ttMinY, ttMaxY;  // raw extents seen since open
static bool         ttAnchored = false;
static int16_t      ttAnchorX, ttAnchorY;
static uint32_t     ttAnchorMs;

void touchTestRequest(bool on) {
  ttWanted = on;
  ttActivityMs = millis();
  if (on) {
    ttCount = ttHead = 0;
    ttMinX = ttMinY = INT16_MAX;
    ttMaxX = ttMaxY = INT16_MIN;
    ttAnchored = false;
  }
}

bool touchTestWanted() {
  if (ttWanted && millis() - ttActivityMs > TOUCH_TEST_IDLE_MS) ttWanted = false;
  return ttWanted;
}

bool touchTestActivity(bool held) {
  int16_t rx, ry;
  if (!held || !buttonTouchRaw(&rx, &ry)) { ttAnchored = false; return false; }
  const uint32_t now = millis();
  ttActivityMs = now;
  if (rx < ttMinX) ttMinX = rx;
  if (rx > ttMaxX) ttMaxX = rx;
  if (ry < ttMinY) ttMinY = ry;
  if (ry > ttMaxY) ttMaxY = ry;
  if (!ttAnchored || abs(rx - ttAnchorX) > TOUCH_TEST_STILL_PX ||
      abs(ry - ttAnchorY) > TOUCH_TEST_STILL_PX) {
    ttAnchored = true; ttAnchorX = rx; ttAnchorY = ry; ttAnchorMs = now;
    return false;
  }
  return now - ttAnchorMs >= TOUCH_TEST_EXIT_MS;
}

void touchTestNoteTap() {
  int16_t rx, ry;
  if (!buttonPressRaw(&rx, &ry)) return;
  TouchTestTap& t = ttTaps[ttHead];
  t.rx = rx; t.ry = ry;
  touchRawToScreen(rx, ry, &t.x, &t.y);
  if (!touchZoneAt(t.x, t.y, &t.zone, &t.arg)) t.zone = t.arg = 0;
  ttHead = (ttHead + 1) % TT_MAX;
  if (ttCount < TT_MAX) ttCount++;
  ttActivityMs = millis();
}

uint8_t touchTestTaps(TouchTestTap* out, uint8_t max) {
  uint8_t n = (ttCount < max) ? ttCount : max;
  for (uint8_t i = 0; i < n; i++) out[i] = ttTaps[(ttHead + TT_MAX - 1 - i) % TT_MAX];
  return n;
}

String touchTestJson() {
  auto& d = displayDevice();
  String s;
  s.reserve(96 + TT_MAX * 40);
  s += "{\"active\":";
  s += touchTestWanted() ? "true" : "false";
  s += ",\"rot\":";  s += d.getRotation() & 3;
  s += ",\"w\":";    s += d.width();
  s += ",\"h\":";    s += d.height();
  if (ttMinX <= ttMaxX) {
    s += ",\"raw\":{\"minX\":"; s += ttMinX; s += ",\"maxX\":"; s += ttMaxX;
    s += ",\"minY\":"; s += ttMinY; s += ",\"maxY\":"; s += ttMaxY; s += '}';
  }
  s += ",\"taps\":[";
  TouchTestTap t[TT_MAX];
  uint8_t n = touchTestTaps(t, TT_MAX);
  for (uint8_t i = 0; i < n; i++) {
    if (i) s += ',';
    s += "{\"x\":"; s += t[i].x; s += ",\"y\":"; s += t[i].y;
    s += ",\"rx\":"; s += t[i].rx; s += ",\"ry\":"; s += t[i].ry;
    s += ",\"zone\":"; s += t[i].zone; s += ",\"arg\":"; s += t[i].arg; s += '}';
  }
  s += "]}";
  return s;
}

#endif  // HAS_TOUCH_ZONES
