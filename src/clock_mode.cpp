#include "clock_mode.h"
#include "display_ui.h"
#include "fonts.h"
#include "settings.h"
#include "config.h"
#include "layout.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
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

// --- Printer info footer (name + LAN IP per configured printer) ---
static char prevInfoLines[MAX_ACTIVE_PRINTERS][40] = {{0}};
static int  prevInfoCount = 0;

#if defined(DISPLAY_ROUND_240)
// Watch-face decorations (round GC9A01): rim tick marks + MQTT status dot.
static bool   roundTicksDrawn = false;
static int8_t roundPrevDot    = -1;   // 0 = none configured, 1 = green, 2 = red
#endif

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
  for (int i = 0; i < MAX_ACTIVE_PRINTERS; i++) prevInfoLines[i][0] = '\0';
  prevInfoCount = 0;
#if defined(DISPLAY_ROUND_240)
  roundTicksDrawn = false;
  roundPrevDot    = -1;
#endif
}

// Footer on the idle/clock screen: one line per configured printer with its
// friendly name and LAN IP. Anchored to the bottom edge so its position is
// independent of the clock size/date - guaranteeing it never overlaps even the
// largest clock (and keeping it outside the clock's redraw band). Only repaints
// when the line set changes, so it costs nothing on a steady screen.
static void drawClockInfo(int sw, int sh, int clockBottom, uint16_t bg, uint16_t clr) {
  char lines[MAX_ACTIVE_PRINTERS][40];
  int count = 0;

  if (dispSettings.showClockInfo) {
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (!isPrinterConfigured(i)) continue;
      const PrinterConfig& cfg = printers[i].config;
      const char* name = (cfg.name[0] != '\0') ? cfg.name : "Printer";
      // Prefer the IP reported by the printer (works in cloud mode too); fall
      // back to the configured LAN IP before the first pushall arrives.
      const char* ip = (printers[i].state.localIp[0] != '\0') ? printers[i].state.localIp
                     : (cfg.ip[0] != '\0') ? cfg.ip : nullptr;
      if (ip)
        snprintf(lines[count], sizeof(lines[count]), "%s  %s", name, ip);
      else
        snprintf(lines[count], sizeof(lines[count]), "%s", name);
      if (++count >= MAX_ACTIVE_PRINTERS) break;
    }
  }

  // Nothing changed since last paint? leave the screen alone.
  bool changed = (count != prevInfoCount);
  for (int i = 0; i < count && !changed; i++)
    if (strcmp(lines[i], prevInfoLines[i]) != 0) changed = true;
  if (!changed) return;

  setFont(tft, FONT_BODY);
  tft.setTextSize(1);
  const int lineH = tft.fontHeight() + 3;
#if defined(DISPLAY_ROUND_240)
  // The chord at the very bottom of the circle is too narrow for a name+IP
  // line; anchor the footer block higher, where the circle is ~160 px wide.
  const int bottomMargin = LY_H - LY_RND_CLK_INFO_Y;
#else
  const int bottomMargin = 4;
#endif
  const int maxRows = (count > prevInfoCount) ? count : prevInfoCount;
  const int blockTop = sh - bottomMargin - maxRows * lineH;

  // Clear the whole footer band (covers shrinking line counts too).
  tft.fillRect(0, blockTop, sw, maxRows * lineH + bottomMargin, bg);
  markFrameDirty();

#if defined(DISPLAY_ROUND_240)
  // The full-width band clear sweeps across the lower rim ticks (4-8 o'clock
  // live at y >= 176) and, with two footer lines, can reach the date row.
  // Invalidate both caches so they repaint: ticks on the next drawClock()
  // pass, the date later in this very pass (drawClockInfo runs before the
  // minute-change block, so resetting prevMinute redraws it immediately).
  roundTicksDrawn = false;
  if (blockTop < clockBottom + 4) {
    prevDateBuf[0] = '\0';
    prevMinute = -1;
  }
#endif

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(clr, bg);
  for (int i = 0; i < count; i++) {
    const int rowY = sh - bottomMargin - (count - i) * lineH + lineH / 2;
    // Hard guarantee against overlapping the clock: if a line would collide we
    // simply drop it rather than shrink the clock. At FONT_BODY size this can
    // happen on a short panel with a tall clock (e.g. 240x320 + Large clock +
    // two printers); every other combination has room.
    if (rowY - lineH / 2 < clockBottom + 4) continue;
    tft.drawString(lines[i], sw / 2, rowY);
  }

  prevInfoCount = count;
  for (int i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (i < count) strlcpy(prevInfoLines[i], lines[i], sizeof(prevInfoLines[i]));
    else prevInfoLines[i][0] = '\0';
  }
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
#if defined(LAYOUT_HAS_LANDSCAPE)
    const bool clkLand = (sw > sh);
    const int clkClearY = clkLand ? LY_LAND_CLK_CLEAR_Y : LY_CLK_CLEAR_Y;
    const int clkClearH = clkLand ? LY_LAND_CLK_CLEAR_H : LY_CLK_CLEAR_H;
#else
    const int clkClearY = LY_CLK_CLEAR_Y;
    const int clkClearH = LY_CLK_CLEAR_H;
#endif
    tft.fillRect(0, clkClearY, sw, clkClearH, bg);
    markFrameDirty();
    prevScale = scale;
    prevTimeX0 = timeX0;
    prevUse24h = netSettings.use24h;
    prevHideDate = dispSettings.hideClockDate;
#if defined(DISPLAY_ROUND_240)
    roundTicksDrawn = false;   // the wipe band crosses the 3/9 o'clock ticks
#endif
  }

#if defined(DISPLAY_ROUND_240)
  // --- Watch-face rim ticks (major at 12/3/6/9) ---
  if (!roundTicksDrawn) {
    roundTicksDrawn = true;
    markFrameDirty();
    for (int i = 0; i < 12; i++) {
      const bool major = (i % 3) == 0;
      const float a  = i * 30.0f * 0.0174532925f;
      const float sa = sinf(a), ca = cosf(a);
      const int16_t r0 = major ? LY_RND_CLK_TICK_RIM : LY_RND_CLK_TICK_RI;
      tft.drawLine(sw / 2 + (int16_t)(sa * r0),
                   sh / 2 - (int16_t)(ca * r0),
                   sw / 2 + (int16_t)(sa * LY_RND_CLK_TICK_RO),
                   sh / 2 - (int16_t)(ca * LY_RND_CLK_TICK_RO),
                   major ? dateClr : CLR_TEXT_DARK);
    }
  }

  // --- MQTT status dot at 12 o'clock (green = any printer connected) ---
  {
    int8_t dot = 0;
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (!isPrinterConfigured(i)) continue;
      dot = printers[i].state.connected ? 1 : 2;
      if (dot == 1) break;
    }
    if (dot != roundPrevDot) {
      markFrameDirty();
      uint16_t c = (dot == 1) ? CLR_GREEN : (dot == 2) ? CLR_RED : bg;
      tft.fillCircle(sw / 2, LY_RND_CLK_DOT_Y, 4, c);
      roundPrevDot = dot;
    }
  }
#endif // DISPLAY_ROUND_240

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

  // --- Printer info footer (name + LAN IP) ---
  // Evaluated every tick (cheap; only repaints on change) so it appears as soon
  // as the printer's IP arrives via pushall, not just on the next minute roll.
  {
    const int clockBottom = timeYTop + digitH +
                            (dispSettings.hideClockDate ? 0 : (DATE_GAP + DATE_FONT_H));
    drawClockInfo(sw, sh, clockBottom, bg, dateClr);
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
