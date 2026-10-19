// BambuHelper-Mono: SH1106 128x64 over I2C with Adafruit GFX/SH110X.
// Style copied from the ESP-Spotify-OLED reference repo.
#ifdef BOARD_IS_MONO

#include "mono_display.h"
#include "mono_weather.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <time.h>

#include <stdio.h>

#ifndef OLED_SDA
#define OLED_SDA 8
#endif
#ifndef OLED_SCL
#define OLED_SCL 9
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3c
#endif
#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif
#ifndef OLED_RESET
#define OLED_RESET -1
#endif
#ifndef MONO_MOCK_AUTO
#define MONO_MOCK_AUTO 0
#endif

// Layout checked for overlaps (default font = 6x8 px cells).
// FreeMonoBold9pt7b caps: yOffset -10, height 11, so baseline y=N spans
// N-10..N (measured in the font file). Printing (Spotify style): title 2x11
// at baselines 12/27, info row at y=36 (%-layer), bar at y=46..51, bottom row
// at y=55 (stage left, time right).
// Prepare/failed/offline keep the airy rows: title baseline y=12,
// TEMP_Y=21 (icons 21..29, text 21..28), LAYER_Y=34 (34..41),
// BAR_Y=46 (46..51), BOTTOM_Y=56 (56..63).
static const int TITLE_SINGLE_Y = 12;
static const int TEMP_Y = 21;
static const int LAYER_Y = 34;
static const int BAR_Y = 46;
static const int BAR_H = 6;
static const int BOTTOM_Y = 56;

static Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

static bool s_enabled = true;
static bool s_booted = false;
static uint8_t s_appliedContrast = 255;  // last value sent via setContrast()
static unsigned long s_bootHoldUntilMs = 0;  // boot screen hold window
static unsigned long s_lastBootDrawMs = 0;   // throttle for live boot repaints
static void renderBootScreen();

// --- Transliteration (the equivalent of the reference repo's ----------------
// --- replaceSpecialCharacters: the GFX fonts ship no accents, so map to ASCII)
// Coverage is Latin-1/UTF-8 C3 only: Spanish/German/French-style diacritics
// map to their bare letter, mapped-away multi-byte falls through silently,
// and non-Latin scripts (Cyrillic, CJK...) are DROPPED by design - with no
// glyphs in FreeMonoBold the alternative would be unreadable tofu. A name
// written entirely in a non-Latin script shows as an empty title, which the
// status screens tolerate (the status text still carries the state).
static void deaccent(char *dst, size_t n, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j + 1 < n;) {
    unsigned char c = (unsigned char)src[i];
    if (c < 0x80) {
      dst[j++] = (char)c;
      i++;
    } else if (c == 0xC3 && src[i + 1]) {
      unsigned char d = (unsigned char)src[i + 1];
      char r = 0;
      if (d >= 0x80 && d <= 0x85) r = 'A';  // A accents
      else if (d == 0x87) r = 'C';         // C cedilla
      else if (d >= 0x88 && d <= 0x8B) r = 'E';
      else if (d >= 0x8C && d <= 0x8F) r = 'I';
      else if (d == 0x91) r = 'N';
      else if (d >= 0x92 && d <= 0x96) r = 'O';
      else if (d >= 0x99 && d <= 0x9C) r = 'U';
      else if (d == 0x9F) r = 's';
      else if (d >= 0xA0 && d <= 0xA5) r = 'a';
      else if (d == 0xA7) r = 'c';
      else if (d >= 0xA8 && d <= 0xAB) r = 'e';
      else if (d >= 0xAC && d <= 0xAF) r = 'i';
      else if (d == 0xB1) r = 'n';
      else if (d >= 0xB2 && d <= 0xB6) r = 'o';
      else if (d >= 0xB9 && d <= 0xBC) r = 'u';
      if (r) dst[j++] = r;
      i += 2;
    } else {
      i++;  // otro multibyte: se descarta
    }
  }
  dst[j] = 0;
}

static bool endsWith(const char *s, const char *suffix) {
  size_t sl = strlen(s), xl = strlen(suffix);
  return sl >= xl && strcmp(s + sl - xl, suffix) == 0;
}

// --- Step 2: helpers ----------------------------------------------------------
String monoCleanJobName(const char *raw) {
  if (!raw) return String("");
  char tmp[128];
  strlcpy(tmp, raw, sizeof(tmp));
  // Slicer suffixes: order matters (.gcode.3mf before .3mf).
  if (endsWith(tmp, ".gcode.3mf")) tmp[strlen(tmp) - 10] = 0;
  else if (endsWith(tmp, ".3mf")) tmp[strlen(tmp) - 4] = 0;
  else if (endsWith(tmp, ".gcode")) tmp[strlen(tmp) - 6] = 0;
  for (char *p = tmp; *p; p++)
    if (*p == '_') *p = ' ';
  char out[128];
  deaccent(out, sizeof(out), tmp);
  // Trim edge whitespace.
  String s(out);
  s.trim();
  return s;
}

String monoCleanJobName(const String &raw) { return monoCleanJobName(raw.c_str()); }

// Printer friendly name, transliterated + trimmed for the OLED (no suffix
// stripping: that is job-name only). "" when the slot has no name.
static String monoPrinterLabel(uint8_t slot) {
  if (slot >= MAX_PRINTERS) return String("");
  char out[32];
  deaccent(out, sizeof(out), printers[slot].config.name);
  String s(out);
  s.trim();
  return s;
}

String monoFormatRemaining(int minutes) {
  char buf[16];
  if (minutes < 0) return String("--");
  if (minutes < 60) {
    snprintf(buf, sizeof(buf), "- %dm", minutes);
  } else {
    snprintf(buf, sizeof(buf), "- %dh %02dm", minutes / 60, minutes % 60);
  }
  return String(buf);
}

void monoDrawProgressBar(int y, int percent, bool blinkOn) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  // Same bar as Spotify: drawRect + fillRect with map().
  oled.drawRect(0, y, 128, BAR_H, SH110X_WHITE);
  int fill = map(percent, 0, 100, 0, 126);
  if (blinkOn) fill = 0;  // pause blink: hide the fill every other frame
  oled.fillRect(0, y, fill, BAR_H, SH110X_WHITE);
}

