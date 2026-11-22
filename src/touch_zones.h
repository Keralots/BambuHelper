#ifndef TOUCH_ZONES_H
#define TOUCH_ZONES_H

#include <Arduino.h>
#include "config.h"

#if HAS_TOUCH_ZONES

// Raw controller coordinates -> screen coordinates at the current rotation.
void touchRawToScreen(int16_t rx, int16_t ry, int16_t* sx, int16_t* sy);

// --- Hit zones ---------------------------------------------------------------
// Renderers register tappable regions while drawing, in screen coordinates.
// updateDisplay() clears the set at the start of every forceRedraw frame, so a
// renderer registers its zones on its forceRedraw pass. Same (id,arg) = update.
enum TouchZoneId : uint8_t {
  TZ_NONE = 0,
  TZ_TEST_TARGET,   // touch test screen + marks, arg = 0..4
  TZ_AMS_UNIT,      // an AMS unit on the print / idle screen, arg = unit index
  TZ_AMS_GAUGE,     // an AMS gauge tile, arg = gauge type (amsUnitOfGauge -> unit)
  TZ_AMS_TRAY,      // one tray row of a unit, arg = unit << 5 | row
};

void touchZonesClear();
void touchZonesClearId(uint8_t id);   // a renderer about to re-register its set
void touchZoneSet(uint8_t id, uint8_t arg, int16_t x, int16_t y, int16_t w, int16_t h);
bool touchZoneAt(int16_t x, int16_t y, uint8_t* id, uint8_t* arg);
// Zone under the latest touchscreen press (false: no coordinates / no zone).
bool touchZoneOfPress(uint8_t* id, uint8_t* arg);

// Touch test screen (SCREEN_TOUCH_TEST): opened from the portal, closed by a
// 2 s still hold, the portal, or 3 min without a touch.
void touchTestRequest(bool on);
bool touchTestWanted();
bool touchTestActivity(bool held); // every loop; true = still hold, close it
void touchTestNoteTap();           // press edge: record where it landed
String touchTestJson();

struct TouchTestTap { int16_t x, y, rx, ry; uint8_t zone, arg; };
uint8_t touchTestTaps(TouchTestTap* out, uint8_t max);  // newest first

#endif  // HAS_TOUCH_ZONES
#endif  // TOUCH_ZONES_H
