// BambuHelper-Mono: display layer for the SH1106 128x64 over I2C.
// Style copied from ESP-Spotify-OLED:
// Adafruit_SH1106G, FreeMonoBold9pt7b for the title, default font for the
// rest, bar via drawRect+fillRect, hand-drawn bitmap icons, transliterated names.
// Only exists with BOARD_IS_MONO (mono-c3 env).
//
// Data model (plan step 1): there is NO dedicated PrinterState struct.
// BambuState (include/bambu_state.h) already carries every requested field
// and the MQTT parser (src/bambu_mqtt.cpp) only updates the fields present
// in each partial push, keeping the rest:
//
//   gcodeState/gcodeStateId <- gcode_state, jobName=subtask_name,
//   percent=mc_percent, remainingMin=mc_remaining_time (minutes),
//   layer/totalLayers=layer_num/total_layer_num,
//   nozzle(+Target)=nozzle_temper(+_target), bed(+Target)=bed_temper(+_target),
//   errorCode=print_error, connected=MQTT connection state.
//
// Tunable constants (all overridable with -D in platformio.ini):
//   MONO_MAX_CHAR_TITLE_PER_LINE  2-line title wrap width (def. 11, as in
//                               Spotify); printing/Done wrap to 2 lines with
//                               "..." on overflow, prepare/failed/offline
//                               show a single row that is static while it
//                               fits 128 px and marquee otherwise
//   MONO_HOT_THRESHOLD_C          nozzle HOT warning in idle (def. 50 C)
//   MONO_REFRESH_MS               static refresh (def. 1000 ms)
//   MONO_MARQUEE_MS               marquee step (~10 fps, def. 100 ms)
//   MONO_END_TIME_ALT_MS          remaining/ETA alternation period (def. 5000 ms)
//   MONO_MOCK                     1 = "mock ..." commands over Serial
//   MONO_MOCK_AUTO                1 = boot into mock cycle mode
//
// Runtime row toggles (web portal Print screen > Mono OLED, monoSettings in
// settings.h, defaults = tuned mono layout: temps on Done/HotIdle/StatusIdle
// off, the rest on, weather off): showLayer, showBar, showTimePrinting,
// showTimeAlt, showTempsDone, showClockIdle, showStatusIdle (X/circle on the
// clock), showHotIdle, showSplash (boot screen held ~3 s; off = straight to
// clock unless WiFi failed), weatherEnabled + weatherKey/weatherQuery (idle
// clock icon + temp), oledContrast (SH1106 hardware contrast 0-255, instant).
#pragma once

#include <Arduino.h>

struct BambuState;  // include/bambu_state.h

// --- Tunable constants -------------------------------------------------------
#ifndef MONO_MAX_CHAR_TITLE_PER_LINE
#define MONO_MAX_CHAR_TITLE_PER_LINE 11
#endif
#ifndef MONO_HOT_THRESHOLD_C
#define MONO_HOT_THRESHOLD_C 50
#endif
#ifndef MONO_REFRESH_MS
#define MONO_REFRESH_MS 1000
#endif
#ifndef MONO_MARQUEE_MS
#define MONO_MARQUEE_MS 100
#endif
#ifndef MONO_END_TIME_ALT_MS
#define MONO_END_TIME_ALT_MS 5000
#endif
#ifndef MONO_MOCK
#define MONO_MOCK 1
#endif
#ifndef MONO_BOOT_HOLD_MS
#define MONO_BOOT_HOLD_MS 3000
#endif

// Screens (step 4). Priority in monoSelectScreen():
// !connected > FAILED/print_error != 0 > FINISH > PAUSE > PREPARE > RUNNING > IDLE.
// Note: the printer keeps reporting FINISH after the print ends (OK on the
// printer does not publish IDLE), so the tick applies dpSettings.finishDisplayMins
// (default 3, 0 = keep forever): FINISH is shown for that window, then falls
// back to IDLE while the MQTT state is still FINISH.
enum MonoScreen : uint8_t {
  MONO_SCR_OFFLINE = 0,
  MONO_SCR_PREPARE,
  MONO_SCR_PRINTING,
  MONO_SCR_PAUSE,
  MONO_SCR_FINISH,
  MONO_SCR_FAILED,
  MONO_SCR_IDLE,
};

MonoScreen monoSelectScreen(const BambuState &st);
const char *monoScreenName(MonoScreen s);

// --- Helpers (step 2, no drawing except the draw* ones) -----------------------
String monoCleanJobName(const String &raw);
String monoCleanJobName(const char *raw);
String monoFormatRemaining(int minutes);  // <0 = no data -> "--"

// Drawing helpers (they use the internal oled; icons are XBM bitmaps, no font glyphs).
void monoDrawProgressBar(int y, int percent, bool blinkOn = false);  // drawRect+fillRect map(); blink hides the fill every ~500 ms
void monoDrawStatusIcon(int x, int y, MonoScreen s);   // triangle / pause / error / check
void monoDrawTitle(const String &clean);               // 1 row, static or marquee

// --- Screen API ----------------------------------------------------------------
void monoDisplayInit();
void monoShowBoot();  // helper IP + printer names with live [on]/[off],
                      // held ~3 s (setup mode: AP SSID on the header row)
void monoShowMessage(const char *line1, const char *line2);
void monoShowStatus(const BambuState &st);  // immediate render (compat)
void monoShowError(unsigned long code);

// Loop: call every iteration. Repaints only when the state changes or every
// MONO_REFRESH_MS; marquee/blink run at MONO_MARQUEE_MS (~10 fps).
// Honors monoDisplaySetEnabled(false) (idle display-off).
void monoDisplayTick();
void monoDisplaySetEnabled(bool on);
bool monoDisplayIsEnabled();
inline void oled_display(bool on) { monoDisplaySetEnabled(on); }

// --- Test mode (step 6) --------------------------------------------------------
// Serial 115200: "mock" (help), "mock off", "mock cycle", "mock <n>".
// Cases: 0 print 0% short, 1 print 50% ~20ch, 2 print long (2-line wrap),
// 3 pause, 4 done, 5 error, 6 cold idle, 7 hot idle, 8 offline.
// "mock w<code>" (e.g. "mock w10d", "mock w" clears) forces a demo weather
// icon + 21C on the idle clock without network, for icon review.
// With -D MONO_MOCK_AUTO=1 it boots into "cycle".
void monoMockHandleSerial();
bool monoMockActive();