void monoDrawStatusIcon(int x, int y, MonoScreen s) {
  switch (s) {
    case MONO_SCR_PRINTING:
    case MONO_SCR_PREPARE:
      // "Play" triangle.
      oled.fillTriangle(x, y, x, y + 7, x + 6, y + 3, SH110X_WHITE);
      break;
    case MONO_SCR_PAUSE:
      // Two vertical pause bars.
      oled.fillRect(x, y, 2, 8, SH110X_WHITE);
      oled.fillRect(x + 4, y, 2, 8, SH110X_WHITE);
      break;
    case MONO_SCR_FAILED:
      // Warning triangle + "!" from primitives (no glyphs).
      oled.drawTriangle(x + 3, y, x, y + 7, x + 7, y + 7, SH110X_WHITE);
      oled.drawFastVLine(x + 3, y + 2, 3, SH110X_WHITE);
      oled.drawPixel(x + 3, y + 6, SH110X_WHITE);
      break;
    case MONO_SCR_FINISH:
      // Check mark, two lines (single weight, inside the 8x8 box).
      oled.drawLine(x, y + 4, x + 3, y + 7, SH110X_WHITE);
      oled.drawLine(x + 3, y + 7, x + 8, y, SH110X_WHITE);
      break;
    case MONO_SCR_OFFLINE:
      // Disconnected "X".
      oled.drawLine(x, y, x + 7, y + 7, SH110X_WHITE);
      oled.drawLine(x + 7, y, x, y + 7, SH110X_WHITE);
      break;
    case MONO_SCR_IDLE:
    default:
      break;
  }
}

// --- Title with wrap / marquee ------------------------------------------------
static String s_mqText = "";
static int s_mqX = 0;
static unsigned long s_mqLastMs = 0;
static uint8_t s_mqHold = 0;  // hold ticks at each end
static bool s_marqueeActive = false;

static void marqueeReset(const String &t) {
  s_mqText = t;
  s_mqX = 0;
  s_mqLastMs = millis();
  s_mqHold = 8;  // opening hold so the start can be read
}

void monoDrawTitle(const String &clean) {
  oled.setFont(&FreeMonoBold9pt7b);
  // Static vs marquee is decided in PIXELS, not characters: FreeMonoBold9 is
  // ~7 px/char, so a 14-char title can still fit the 128 px row and scrolling
  // 2 px before snapping back reads as a glitch.
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(clean, 0, 0, &x1, &y1, &w, &h);
  if ((int)w <= 128) {
    // Fits: static single row.
    s_marqueeActive = false;
    oled.setCursor(0, TITLE_SINGLE_Y);
    oled.print(clean);
  } else {
    // Too wide: single-row horizontal marquee (never wraps to 2 rows).
    s_marqueeActive = true;
    if (s_mqText != clean) marqueeReset(clean);
    unsigned long now = millis();
    if (now - s_mqLastMs >= (unsigned long)MONO_MARQUEE_MS) {
      s_mqLastMs = now;
      if (s_mqHold > 0) {
        s_mqHold--;
      } else {
        s_mqX -= 3;
        if (s_mqX + (int)w <= 128) {
          // End visible: hold, then back to the start.
          s_mqX = 0;
          s_mqHold = 8;
        }
      }
    }
    oled.setCursor(s_mqX, TITLE_SINGLE_Y);
    oled.print(clean);
  }
  oled.setFont();
  oled.setTextSize(1);
}

// --- Step 4: screen selection --------------------------------------------------
const char *monoScreenName(MonoScreen s) {
  switch (s) {
    case MONO_SCR_OFFLINE: return "OFFLINE";
    case MONO_SCR_PREPARE: return "PREPARE";
    case MONO_SCR_PRINTING: return "PRINTING";
    case MONO_SCR_PAUSE: return "PAUSE";
    case MONO_SCR_FINISH: return "FINISH";
    case MONO_SCR_FAILED: return "FAILED";
    case MONO_SCR_IDLE:
    default: return "IDLE";
  }
}

MonoScreen monoSelectScreen(const BambuState &st) {
  if (!st.connected) return MONO_SCR_OFFLINE;
#if HAS_HMS_UI
  if (st.printError != 0) return MONO_SCR_FAILED;
#endif
  if (st.gcodeStateId == GCODE_FAILED) return MONO_SCR_FAILED;
  switch (st.gcodeStateId) {
    case GCODE_FINISH: return MONO_SCR_FINISH;
    case GCODE_PAUSE: return MONO_SCR_PAUSE;
    case GCODE_PREPARE: return MONO_SCR_PREPARE;
    case GCODE_RUNNING: return MONO_SCR_PRINTING;
    case GCODE_IDLE:
    case GCODE_UNKNOWN:
    case GCODE_OTHER:
    default: return MONO_SCR_IDLE;
  }
}

// --- FINISH hold -> Idle -----------------------------------------------------
// The printer keeps reporting gcode_state=FINISH after the print ends (pressing
// OK on the printer does NOT publish IDLE), so without a timeout the mono
// screen stays on Done forever. Big screens solve this with
// dpSettings.finishDisplayMins (default 3, 0 = keep forever): FINISHED is shown
// for that window and then the screen falls back to Idle/Clock even though the
// MQTT state is still FINISH. Mirror that here.
static unsigned long s_finishSinceMs = 0;
static bool s_wasFinish = false;

static unsigned long monoFinishHoldMs() {
  if (dpSettings.finishDisplayMins == 0) return 0;  // keep Done forever
  return (unsigned long)dpSettings.finishDisplayMins * 60000UL;
}

static MonoScreen monoEffectiveScreen(MonoScreen raw) {
  unsigned long now = millis();
  if (raw == MONO_SCR_FINISH) {
    if (!s_wasFinish) {
      s_wasFinish = true;
      s_finishSinceMs = now;
    }
    unsigned long hold = monoFinishHoldMs();
    if (hold > 0 && (long)(now - s_finishSinceMs) >= (long)hold) {
      return MONO_SCR_IDLE;  // expired: show Idle while printer still says FINISH
    }
    return MONO_SCR_FINISH;
  }
  s_wasFinish = false;
  return raw;
}

// --- Format helpers -----------------------------------------------------------
static int tempInt(float v) { return (int)(v + 0.5f); }

// "215/220", or "215" when the target is 0.
static void fmtTempVal(char *dst, size_t n, float cur, float target) {
  if (target < 0.5f)
    snprintf(dst, n, "%d", tempInt(cur));
  else
    snprintf(dst, n, "%d/%d", tempInt(cur), tempInt(target));
}

