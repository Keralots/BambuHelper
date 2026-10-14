// BambuHelper-Mono: SH1106 128x64 over I2C with Adafruit GFX/SH110X.
// Style copied from the ESP-Spotify-OLED reference repo (see REFERENCE_NOTES.md).
#ifdef BOARD_IS_MONO

#include "mono_display.h"
#include "bambu_state.h"
#include "config.h"
#include "settings.h"

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
// N-10..N (measured in the font file). Title: always a single row at
// baseline y=12 (glyphs 2..12, ~2 px top margin; longer names scroll as a
// marquee on that same row, never wrapping).
// TEMP_Y=21 (icons 21..29, text 21..28), LAYER_Y=34 (34..41),
// BAR_Y=46 (46..51), BOTTOM_Y=56 (56..63): ~5 px of air between rows.
static const int TITLE_SINGLE_Y = 12;
static const int TEMP_Y = 21;
static const int LAYER_Y = 34;
static const int BAR_Y = 46;
static const int BAR_H = 6;
static const int BOTTOM_Y = 56;

static Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

static bool s_enabled = true;
static bool s_booted = false;

// --- Transliteration (the equivalent of the reference repo's ----------------
// --- replaceSpecialCharacters: the GFX fonts ship no accents, so map to ASCII)
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

void monoDrawProgressBar(int y, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  // Same bar as Spotify: drawRect + fillRect with map().
  oled.drawRect(0, y, 128, BAR_H, SH110X_WHITE);
  oled.fillRect(0, y, map(percent, 0, 100, 0, 126), BAR_H, SH110X_WHITE);
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
  const int maxLine = MONO_MAX_CHAR_TITLE_PER_LINE;
  int len = (int)clean.length();
  if (len <= maxLine) {
    // Fits: static single row.
    s_marqueeActive = false;
    oled.setCursor(0, TITLE_SINGLE_Y);
    oled.print(clean);
  } else {
    // Too long: single-row horizontal marquee (never wraps to 2 rows).
    s_marqueeActive = true;
    if (s_mqText != clean) marqueeReset(clean);
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(clean, 0, 0, &x1, &y1, &w, &h);
    (void)x1;
    (void)y1;
    (void)h;
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

static void fmtClock(char *dst, size_t n) {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  snprintf(dst, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

// Bottom row: remaining time, alternating with the bare end time
// every MONO_END_TIME_ALT_MS (needs NTP; without a clock it stays put).
static int endTimeAltPhase() {
#if MONO_SHOW_END_TIME
  if (!ntpOk()) return 0;
  return (int)((millis() / (unsigned long)MONO_END_TIME_ALT_MS) % 2);
#else
  return 0;
#endif
}

static String bottomTime(const BambuState &st, int altPhase) {
#if MONO_SHOW_END_TIME
  if (altPhase == 1 && (int)st.remainingMinutes >= 0 && st.remainingMinutes < 60 * 48) {
    time_t end = time(nullptr) + (time_t)st.remainingMinutes * 60;
    struct tm tmv;
    localtime_r(&end, &tmv);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return String(buf);
  }
#endif
  if (!st.connected) return String("--");
  return monoFormatRemaining((int)st.remainingMinutes);
}

// --- One render per screen (all end with display()) ----------------------------
static void renderPrinting(const BambuState &st, MonoScreen scr, bool blinkOn) {
  oled.clearDisplay();
  monoDrawTitle(monoCleanJobName(jobDisplayName(st)));
  // Temps with icons (y = TEMP_Y).
  drawTempsRow(TEMP_Y, st.nozzleTemp, st.nozzleTarget, st.bedTemp, st.bedTarget);
  oled.setCursor(0, LAYER_Y);
  char line[32];
  if (st.totalLayers > 0) {
    snprintf(line, sizeof(line), "Layer %u/%u", st.layerNum, st.totalLayers);
    oled.print(line);
  } else if (st.layerNum > 0) {
    snprintf(line, sizeof(line), "Layer %u", st.layerNum);
    oled.print(line);
  }
  monoDrawProgressBar(BAR_Y, st.progress);
  // Bottom row: icon + % on the left, time on the right.
  String pct = String((int)st.progress) + "%";
  String t = bottomTime(st, endTimeAltPhase());
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  (void)x1;
  (void)y1;
  (void)h;
  if (blinkOn) {
    monoDrawStatusIcon(0, BOTTOM_Y, scr);
    oled.setCursor(11, BOTTOM_Y);
    oled.print(pct);
  }
  oled.setCursor(128 - (int)w, BOTTOM_Y);
  oled.print(t);
  oled.display();
}

static void wrapJobName2Lines(const String &clean, String &l1, String &l2) {
  const int maxLine = MONO_MAX_CHAR_TITLE_PER_LINE;  // 11 con la fuente grande
  String t = clean;
  t.trim();
  if ((int)t.length() <= maxLine) {
    l1 = t;
    l2 = "";
    return;
  }
  // Relleno greedy por palabras para la primera linea.
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
  if ((int)rest.length() > maxLine) rest = rest.substring(0, maxLine);
  l2 = rest;
}

static void renderFinish(const BambuState &st) {
  oled.clearDisplay();
  // 1. Titulo en hasta 2 lineas (fuente grande).
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
  // 3. Temps de enfriamiento abajo (misma fila que en printing).
  drawTempsRow(54, st.nozzleTemp, st.nozzleTarget, st.bedTemp, st.bedTarget);
  oled.display();
}

static void renderIdle(const BambuState &st) {
  oled.clearDisplay();
  // 1. Fixed top title.
  monoDrawTitle(String("Idle"));

  // 2. Big centered clock (Spotify-clock style); big temps when NTP is down.
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setTextSize(1);
  if (ntpOk()) {
    char clk[8];
    fmtClock(clk, sizeof(clk));
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(String(clk), 0, 0, &x1, &y1, &w, &h);
    oled.setCursor((128 - (int)w) / 2, 42);
    oled.print(clk);
  } else {
    // Big actual temps with icons (targets rarely matter in idle).
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
  }
  oled.setFont();
  oled.setTextSize(1);

  // 3. Bottom row (y = 56): HOT on the left ...
  oled.setCursor(0, BOTTOM_Y);
  if (st.nozzleTemp > (float)MONO_HOT_THRESHOLD_C) {
    oled.print("HOT ");
  }

  // 4. ... and a connection indicator in the bottom-right corner.
  const int iconX = 116;
  const int iconY = 57;
  if (st.connected) {
    oled.fillRect(iconX, iconY + 2, 8, 4, SH110X_WHITE);
  } else {
    oled.drawRect(iconX, iconY + 1, 8, 6, SH110X_WHITE);
    oled.drawLine(iconX, iconY + 1, iconX + 7, iconY + 6, SH110X_WHITE);
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
  if (name.length() > 21) name = name.substring(0, 21);
  oled.print(name);
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_FAILED);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("error");
  oled.display();
}

static void renderOffline() {
  oled.clearDisplay();
  monoDrawTitle(String("Offline"));
  oled.setCursor(0, TEMP_Y);
  oled.print("printer IP:");
  oled.setCursor(0, LAYER_Y);
  const char *ip = "";
  if (activePrinterIndex < MAX_PRINTERS) ip = printers[activePrinterIndex].config.ip;
  oled.print((ip && ip[0]) ? ip : "--");
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
  oled.print(monoCleanJobName(jobDisplayName(st)).substring(0, 21));
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
  char job[40];
  int clockMin;  // for the idle clock
  int8_t timeAlt;  // remaining/end-time alternation phase (repaints on toggle)
};

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
  s.timeAlt = (int8_t)endTimeAltPhase();
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
         a.clockMin == b.clockMin && a.timeAlt == b.timeAlt;
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

// --- Modo pruebas (Paso 6) ------------------------------------------------------
#if MONO_MOCK
static bool s_mockOn = false;
static bool s_mockCycle = false;
static int s_mockIdx = 0;
static unsigned long s_mockLastMs = 0;

static void mockBuild(int idx, BambuState &o) {
  memset(&o, 0, sizeof(o));
  o.connected = true;
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
  Serial.println(F("mock: 0 print 0% short | 1 print 50% 20ch | 2 print long marquee | "
                   "3 pause | 4 done | 5 error | 6 cold idle | 7 hot idle | 8 offline"));
  Serial.println(F("usage: mock <n> | mock cycle | mock off"));
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
  oled.setTextWrap(false);  // title wrap is manual; nothing else overflows
  s_booted = true;
  s_enabled = true;
  Serial.printf("MONO heap=%u\n", (unsigned)ESP.getFreeHeap());
  monoShowMessage("BambuHelper", "Mono starting");
#if MONO_MOCK && MONO_MOCK_AUTO
  s_mockOn = true;
  s_mockCycle = true;
  s_mockLastMs = millis();
  Serial.println(F("mock cycle (MONO_MOCK_AUTO)"));
#endif
}

void monoShowBoot(const char *ssid, const char *ip) {
  if (!s_booted || !s_enabled) return;
  oled.clearDisplay();
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 15);
  oled.print("Bambu");
  oled.setFont();
  oled.setTextSize(1);
  oled.setCursor(0, 26);
  oled.print("Helper Mono");
  oled.setCursor(0, 40);
  oled.print(ssid ? ssid : "");
  oled.setCursor(0, 52);
  oled.print(ip ? ip : "");
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
  MonoSig sig = makeSig(scr, raw, st);
  bool changed = !s_haveLast || !sigSame(sig, s_last);
  unsigned long now = millis();
  // Marquee/parpadeo => ~10 fps; estatico => 1 s.
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
