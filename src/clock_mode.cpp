#include "clock_mode.h"
#include "display_ui.h"
#include "settings.h"
#include "config.h"
#include "layout.h"
#include "fonts.h"
#include <time.h>

// Font 7 digit dimensions (same as pong clock)
#define CLK_DIGIT_W   LY_ARK_DIGIT_W   // 32
#define CLK_DIGIT_H   LY_ARK_DIGIT_H   // 48
#define CLK_COLON_W   LY_ARK_COLON_W   // 12

#define CLK_TIME_W    (4 * CLK_DIGIT_W + CLK_COLON_W)
#define CLK_TIME_X    ((LY_W - CLK_TIME_W) / 2)

static int prevMinute = -1;
static char prevDigits[5] = {0, 0, 0, 0, 0};
static bool prevColon = false;
static char prevDateBuf[28] = "";
static char prevAmPm[3] = "";

void resetClock() {
  prevMinute = -1;
  memset(prevDigits, 0, sizeof(prevDigits));
  prevColon = false;
  prevDateBuf[0] = '\0';
  prevAmPm[0] = '\0';
}

// X position for each of the 5 slots: d0 d1 : d3 d4
static int clkDigitX(int i) {
  if (i < 2) return CLK_TIME_X + i * CLK_DIGIT_W;
  if (i == 2) return CLK_TIME_X + 2 * CLK_DIGIT_W;  // colon
  return CLK_TIME_X + 2 * CLK_DIGIT_W + CLK_COLON_W + (i - 3) * CLK_DIGIT_W;
}

void drawClock() {
  struct tm now;
  if (!getLocalTime(&now, 0)) {
    time_t t = time(nullptr);
    if (t < 1600000000UL) return;
    localtime_r(&t, &now);
  }

  uint16_t bg = dispSettings.bgColor;
  uint16_t timeClr = dispSettings.clockTimeColor;
  uint16_t dateClr = dispSettings.clockDateColor;

  // --- Colon blink (every call, ~250ms) ---
  bool colonOn = (millis() % 1000) < 500;
  if (colonOn != prevColon) {
    int cx = clkDigitX(2);
    int cy = LY_CLK_TIME_Y - CLK_DIGIT_H / 2;
    tft.fillRect(cx, cy, CLK_COLON_W, CLK_DIGIT_H, bg);
    if (colonOn) {
      setFont(tft, FONT_7SEG);
      tft.setTextSize(1);
      tft.setTextColor(timeClr, bg);
      tft.drawChar(':', cx, cy, 7);
    }
    prevColon = colonOn;
  }

  // --- Only update digits/date when minute changes ---
  if (now.tm_min == prevMinute) return;
  prevMinute = now.tm_min;

  // Build digit array
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

  // Draw only changed digits
  setFont(tft, FONT_7SEG);
  tft.setTextSize(1);
  tft.setTextColor(timeClr, bg);

  int dy = LY_CLK_TIME_Y - CLK_DIGIT_H / 2;  // top-left Y (MC_DATUM centers at LY_CLK_TIME_Y)

  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;  // colon handled above
    if (digits[i] == prevDigits[i]) continue;

    int x = clkDigitX(i);
    int clearW = CLK_DIGIT_W + 2;
    tft.fillRect(x, dy, clearW, CLK_DIGIT_H, bg);
    tft.drawChar(digits[i], x, dy, 7);
    prevDigits[i] = digits[i];
  }

  // Force colon redraw after full redraw (first draw or resetClock)
  if (prevDigits[2] == 0) {
    prevColon = !colonOn;  // will be redrawn on next call
    prevDigits[2] = ':';
  }

  // --- AM/PM (12h mode) / clear stale AM/PM when switching to 24h ---
  if (!netSettings.use24h) {
    const char* ampm = now.tm_hour < 12 ? "AM" : "PM";
    if (strcmp(ampm, prevAmPm) != 0) {
      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_LARGE);
      tft.setTextColor(dateClr, bg);
      int ampmW = tft.textWidth("PM");
      tft.fillRect(LY_W / 2 - ampmW / 2 - 2, LY_CLK_AMPM_Y - 12, ampmW + 4, 24, bg);
      tft.drawString(ampm, LY_W / 2, LY_CLK_AMPM_Y);
      strlcpy(prevAmPm, ampm, sizeof(prevAmPm));
    }
  } else if (prevAmPm[0] != '\0') {
    tft.setTextDatum(MC_DATUM);
    setFont(tft, FONT_LARGE);
    int ampmW = tft.textWidth("PM");
    tft.fillRect(LY_W / 2 - ampmW / 2 - 2, LY_CLK_AMPM_Y - 12, ampmW + 4, 24, bg);
    prevAmPm[0] = '\0';
  }

  // --- Date ---
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  char dateBuf[28];
  int day = now.tm_mday, mon = now.tm_mon + 1, year = now.tm_year + 1900;
  switch (netSettings.dateFormat) {
    case 1:  snprintf(dateBuf, sizeof(dateBuf), "%s  %02d-%02d-%04d", days[now.tm_wday], day, mon, year); break;
    case 2:  snprintf(dateBuf, sizeof(dateBuf), "%s  %02d/%02d/%04d", days[now.tm_wday], mon, day, year); break;
    case 3:  snprintf(dateBuf, sizeof(dateBuf), "%s  %04d-%02d-%02d", days[now.tm_wday], year, mon, day); break;
    case 4:  snprintf(dateBuf, sizeof(dateBuf), "%s  %d %s %04d", days[now.tm_wday], day, months[now.tm_mon], year); break;
    case 5:  snprintf(dateBuf, sizeof(dateBuf), "%s  %s %d, %04d", days[now.tm_wday], months[now.tm_mon], day, year); break;
    default: snprintf(dateBuf, sizeof(dateBuf), "%s  %02d.%02d.%04d", days[now.tm_wday], day, mon, year); break;
  }

  if (strcmp(dateBuf, prevDateBuf) != 0) {
    tft.setTextDatum(MC_DATUM);
    setFont(tft, FONT_LARGE);
    tft.setTextColor(dateClr, bg);
    // Clear previous date, draw new
    int dateW = tft.textWidth(prevDateBuf[0] ? prevDateBuf : dateBuf);
    int newW = tft.textWidth(dateBuf);
    int clearW = (dateW > newW) ? dateW : newW;
    tft.fillRect(LY_W / 2 - clearW / 2 - 2, LY_CLK_DATE_Y - 12, clearW + 4, 24, bg);
    tft.drawString(dateBuf, LY_W / 2, LY_CLK_DATE_Y);
    strlcpy(prevDateBuf, dateBuf, sizeof(prevDateBuf));
  }
}