// --- Temp icons (8x9 XBM bitmaps, no font glyphs) ---------------------------------
// Hand-drawn like the reference repo's weather_icons.h. XBM order: bit0 of
// each byte is the leftmost pixel, one byte per row (width 8).
static const unsigned char NZL_BMP[] PROGMEM = {
    0x3C, 0x3C, 0xFF, 0xFF, 0xFF, 0x7E, 0x7E, 0x3C, 0x18,
};
static const unsigned char BED_BMP[] PROGMEM = {
    0x12, 0x24, 0x12, 0x00, 0xFF, 0xFF, 0x81, 0x81, 0x81,
};
#define TEMP_ICON_W 8
#define TEMP_ICON_H 9

static void drawNozzleIcon(int x, int y) {
  oled.drawBitmap(x, y, NZL_BMP, TEMP_ICON_W, TEMP_ICON_H, SH110X_WHITE);
}

static void drawBedIcon(int x, int y) {
  oled.drawBitmap(x, y, BED_BMP, TEMP_ICON_W, TEMP_ICON_H, SH110X_WHITE);
}

// Temps row: [nozzle icon] 215/220  [bed icon] 60/60, default font.
static void drawTempsRow(int y, float nzl, float nzlT, float bed, float bedT) {
  char nb[16], bb[16];
  fmtTempVal(nb, sizeof(nb), nzl, nzlT);
  fmtTempVal(bb, sizeof(bb), bed, bedT);
  int16_t x1, y1;
  uint16_t w, h;
  int x = 0;
  drawNozzleIcon(x, y);
  x += 10;
  oled.setCursor(x, y);
  oled.print(nb);
  oled.getTextBounds(String(nb), 0, 0, &x1, &y1, &w, &h);
  x += (int)w + 6;
  drawBedIcon(x, y);
  x += 11;
  oled.setCursor(x, y);
  oled.print(bb);
}

static bool ntpOk() { return time(nullptr) > (time_t)NTP_SYNCED_EPOCH; }

// Printer liveness for the idle corner icon: connected flag + fresh data.
// Mirrors the main state machine's freshness rule (BAMBU_STALE_TIMEOUT, x5 in
// cloud mode where the broker outlives the printer).
static bool monoPrinterLive(const BambuState &st) {
  if (!st.connected) return false;
  if (st.lastUpdate == 0) return false;
  unsigned long staleMs = (unsigned long)BAMBU_STALE_TIMEOUT;
  if (activePrinterIndex < MAX_PRINTERS &&
      isCloudMode(printers[activePrinterIndex].config.mode))
    staleMs *= 5UL;
  return (millis() - st.lastUpdate) <= staleMs;
}

// 24-hour checkbox (Clock section) also governs the mono times: 12h mode
// drops the leading zero and reports AM/PM through *pm (NULL-tolerant).
static int monoClockHour(int h24, bool *pm) {
  if (netSettings.use24h) {
    if (pm) *pm = false;
    return h24;
  }
  if (pm) *pm = (h24 >= 12);
  int h = h24 % 12;
  return (h == 0) ? 12 : h;
}

static void fmtClockMode(char *dst, size_t n, bool *pm) {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  bool isPM = false;
  int h = monoClockHour(tmv.tm_hour, &isPM);
  if (pm) *pm = isPM;
  if (netSettings.use24h)
    snprintf(dst, n, "%02d:%02d", h, tmv.tm_min);
  else
    snprintf(dst, n, "%d:%02d", h, tmv.tm_min);
}

// --- One render per screen (all end with display()) ----------------------------
static void wrapJobName2Lines(const String &clean, String &l1, String &l2);

// Spotify-style printing screen (ESP-Spotify-OLED reference): job title in
// FreeMonoBold9pt7b wrapped to 2x11 lines (no marquee), two default-font info
// lines (layer, plain-text temps), bar at y=50, status glyph + % bottom-left
// with the remaining/end time bottom-right.
static const int PRINT_TITLE_Y1 = 12;
static const int PRINT_TITLE_Y2 = 27;
static const int PRINT_INFO_Y1 = 36;
static const int PRINT_BAR_Y = 46;
static const int PRINT_BOTTOM_Y = 55;

// Short remaining time: "-53m" / "-1h05m", "--" with no data.
static String fmtRemainingShort(int minutes) {
  char buf[16];
  if (minutes < 0) return String("--");
  if (minutes < 60) {
    snprintf(buf, sizeof(buf), "-%dm", minutes);
  } else {
    snprintf(buf, sizeof(buf), "-%dh%02dm", minutes / 60, minutes % 60);
  }
  return String(buf);
}

// ETA = local time (NTP + DST-aware timezone) + remaining minutes.
// 24h format (no "ETA" word), "+Nd" when it crosses midnight, "--:--" with no clock.
static String fmtETA(const BambuState &st) {
  if (!ntpOk() || st.remainingMinutes < 0) return String("--:--");
  time_t now = time(nullptr);
  struct tm nowtm, endtm;
  localtime_r(&now, &nowtm);
  time_t end = now + (time_t)st.remainingMinutes * 60;
  localtime_r(&end, &endtm);
  int dayDiff = (endtm.tm_yday - nowtm.tm_yday) +
                (endtm.tm_year - nowtm.tm_year) * 365;
  if (dayDiff < 0) dayDiff = 0;
  char buf[16];
  if (dayDiff > 0)
    snprintf(buf, sizeof(buf), "%02d:%02d+%d", endtm.tm_hour, endtm.tm_min,
             dayDiff);
  else
    snprintf(buf, sizeof(buf), "%02d:%02d", endtm.tm_hour, endtm.tm_min);
  return String(buf);
}

// Current process text (row below the bar, left). Single map.
static const char* stageToText(const BambuState &st) {
  switch (st.gcodeStateId) {
    case GCODE_PAUSE:   return "[Paused]";
    case GCODE_FINISH:  return "[Finished]";
    case GCODE_FAILED:  return "[Cancelled]";
    default: break;
  }
  if (st.gcodeStateId == GCODE_RUNNING ||
      st.gcodeStateId == GCODE_PREPARE) {
    switch (st.stgCur) {
      case 1:   case 40: case 47: case 48: return "Auto bed leveling";
      case 2:   return "Heating bed";
      case 7:   return "Heating nozzle";
      case 49:  return "Heating chamber";
      case 14:  return "Cleaning nozzle";
      case 8:   case 19: case 12: case 18: return "Calibrating";
      case 4:   case 22: case 24: return "Changing filament";
      case 0:   return "Printing";
      default: {
        const char* lbl = stgCurLabel(st.stgCur);
        return lbl ? lbl : "Printing";
      }
    }
  }
  static char buf[16];
  const char* s = st.gcodeState;
  if (!s || !s[0]) return "Unknown";
  buf[0] = (char)toupper((unsigned char)s[0]);
  size_t i;
  for (i = 1; s[i] && i < sizeof(buf) - 1; i++)
    buf[i] = (char)tolower((unsigned char)s[i]);
  buf[i] = 0;
  return buf;
}

