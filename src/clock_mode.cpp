#include "clock_mode.h"
#include "display_ui.h"
#include "fonts.h"
#include "settings.h"
#include "config.h"
#include "layout.h"
#include <time.h>

// Base (1x) digit metrics for the simple clock. Layout-agnostic on purpose:
// LY_ARK_* values in some layout profiles (e.g. layout_480x480.h) are already
// pre-scaled for the pong clock, so reusing them here would double-scale on
// those screens.
static constexpr int CLK_BASE_W     = 32;
static constexpr int CLK_BASE_H     = 48;
static constexpr int CLK_BASE_COLON = 12;

static inline int clkScrW() { return (int)tft.width(); }
static inline int clkScrH() { return (int)tft.height(); }

static constexpr int DATE_FONT_H   = 16;   // FONT_BODY at 1x
static constexpr int DATE_GAP      = 14;   // gap between time digits and date

static int clkDigitX(int i, int timeX0, int digitW, int colonW) {
  if (i < 2)  return timeX0 + i * digitW;
  if (i == 2) return timeX0 + 2 * digitW;                       // colon slot
  return timeX0 + 2 * digitW + colonW + (i - 3) * digitW;
}

// Map the 1..3 size selector to a scale factor.
static float sizeIndexToScale(int idx) {
  switch (idx) {
    case 3: return 2.0f;   // Large
    case 2: return 1.5f;   // Medium
    default: return 1.0f;  // Normal
  }
}

static int autoSizeIndex() {
  if (SCREEN_W >= 480) return 3;   // Large
  if (SCREEN_W >= 320) return 2;   // Medium
  return 1;                        // Normal
}

// Resolve the user-selected time size, clamping down if the resulting block
// (digits + colon + AM/PM in 12h mode) wouldn't fit horizontally.
// Returns a scale factor (1.0 / 1.5 / 2.0).
static float getEffectiveClockScale() {
  uint8_t requested = dispSettings.clockTimeSize;
  if (requested > 3) requested = 0;                              // tolerate junk
  int wanted = requested ? (int)requested : autoSizeIndex();

  int suffixW = 0;
  if (!netSettings.use24h) {
    setFont(tft, FONT_BODY);
    tft.setTextSize(1);
    int amW = tft.textWidth("AM");
    int pmW = tft.textWidth("PM");
    suffixW = (amW > pmW ? amW : pmW) + 6;                       // gap + label
  }
  auto fits = [&](int idx) {
    float s = sizeIndexToScale(idx);
    int blockW = (int)(4 * CLK_BASE_W * s) + (int)(CLK_BASE_COLON * s);
    return blockW + suffixW <= clkScrW() - 4;
  };
  while (wanted > 1 && !fits(wanted)) wanted--;
  return sizeIndexToScale(wanted);
}

// --- Per-tick state cache ---
static int  prevMinute = -1;
static char prevDigits[5] = {0, 0, 0, 0, 0};
static bool prevColon = false;
static char prevDateBuf[28] = "";
static char prevAmPm[3] = "";
static int  prevAmpmX = -1;
static int  prevAmpmY = -1;
static int  prevSuffixTextW = 0;
static int  prevDateY = -1;
static float prevScale = -1.0f;
static int   prevTimeX0 = -1;
static bool  prevUse24h = true;
static bool  prevHideDate = false;

void resetClock() {
  prevMinute = -1;
  memset(prevDigits, 0, sizeof(prevDigits));
  prevColon = false;
  prevDateBuf[0] = '\0';
  prevAmPm[0] = '\0';
  prevAmpmX = -1;
  prevAmpmY = -1;
  prevSuffixTextW = 0;
  prevDateY = -1;
  prevScale = -1.0f;
  prevTimeX0 = -1;
  prevUse24h = true;
  prevHideDate = false;
}