static void renderPrinting(const BambuState &st, MonoScreen scr, bool blinkOn) {
  oled.clearDisplay();
  s_marqueeActive = false;  // 2-line wrap never overflows: no marquee here
  bool hasData = st.lastPrintDataMs != 0;
  int pct = hasData ? (int)st.progress : 0;
  bool isPause = (scr == MONO_SCR_PAUSE);

  // ── Title: 2 lines, unchanged from the previous layout ──
  String l1, l2;
  wrapJobName2Lines(monoCleanJobName(jobDisplayName(st)), l1, l2);
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, PRINT_TITLE_Y1);
  oled.print(l1);
  if (l2.length() > 0) {
    oled.setCursor(0, PRINT_TITLE_Y2);
    oled.print(l2);
  }
  oled.setFont();
  oled.setTextSize(1);

  // ── Row above the bar: % on the left | layer/total on the right ──
  oled.setCursor(0, PRINT_INFO_Y1);
  if (!hasData)
    oled.print(F("--%"));
  else {
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    oled.print(pctBuf);
  }
  if (monoSettings.showLayer) {
    char layerBuf[16];
    if (!hasData || st.totalLayers == 0)
      snprintf(layerBuf, sizeof(layerBuf), "--/--");
    else
      snprintf(layerBuf, sizeof(layerBuf), "%u/%u", st.layerNum, st.totalLayers);
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(layerBuf, 0, 0, &x1, &y1, &w, &h);
    oled.setCursor(128 - (int)w, PRINT_INFO_Y1);
    oled.print(layerBuf);
  }

  // ── Progress bar (same style: 1 px border, BAR_H thick, map() fill);
  //     when paused the fill blinks every ~500 ms (blinkOn, non-blocking) ──
  if (monoSettings.showBar) monoDrawProgressBar(PRINT_BAR_Y, pct,
                                                isPause && blinkOn);

  // ── Row below the bar: current process left | remaining time / ETA right ──
  //     with showTimeAlt they alternate every 5 s. With showTimePrinting off
  //     the row has no time on the right and the process uses the full width.
  const char* stageTxt = stageToText(st);
  int16_t x1, y1;
  uint16_t etaW, etaH;
  int etaX = 128;
  char bottomRightBuf[16];
  if (!monoSettings.showTimePrinting) {
    bottomRightBuf[0] = 0;
  } else if (!hasData || st.gcodeStateId == GCODE_FAILED ||
             st.gcodeStateId == GCODE_FINISH) {
    snprintf(bottomRightBuf, sizeof(bottomRightBuf), "--:--");
  } else if (monoSettings.showTimeAlt &&
             (int)((millis() / (unsigned long)MONO_END_TIME_ALT_MS) % 2) == 1) {
    snprintf(bottomRightBuf, sizeof(bottomRightBuf), "%s",
             fmtRemainingShort((int)st.remainingMinutes).c_str());
  } else {
    snprintf(bottomRightBuf, sizeof(bottomRightBuf), "%s", fmtETA(st).c_str());
  }
  oled.getTextBounds(bottomRightBuf, 0, 0, &x1, &y1, &etaW, &etaH);
  etaX = 128 - (int)etaW;
  int stageMaxW = etaX - 4;
  char stageBuf[48];
  if (stageMaxW > 0) {
    int16_t sx1, sy1;
    uint16_t sw, sh;
    oled.getTextBounds(stageTxt, 0, 0, &sx1, &sy1, &sw, &sh);
    if ((int)sw > stageMaxW) {
      size_t len = strlen(stageTxt);
      size_t cut = len;
      while (cut > 0) {
        char tmp[48];
        size_t cp = (cut < sizeof(tmp) - 4) ? cut : sizeof(tmp) - 4;
        memcpy(tmp, stageTxt, cp);
        tmp[cp] = 0;
        strlcat(tmp, "...", sizeof(tmp));
        oled.getTextBounds(tmp, 0, 0, &sx1, &sy1, &sw, &sh);
        if ((int)sw <= stageMaxW) break;
        cut--;
      }
      char tmp[48];
      size_t cp = (cut < sizeof(tmp) - 4) ? cut : sizeof(tmp) - 4;
      memcpy(tmp, stageTxt, cp);
      tmp[cp] = 0;
      strlcat(tmp, "...", sizeof(tmp));
      strlcpy(stageBuf, tmp, sizeof(stageBuf));
    } else {
      strlcpy(stageBuf, stageTxt, sizeof(stageBuf));
    }
  } else {
    stageBuf[0] = 0;
  }
  oled.setCursor(0, PRINT_BOTTOM_Y);
  oled.print(stageBuf);
  oled.setCursor(etaX, PRINT_BOTTOM_Y);
  oled.print(bottomRightBuf);

  oled.display();
}

static void wrapJobName2Lines(const String &clean, String &l1, String &l2) {
  const int maxLine = MONO_MAX_CHAR_TITLE_PER_LINE;  // 11 with the large font
  String t = clean;
  t.trim();
  if ((int)t.length() <= maxLine) {
    l1 = t;
    l2 = "";
    return;
  }
  // Greedy word wrap for the first line.
  int n = t.length();
  int cut = -1;
  int lastSpace = -1;
  for (int i = 0; i < n && i <= maxLine; i++) {
    if (t[i] == ' ') lastSpace = i;
  }
  if (lastSpace > 0) {
    cut = lastSpace;
  } else {
    cut = maxLine;  // palabra larga: corte duro
  }
  l1 = t.substring(0, cut);
  l1.trim();
  String rest = t.substring(cut);
  rest.trim();
  // Overflow is marked with a bare "..." rather than a silent chop, so a
  // cut name reads as truncated and the tail length is still guessable.
  if ((int)rest.length() > maxLine) {
    rest = rest.substring(0, maxLine - 3) + "...";
  }
  l2 = rest;
}

static void renderFinish(const BambuState &st) {
  oled.clearDisplay();
  // 1. Title in up to 2 lines (large font).
  String l1, l2;
  wrapJobName2Lines(monoCleanJobName(jobDisplayName(st)), l1, l2);
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 12);
  oled.print(l1);
  if (l2.length() > 0) {
    oled.setCursor(0, 25);
    oled.print(l2);
  }
  // 2. DONE! en recuadro centrado.
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds("DONE!", 0, 0, &x1, &y1, &w, &h);
  const int boxH = 18;
  const int boxW = (int)w + 20;
  const int boxX = (128 - boxW) / 2;
  const int boxY = 32;
  oled.drawRect(boxX, boxY, boxW, boxH, SH110X_WHITE);
  oled.setCursor((128 - (int)w) / 2, boxY + 13);
  oled.print("DONE!");
  oled.setFont();
  oled.setTextSize(1);
  // 3. Cooldown temps at the bottom (same row as in printing).
  if (monoSettings.showTempsDone)
    drawTempsRow(54, st.nozzleTemp, st.nozzleTarget, st.bedTemp, st.bedTarget);
  oled.display();
}

static void renderIdle(const BambuState &st) {
  oled.clearDisplay();
  s_marqueeActive = false;  // no title on idle: don't inherit a marquee cadence
  if (monoSettings.showClockIdle) {
    // Spotify clock (ESP-Spotify-OLED printLocalTime): default font, 30x30
    // weather icon at (23,32), size-2 temperature at (60,40) with the degree
    // bitmap. No printer temps here by design. With the weather row visible
    // the clock sits at the Spotify-ish top anchor (y=5); with weather off
    // the 24 px clock centers vertically on its own ((64-24)/2 = 20).
    bool wxShown = monoSettings.weatherEnabled || monoMockActive();
    int clkY = wxShown ? 5 : 20;
    oled.setFont();
    oled.setTextSize(3);
    if (ntpOk()) {
      char clk[10];
      bool isPM = false;
      fmtClockMode(clk, sizeof(clk), &isPM);
      if (netSettings.use24h) {
        oled.setCursor(20, clkY);
        oled.print(clk);
      } else {
        // Centered time + AM/PM block (5-char times leave no room at x=20).
        int16_t x1, y1;
        uint16_t w, h;
        oled.getTextBounds(String(clk), 20, clkY, &x1, &y1, &w, &h);
        int x0 = (128 - ((int)w + 6 + 12)) / 2;
        if (x0 < 0) x0 = 0;
        oled.setCursor(x0, clkY);
        oled.print(clk);
        oled.setTextSize(1);
        oled.setCursor(x0 + (int)w + 6, clkY + 8);
        oled.print(isPM ? "PM" : "AM");
        oled.setTextSize(3);
      }
    } else {
      oled.setCursor(20, clkY);
      oled.print("--:--");
    }
    if (wxShown) {
      MonoWeatherData w;
      bool have = monoWeatherGet(w);
      char tmp[8];
      if (have)
        snprintf(tmp, sizeof(tmp), "%d", tempInt(w.tempC));
      else
        snprintf(tmp, sizeof(tmp), "--");
      size_t n = strlen(tmp);
      oled.drawBitmap(23, 32, monoWeatherIcon(have ? w.icon : ""),
                      MONO_WX_ICON_W, MONO_WX_ICON_H, SH110X_WHITE);
      oled.setTextSize(2);
      oled.setCursor(60, 40);
      oled.print(tmp);
      oled.drawBitmap(60 + 2 + n * 12, 40, monoDegreeBmp(), 8, 10, SH110X_WHITE);
      oled.setCursor(60 + 2 + 9 + n * 12, 40);
      oled.print("C");
    }
    oled.setFont();
    oled.setTextSize(1);
  } else {
    // Big actual temps with icons (targets rarely matter in idle).
    oled.setFont(&FreeMonoBold9pt7b);
    oled.setTextSize(1);
    char nb[8], bb[8];
    snprintf(nb, sizeof(nb), "%d", tempInt(st.nozzleTemp));
    snprintf(bb, sizeof(bb), "%d", tempInt(st.bedTemp));
    int16_t x1, y1;
    uint16_t w, h;
    int x = 0;
    drawNozzleIcon(x, 32);
    x += 11;
    oled.setCursor(x, 42);
    oled.print(nb);
    oled.getTextBounds(String(nb), 0, 0, &x1, &y1, &w, &h);
    x += (int)w + 8;
    drawBedIcon(x, 32);
    x += 12;
    oled.setCursor(x, 42);
    oled.print(bb);
    oled.setFont();
    oled.setTextSize(1);

    // HOT warning lives on this fallback screen (the Spotify clock above has
    // no room for it).
    oled.setCursor(0, BOTTOM_Y);
    if (monoSettings.showHotIdle && st.nozzleTemp > (float)MONO_HOT_THRESHOLD_C) {
      oled.print("HOT ");
    }
  }

  // ... and a status indicator in the bottom-right corner (8x8 box at 116,56):
  // warn triangle when the network is down, X when the printer is off,
  // empty circle when idle and all good. Liveness needs fresh data, not just
  // the MQTT flag: in cloud mode the broker stays reachable with the printer
  // off, and LAN keepalive takes a minute to fail - both painted a stale
  // circle instead of the X. Hidable via the web "connection icon" toggle.
  const int iconX = 116;
  const int iconY = 56;
  if (!monoSettings.showStatusIdle) {
    // nothing: clock + weather use the full width
  } else if (!isWiFiConnected() || isAPMode()) {
    oled.drawTriangle(iconX + 3, iconY, iconX, iconY + 7, iconX + 7, iconY + 7,
                      SH110X_WHITE);
    oled.drawFastVLine(iconX + 3, iconY + 2, 3, SH110X_WHITE);
    oled.drawPixel(iconX + 3, iconY + 6, SH110X_WHITE);
  } else if (!monoPrinterLive(st)) {
    oled.drawLine(iconX, iconY, iconX + 7, iconY + 7, SH110X_WHITE);
    oled.drawLine(iconX + 7, iconY, iconX, iconY + 7, SH110X_WHITE);
  } else {
    oled.drawCircle(iconX + 4, iconY + 4, 3, SH110X_WHITE);
  }
  oled.display();
}

static void renderFailed(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(String("ERR"));
  oled.setCursor(0, TEMP_Y);
#if HAS_HMS_UI
  char buf[24];
  snprintf(buf, sizeof(buf), "code %08X", (unsigned)st.printError);
  oled.print(buf);
#else
  oled.print(st.gcodeState);
#endif
  oled.setCursor(0, LAYER_Y);
  String name = monoCleanJobName(jobDisplayName(st));
  if (name.length() > 21) name = name.substring(0, 18) + "...";
  oled.print(name);
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_FAILED);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("error");
  oled.display();
}