void drawClock() {
  struct tm now;
  if (!getLocalTime(&now, 0)) {
    time_t t = time(nullptr);
    if (t < 1600000000UL) return;
    localtime_r(&t, &now);
  }

  const uint16_t bg       = dispSettings.bgColor;
  const uint16_t timeClr  = dispSettings.clockTimeColor;
  const uint16_t dateClr  = dispSettings.clockDateColor;

  const float scale  = getEffectiveClockScale();
  const int digitW = (int)(CLK_BASE_W * scale);
  const int digitH = (int)(CLK_BASE_H * scale);
  const int colonW = (int)(CLK_BASE_COLON * scale);
  const int timeBlockW = 4 * digitW + colonW;

  // AM/PM suffix width (only meaningful in 12h mode).
  int suffixTextW = 0;
  int suffixW = 0;
  if (!netSettings.use24h) {
    setFont(tft, FONT_BODY);
    tft.setTextSize(1);
    int amW = tft.textWidth("AM");
    int pmW = tft.textWidth("PM");
    suffixTextW = (amW > pmW ? amW : pmW);
    suffixW = suffixTextW + 6;
  }
  const int totalW  = timeBlockW + suffixW;
  const int sw      = clkScrW();
  const int sh      = clkScrH();
  const int timeX0  = (sw - totalW) / 2;
  // Vertically center the whole clock block on the current canvas. With the
  // layout's fixed LY_CLK_TIME_Y the time sat above mid-screen in portrait
  // and below it in landscape (the 240x320 layout's value was chosen for
  // portrait). Compute timeYTop from the actual screen height instead.
  const int contentH = digitH +
                       (dispSettings.hideClockDate ? 0 : (DATE_GAP + DATE_FONT_H));
  const int timeYTop = (sh - contentH) / 2;
  const int ampmX   = timeX0 + timeBlockW + 6;
  const int ampmFontH = DATE_FONT_H;
  const int ampmY   = timeYTop + digitH - ampmFontH;             // bottom-align with digits

  // Force a full redraw whenever the horizontal layout shifts (scale change,
  // 12h <-> 24h centering shift, or hide-date toggle).
  if (scale != prevScale || timeX0 != prevTimeX0 ||
      netSettings.use24h != prevUse24h ||
      dispSettings.hideClockDate != prevHideDate) {
    prevMinute = -1;
    memset(prevDigits, 0, sizeof(prevDigits));
    prevColon = false;
    prevDateBuf[0] = '\0';
    prevAmPm[0] = '\0';
    tft.fillRect(0, LY_CLK_CLEAR_Y, sw, LY_CLK_CLEAR_H, bg);
    markFrameDirty();
    prevScale = scale;
    prevTimeX0 = timeX0;
    prevUse24h = netSettings.use24h;
    prevHideDate = dispSettings.hideClockDate;
  }

  // --- Colon blink (~250 ms cadence; every call) ---
  const bool colonOn = (millis() % 1000) < 500;
  if (colonOn != prevColon) {
    markFrameDirty();
    const int cx = clkDigitX(2, timeX0, digitW, colonW);
    tft.fillRect(cx, timeYTop, colonW, digitH, bg);
    if (colonOn) {
      setFont(tft, FONT_7SEG);
      tft.setTextSize(scale);
      tft.setTextColor(timeClr, bg);
      tft.drawChar(':', cx, timeYTop, 7);
    }
    prevColon = colonOn;
  }

  // --- Only update digits/date when minute changes ---
  if (now.tm_min == prevMinute) return;
  prevMinute = now.tm_min;
  markFrameDirty();

  // Build digit array.
  char digits[5];
  if (netSettings.use24h) {
    digits[0] = '0' + (now.tm_hour / 10);
    digits[1] = '0' + (now.tm_hour % 10);
  } else {
    int h = now.tm_hour % 12;
    if (h == 0) h = 12;
    digits[0] = (h >= 10) ? '1' : ' ';
    digits[1] = '0' + (h % 10);
  }
  digits[2] = ':';
  digits[3] = '0' + (now.tm_min / 10);
  digits[4] = '0' + (now.tm_min % 10);

  // Draw only changed digits.
  setFont(tft, FONT_7SEG);
  tft.setTextSize(scale);
  tft.setTextColor(timeClr, bg);

  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;                                        // colon handled above
    if (digits[i] == prevDigits[i]) continue;
    const int x = clkDigitX(i, timeX0, digitW, colonW);
    tft.fillRect(x, timeYTop, digitW + 2, digitH, bg);
    tft.drawChar(digits[i], x, timeYTop, 7);
    prevDigits[i] = digits[i];
  }

  // Force colon redraw on first paint after a full clear.
  if (prevDigits[2] == 0) {
    prevColon = !colonOn;
    prevDigits[2] = ':';
  }

  // --- AM/PM inline next to the time, or clear when switching to 24h ---
  if (!netSettings.use24h) {
    const char* ampm = now.tm_hour < 12 ? "AM" : "PM";
    if (strcmp(ampm, prevAmPm) != 0 ||
        ampmX != prevAmpmX || ampmY != prevAmpmY) {
      setFont(tft, FONT_BODY);
      tft.setTextSize(1);
      tft.setTextColor(dateClr, bg);
      tft.setTextDatum(TL_DATUM);
      // Clear previous AM/PM at its old position if we moved it, then draw new.
      if (prevAmpmX >= 0 && (prevAmpmX != ampmX || prevAmpmY != ampmY) && prevAmPm[0]) {
        tft.fillRect(prevAmpmX, prevAmpmY - 1,
                     prevSuffixTextW + 2, ampmFontH + 2, bg);
      }
      tft.fillRect(ampmX, ampmY - 1, suffixTextW + 2, ampmFontH + 2, bg);
      tft.drawString(ampm, ampmX, ampmY);
      strlcpy(prevAmPm, ampm, sizeof(prevAmPm));
      prevAmpmX = ampmX;
      prevAmpmY = ampmY;
      prevSuffixTextW = suffixTextW;
    }
  } else if (prevAmPm[0] != '\0') {
    tft.fillRect(prevAmpmX, prevAmpmY - 1,
                 prevSuffixTextW + 2, ampmFontH + 2, bg);
    prevAmPm[0] = '\0';
    prevAmpmX = -1;
    prevAmpmY = -1;
    prevSuffixTextW = 0;
  }

  // --- Date (or hide-date wipe) ---
  if (dispSettings.hideClockDate) {
    if (prevDateBuf[0] && prevDateY >= 0) {
      const int dateClearH = 22;
      tft.fillRect(0, prevDateY - dateClearH / 2, sw, dateClearH, bg);
      prevDateBuf[0] = '\0';
    }
    return;
  }

  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateBuf[28];
  const int day = now.tm_mday;
  const int mon = now.tm_mon + 1;
  const int year = now.tm_year + 1900;
  switch (netSettings.dateFormat) {
    case 1:  snprintf(dateBuf, sizeof(dateBuf), "%s %02d-%02d-%04d", days[now.tm_wday], day, mon, year); break;
    case 2:  snprintf(dateBuf, sizeof(dateBuf), "%s %02d/%02d/%04d", days[now.tm_wday], mon, day, year); break;
    case 3:  snprintf(dateBuf, sizeof(dateBuf), "%s %04d-%02d-%02d", days[now.tm_wday], year, mon, day); break;
    case 4:  snprintf(dateBuf, sizeof(dateBuf), "%s %d %s %04d", days[now.tm_wday], day, months[now.tm_mon], year); break;
    case 5:  snprintf(dateBuf, sizeof(dateBuf), "%s %s %d, %04d", days[now.tm_wday], months[now.tm_mon], day, year); break;
    default: snprintf(dateBuf, sizeof(dateBuf), "%s %02d.%02d.%04d", days[now.tm_wday], day, mon, year); break;
  }

  // Date Y: derived from the centered time block. MC_DATUM expects the
  // vertical center of the text — add half the font height to the gap-relative
  // top so the baseline sits cleanly below the digits.
  const int dateY = timeYTop + digitH + DATE_GAP + DATE_FONT_H / 2;

  if (strcmp(dateBuf, prevDateBuf) != 0 || dateY != prevDateY) {
    setFont(tft, FONT_BODY);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(dateClr, bg);
    const int dateClearH = 22;
    const int dateW = tft.textWidth(prevDateBuf[0] ? prevDateBuf : dateBuf);
    const int newW = tft.textWidth(dateBuf);
    const int clearW = (dateW > newW) ? dateW : newW;
    // If Y moved, also wipe the previous date strip first.
    if (prevDateY >= 0 && prevDateY != dateY && prevDateBuf[0]) {
      tft.fillRect(0, prevDateY - dateClearH / 2, sw, dateClearH, bg);
    }
    tft.fillRect(sw / 2 - clearW / 2 - 2,
                 dateY - dateClearH / 2,
                 clearW + 4, dateClearH, bg);
    tft.drawString(dateBuf, sw / 2, dateY);
    strlcpy(prevDateBuf, dateBuf, sizeof(prevDateBuf));
    prevDateY = dateY;
  }
}