static void renderOffline() {
  oled.clearDisplay();
  // Name first (scrolls when long), helper IP, then the printer target as
  // the diagnostic line - the printer IP alone says nothing about which
  // unit is down.
  uint8_t slot = activePrinterIndex < MAX_PRINTERS ? activePrinterIndex : 0;
  String nm = monoPrinterLabel(slot);
  monoDrawTitle(nm.length() > 0 ? nm : String("Offline"));
  String hip = isAPMode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  oled.setCursor(0, TEMP_Y);
  oled.print("IP " + hip);
  oled.setCursor(0, LAYER_Y);
  if (isPrinterConfigured(slot)) {
    if (isCloudMode(printers[slot].config.mode)) {
      oled.print("to cloud");
    } else {
      const char *pip = printers[slot].config.ip;
      oled.print("to ");
      oled.print((pip && pip[0]) ? pip : "--");
    }
  } else {
    oled.print("--");
  }
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_OFFLINE);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("offline");
  oled.display();
}

static void renderPrepare(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(String("Heating"));
  drawTempsRow(TEMP_Y, st.nozzleTemp, st.nozzleTarget, st.bedTemp, st.bedTarget);
  oled.setCursor(0, LAYER_Y);
  String nm = monoCleanJobName(jobDisplayName(st));
  if (nm.length() > 21) nm = nm.substring(0, 18) + "...";
  oled.print(nm);
  monoDrawProgressBar(BAR_Y, 0);
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_PREPARE);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("0%");
  oled.display();
}

// --- Change detection (Step 5) -------------------------------------------------
struct MonoSig {
  MonoScreen scr;
  MonoScreen rawScr;  // pre-hold screen: FINISH->IDLE expiry must read as change
  int progress;
  int remaining;
  unsigned layer;
  unsigned total;
  int nzl, nzlT, bed, bedT;
  unsigned long err;
  bool connected;
  char job[128];  // full clean-name cap (subtaskName[128]); must track, or a
                  // renamed job that shares its prefix never repaints
  int clockMin;  // for the idle clock
  int8_t printTimeAlt;  // printing remaining/ETA alternation phase (repaints on toggle)
  uint16_t monoFlags;  // web toggles bitmask: a flip must repaint immediately
  int wtemp;       // weather temp on the idle clock
  char wicon[4];   // weather code on the idle clock
};

static uint16_t monoFlagsNow() {
  uint16_t f = 0;
  if (monoSettings.showTempsDone) f |= 0x01;
  if (monoSettings.showTimePrinting) f |= 0x02;
  if (monoSettings.showTimeAlt) f |= 0x04;
  if (monoSettings.showLayer) f |= 0x08;
  if (monoSettings.showBar) f |= 0x10;
  if (monoSettings.showHotIdle) f |= 0x40;
  if (monoSettings.showClockIdle) f |= 0x80;
  if (monoSettings.weatherEnabled) f |= 0x100;
  if (!netSettings.use24h) f |= 0x200;  // 12h clock/end-time: repaint now
  if (!monoSettings.showStatusIdle) f |= 0x400;
  return f;
}

static MonoSig s_last;
static bool s_haveLast = false;
static unsigned long s_lastFrameMs = 0;

static MonoSig makeSig(MonoScreen scr, MonoScreen rawScr, const BambuState &st) {
  MonoSig s;
  s.scr = scr;
  s.rawScr = rawScr;
  s.progress = (int)st.progress;
  s.remaining = (int)st.remainingMinutes;
  s.layer = st.layerNum;
  s.total = st.totalLayers;
  s.nzl = tempInt(st.nozzleTemp);
  s.nzlT = tempInt(st.nozzleTarget);
  s.bed = tempInt(st.bedTemp);
  s.bedT = tempInt(st.bedTarget);
#if HAS_HMS_UI
  s.err = st.printError;
#else
  s.err = 0;
#endif
  s.connected = st.connected;
  String j = monoCleanJobName(jobDisplayName(st));
  strlcpy(s.job, j.c_str(), sizeof(s.job));
  s.printTimeAlt =
      monoSettings.showTimeAlt
          ? (int8_t)((millis() / (unsigned long)MONO_END_TIME_ALT_MS) % 2)
          : 0;
  s.monoFlags = monoFlagsNow();
  MonoWeatherData w;
  if (monoWeatherGet(w)) {
    s.wtemp = tempInt(w.tempC);
    strlcpy(s.wicon, w.icon, sizeof(s.wicon));
  } else {
    s.wtemp = -999;
    s.wicon[0] = '\0';
  }
  if (ntpOk()) {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    s.clockMin = tmv.tm_hour * 60 + tmv.tm_min;
  } else {
    s.clockMin = -1;
  }
  return s;
}

static bool sigSame(const MonoSig &a, const MonoSig &b) {
  return a.scr == b.scr && a.rawScr == b.rawScr && a.progress == b.progress &&
         a.remaining == b.remaining && a.layer == b.layer && a.total == b.total &&
         a.nzl == b.nzl && a.nzlT == b.nzlT && a.bed == b.bed && a.bedT == b.bedT &&
         a.err == b.err && a.connected == b.connected && strcmp(a.job, b.job) == 0 &&
         a.clockMin == b.clockMin &&
         a.printTimeAlt == b.printTimeAlt &&
         a.monoFlags == b.monoFlags && a.wtemp == b.wtemp &&
         strcmp(a.wicon, b.wicon) == 0;
}

static void renderNow(MonoScreen scr, const BambuState &st) {
  bool blinkOn = true;
  if (scr == MONO_SCR_PAUSE) blinkOn = ((millis() / 500) % 2) == 0;
  switch (scr) {
    case MONO_SCR_PRINTING:
    case MONO_SCR_PAUSE: renderPrinting(st, scr, blinkOn); break;
    case MONO_SCR_FINISH: renderFinish(st); break;
    case MONO_SCR_FAILED: renderFailed(st); break;
    case MONO_SCR_OFFLINE: renderOffline(); break;
    case MONO_SCR_PREPARE: renderPrepare(st); break;
    case MONO_SCR_IDLE:
    default: renderIdle(st); break;
  }
}

// --- Mock mode (Step 6) ---------------------------------------------------------
#if MONO_MOCK
static bool s_mockOn = false;
static bool s_mockCycle = false;
static int s_mockIdx = 0;
static unsigned long s_mockLastMs = 0;

static void mockBuild(int idx, BambuState &o) {
  memset(&o, 0, sizeof(o));
  o.connected = true;
  o.lastUpdate = millis();  // live data: keeps monoPrinterLive() true
  o.progress = 0;
  o.remainingMinutes = 0;
  switch (idx) {
    case 0:  // printing 0%, short name
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Cube", sizeof(o.subtaskName));
      o.progress = 0;
      o.remainingMinutes = 45;
      o.layerNum = 1;
      o.totalLayers = 200;
      o.nozzleTemp = 215;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 1:  // printing 50%, ~20 chars
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Phone_stand_20_chars", sizeof(o.subtaskName));
      o.progress = 50;
      o.remainingMinutes = 83;
      o.layerNum = 123;
      o.totalLayers = 456;
      o.nozzleTemp = 215;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 2:  // printing, long name -> marquee
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Very_long_part_for_marquee_testing_that_overflows.gcode.3mf",
              sizeof(o.subtaskName));
      o.progress = 87;
      o.remainingMinutes = 125;
      o.layerNum = 300;
      o.totalLayers = 345;
      o.nozzleTemp = 220;
      o.nozzleTarget = 220;
      o.bedTemp = 65;
      o.bedTarget = 65;
      break;
    case 3:  // paused
      setPrinterGcodeStateCanonical(o, GCODE_PAUSE);
      o.printing = true;
      strlcpy(o.subtaskName, "Phone_stand_20_chars", sizeof(o.subtaskName));
      o.progress = 50;
      o.remainingMinutes = 83;
      o.layerNum = 123;
      o.totalLayers = 456;
      o.nozzleTemp = 210;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 4:  // finished
      setPrinterGcodeStateCanonical(o, GCODE_FINISH);
      o.printing = false;
      strlcpy(o.subtaskName, "Cube", sizeof(o.subtaskName));
      o.progress = 100;
      o.nozzleTemp = 40;
      o.bedTemp = 35;
      break;
    case 5:  // error
      setPrinterGcodeStateCanonical(o, GCODE_FAILED);
      o.printing = false;
      strlcpy(o.subtaskName, "Failed_part", sizeof(o.subtaskName));
#if HAS_HMS_UI
      o.printError = 0x03008007UL;
      o.printErrorSeen = true;
#endif
      o.nozzleTemp = 180;
      o.bedTemp = 50;
      break;
    case 6:  // cold idle
      setPrinterGcodeStateCanonical(o, GCODE_IDLE);
      o.printing = false;
      o.nozzleTemp = 24;
      o.bedTemp = 24;
      break;
    case 7:  // hot idle -> HOT
      setPrinterGcodeStateCanonical(o, GCODE_IDLE);
      o.printing = false;
      o.nozzleTemp = 62;
      o.bedTemp = 45;
      break;
    case 8:  // offline
    default:
      o.connected = false;
      setPrinterGcodeStateCanonical(o, GCODE_UNKNOWN);
      break;
  }
}

static void mockHelp() {
  Serial.println(F("mock: 0 print 0% short | 1 print 50% 20ch | 2 print long wrap | "
                   "3 pause | 4 done | 5 error | 6 cold idle | 7 hot idle | 8 offline"));
  Serial.println(F("usage: mock <n> | mock cycle | mock off | mock w<code> (demo weather icon, empty clears)"));
}

static String s_serLine = "";

void monoMockHandleSerial() {
  // Only consume lines starting with 'm' (peek), so other serial protocols
  // (e.g. Improv on first boot) keep their bytes.
  while (Serial.available()) {
    if (Serial.peek() != 'm' && s_serLine.length() == 0) return;
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_serLine.trim();
      if (s_serLine.startsWith(F("mock"))) {
        String arg = s_serLine.substring(4);
        arg.trim();
        if (arg.length() == 0) {
          mockHelp();
        } else if (arg == "off") {
          s_mockOn = false;
          s_mockCycle = false;
          s_haveLast = false;  // force a repaint with live data
          Serial.println(F("mock off"));
        } else if (arg == "cycle") {
          s_mockOn = true;
          s_mockCycle = true;
          s_mockLastMs = millis();
          s_haveLast = false;
          Serial.println(F("mock cycle: rotating 9 screens every 3 s"));
        } else if (arg[0] == 'w') {
          // Demo weather icon for the idle clock ("mock w10d", "mock w" clears).
          // Checked before the numeric branch: toInt("w10d") is 0.
          monoWeatherSetDemo(arg.substring(1).c_str());
          s_haveLast = false;  // repaint the idle clock with the new icon
        } else {
          int idx = arg.toInt();
          if (idx >= 0 && idx <= 8) {
            s_mockOn = true;
            s_mockCycle = false;
            s_mockIdx = idx;
            s_haveLast = false;
            Serial.printf("mock %d\n", idx);
          } else {
            mockHelp();
          }
        }
      }
      s_serLine = "";
    } else {
      s_serLine += c;
      if (s_serLine.length() > 32) s_serLine = "";  // foreign line: drop it
    }
  }
}

bool monoMockActive() { return s_mockOn; }
#else
void monoMockHandleSerial() {}
bool monoMockActive() { return false; }
#endif  // MONO_MOCK

// --- API -----------------------------------------------------------------------
void monoDisplayInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);  // 400 kHz: the ~10 fps marquee needs it
  delay(250);
  oled.begin(OLED_I2C_ADDR, true);
  oled.setTextColor(SH110X_WHITE);
  oled.setContrast(monoSettings.oledContrast);
  s_appliedContrast = monoSettings.oledContrast;
  oled.setTextWrap(false);  // title wrap is manual; nothing else overflows
  s_booted = true;
  s_enabled = true;
  Serial.printf("MONO heap=%u\n", (unsigned)ESP.getFreeHeap());
  // The splash toggle owns every boot pixel: skipped means a blank panel
  // until the tick paints the clock (this message was the "splash" users saw
  // even with the toggle off).
  if (monoSettings.showSplash) {
    monoShowMessage("BambuHelper", "Mono starting");
  } else {
    oled.clearDisplay();
    oled.display();
  }
#if MONO_MOCK && MONO_MOCK_AUTO
  s_mockOn = true;
  s_mockCycle = true;
  s_mockLastMs = millis();
  Serial.println(F("mock cycle (MONO_MOCK_AUTO)"));
#endif
}

// Liveness per slot for the boot list (same freshness rule as the idle
// corner icon: flag + recent data, x5 stale window in cloud mode).
static bool monoSlotLive(uint8_t slot) {
  if (slot >= MAX_PRINTERS) return false;
  const BambuState &s = printers[slot].state;
  if (!s.connected || s.lastUpdate == 0) return false;
  unsigned long staleMs = (unsigned long)BAMBU_STALE_TIMEOUT;
  if (isCloudMode(printers[slot].config.mode)) staleMs *= 5UL;
  return (millis() - s.lastUpdate) <= staleMs;
}

void monoShowBoot() {
  if (!s_booted || !s_enabled) return;
  renderBootScreen();
  // Hold it: the tick repaints every loop and would otherwise cover the boot
  // screen on the very next iteration (which is why skip/show looked the same).
  s_bootHoldUntilMs = millis() + (unsigned long)MONO_BOOT_HOLD_MS;
  s_lastBootDrawMs = millis();
}

static void renderBootScreen() {
  oled.clearDisplay();
  // All default font (21 chars/row): header, helper IP, section, names.
  oled.setFont();
  oled.setTextSize(1);
  bool ap = isAPMode();
  String head = ap ? getAPSSID() : String("-- Bambu Helper --");
  if (head.length() > 21) head = head.substring(0, 21);
  oled.setCursor(0, 4);
  oled.print(head);
  String hip = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  if (hip == "0.0.0.0") hip = "connecting";
  String ipline = "IP: " + hip;
  if (ipline.length() > 21) ipline = ipline.substring(0, 21);
  oled.setCursor(0, 12);
  oled.print(ipline);
  oled.setCursor(0, 20);
  oled.print("Printers:");
  int y = 28;
  int shown = 0;
  for (uint8_t i = 0; i < MAX_PRINTERS && y <= 52; i++) {
    if (!isPrinterConfigured(i)) continue;
    String nm = monoPrinterLabel(i);
    if (nm.length() == 0) nm = String("Printer ") + (int)(i + 1);
    if (nm.length() > 16) nm = nm.substring(0, 16);
    nm += monoSlotLive(i) ? " [on]" : " [off]";
    oled.setCursor(0, y);
    oled.print(nm);
    y += 8;
    shown++;
  }
  if (shown == 0) {
    oled.setCursor(0, 28);
    oled.print("no printer");
  }
  oled.display();
}

void monoShowMessage(const char *line1, const char *line2) {
  if (!s_booted || !s_enabled) return;
  oled.clearDisplay();
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 15);
  oled.print(line1 ? line1 : "");
  oled.setFont();
  oled.setTextSize(1);
  oled.setCursor(0, 34);
  oled.print(line2 ? line2 : "");
  oled.display();
}

void monoShowStatus(const BambuState &st) {
  if (!s_booted || !s_enabled) return;
  MonoScreen raw = monoSelectScreen(st);
  MonoScreen scr = monoEffectiveScreen(raw);
  renderNow(scr, st);
  s_last = makeSig(scr, raw, st);
  s_haveLast = true;
  s_lastFrameMs = millis();
}

void monoShowError(unsigned long code) {
  if (!s_booted || !s_enabled) return;
  BambuState st;
  memset(&st, 0, sizeof(st));
  st.connected = true;
  setPrinterGcodeStateCanonical(st, GCODE_FAILED);
#if HAS_HMS_UI
  st.printError = (uint32_t)code;
  st.printErrorSeen = true;
#endif
  renderFailed(st);
}

void monoDisplaySetEnabled(bool on) {
  s_enabled = on;
  if (s_booted && !on) {
    oled.clearDisplay();
    oled.display();
    s_haveLast = false;  // repaint on re-enable even if nothing changed
  } else {
    s_haveLast = false;
  }
}

bool monoDisplayIsEnabled() { return s_enabled; }

void monoDisplayTick() {
  if (!s_booted || !s_enabled) return;
  // Boot-screen hold: monoShowBoot() paints once and the tick below would
  // cover it on the next loop, so hold the frame for the window. Mock mode
  // always wins (review must not wait out the hold).
  if (s_bootHoldUntilMs != 0) {
    if ((long)(millis() - s_bootHoldUntilMs) < 0) {
      if (!monoMockActive()) {
        // Live boot: repaint ~1 Hz so the [on]/[off] flags track MQTT while
        // it connects (right after boot everything still reads [off]).
        if (millis() - s_lastBootDrawMs >= 1000) {
          s_lastBootDrawMs = millis();
          renderBootScreen();
        }
        return;
      }
    } else {
      s_bootHoldUntilMs = 0;
    }
  }
  // Contrast is a hardware register, not pixels: push it when the web
  // setting moves (slider saves instantly, this picks it up next loop).
  if (s_appliedContrast != monoSettings.oledContrast) {
    s_appliedContrast = monoSettings.oledContrast;
    oled.setContrast(s_appliedContrast);
  }
#if MONO_MOCK
  BambuState mock;
  const BambuState *src = nullptr;
  if (s_mockOn) {
    if (s_mockCycle && millis() - s_mockLastMs >= 3000) {
      s_mockLastMs = millis();
      s_mockIdx = (s_mockIdx + 1) % 9;
      Serial.printf("mock %d\n", s_mockIdx);
    }
    mockBuild(s_mockIdx, mock);
    src = &mock;
  } else {
    if (activePrinterIndex < MAX_PRINTERS)
      src = &printers[activePrinterIndex].state;
    else
      src = &printers[0].state;
  }
#else
  const BambuState *src = (activePrinterIndex < MAX_PRINTERS)
                              ? &printers[activePrinterIndex].state
                              : &printers[0].state;
#endif
  const BambuState &st = *src;
  MonoScreen raw = monoSelectScreen(st);
  MonoScreen scr = monoEffectiveScreen(raw);
  // Weather only feeds the idle clock: polling here keeps it off the
  // printing path (no extra TLS while MQTT streams) and off the mock path
  // (mock uses the "mock w<code>" demo instead of the network).
#if MONO_MOCK
  if (scr == MONO_SCR_IDLE && !s_mockOn) monoWeatherPoll();
#else
  if (scr == MONO_SCR_IDLE) monoWeatherPoll();
#endif
  MonoSig sig = makeSig(scr, raw, st);
  bool changed = !s_haveLast || !sigSame(sig, s_last);
  unsigned long now = millis();
  // Marquee/blink => ~10 fps; static => 1 s.
  bool animated = s_marqueeActive || scr == MONO_SCR_PAUSE ||
                  (s_haveLast && s_last.scr != scr);
  if (scr == MONO_SCR_PAUSE) animated = true;
  unsigned long interval =
      animated ? (unsigned long)MONO_MARQUEE_MS : (unsigned long)MONO_REFRESH_MS;
  if (changed || now - s_lastFrameMs >= interval) {
    renderNow(scr, st);
    s_last = sig;
    s_haveLast = true;
    s_lastFrameMs = now;
  }
}

#endif  // BOARD_IS_MONO
