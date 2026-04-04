/*
 * Pong/Breakout animated clock for BambuHelper
 * Ported from TFTClock project, adapted for TFT_eSPI.
 *
 * Ball bounces off paddle and breaks colored brick rows.
 * On minute change, digits break apart with fragment effects.
 */

#include "clock_pong.h"
#include "config.h"
#include "layout.h"
#include "settings.h"
#include "display_ui.h"
#include <TFT_eSPI.h>
#include <time.h>

extern TFT_eSPI tft;

// ========== Layout constants (from layout profile) ==========
#define ARK_BRICK_ROWS    LY_ARK_BRICK_ROWS
#define ARK_BRICK_COLS    LY_ARK_COLS
#define ARK_BRICK_W       LY_ARK_BRICK_W
#define ARK_BRICK_H       LY_ARK_BRICK_H
#define ARK_BRICK_GAP     LY_ARK_BRICK_GAP
#define ARK_BRICK_START_X LY_ARK_START_X
#define ARK_BRICK_START_Y LY_ARK_START_Y
#define ARK_PADDLE_Y      LY_ARK_PADDLE_Y
#define ARK_PADDLE_W      LY_ARK_PADDLE_W
#define ARK_TIME_Y        LY_ARK_TIME_Y
#define ARK_DATE_Y        LY_ARK_DATE_Y
#define DIGIT_W           LY_ARK_DIGIT_W
#define DIGIT_H           LY_ARK_DIGIT_H
#define COLON_W           LY_ARK_COLON_W

// ========== Gameplay constants (not layout-dependent) ==========
#define ARK_BALL_SIZE     4
#define ARK_PADDLE_H      4
#define ARK_BALL_SPEED    3.0f
#define ARK_PADDLE_SPEED  4
#define ARK_UPDATE_MS     20    // ~50fps
#define ARK_MAX_FRAGS     20
#define ARK_TIME_OVERRIDE_MS 60000

// Brick colors per row (classic Arcanoid rainbow).
// BRICK_COLOR_COUNT is the authoritative size; ARK_BRICK_ROWS must not exceed it.
#define BRICK_COLOR_COUNT 5
static const uint16_t brickColors[BRICK_COLOR_COUNT] = {
  0xF800,  // Red
  0xFD20,  // Orange
  0xFFE0,  // Yellow
  0x07E0,  // Green
  0x001F,  // Blue
};
static_assert(ARK_BRICK_ROWS <= BRICK_COLOR_COUNT,
              "ARK_BRICK_ROWS exceeds brickColors array size");

// ========== Fragment struct ==========
struct PongFragment {
  float x, y, vx, vy;
  bool active;
};

// ========== State ==========
static bool arkBricks[ARK_BRICK_ROWS][ARK_BRICK_COLS];
static int arkBrickCount = 0;

static float ballX, ballY, ballVX, ballVY;
static bool ballActive = false;
static int prevBallX = -1, prevBallY = -1;

static int paddleX = LY_W / 2, prevPaddleX = LY_W / 2;
static uint8_t paddleBouncesWithoutHit = 0;

static bool initialized = false;
static unsigned long lastUpdateMs = 0;
static int lastMinute = -1;
static bool animTriggered = false;

// Digit transition
static int dispHour = 0, dispMin = 0;
static bool timeOverridden = false;
static unsigned long timeOverrideStart = 0;

static int targetDigits[4], targetValues[4];
static int numTargets = 0, currentTarget = 0;
static bool breaking = false;

static PongFragment frags[ARK_MAX_FRAGS];
static int fragTimer = 0;

// Digit bounce
static float digitOffsetY[5] = {0};
static float digitVelocity[5] = {0};
static int prevDigitY[5] = {0};

// Draw cache - avoid redrawing unchanged digits
static char prevDigits[6] = {0};  // 5 chars + null
static bool prevColon = false;
static char prevDateStr[28] = {0};

// ========== Digit bounce (inlined) ==========
static void triggerBounce(int i) {
  if (i >= 0 && i < 5) digitVelocity[i] = -6.0f;
}

static void updateBounce() {
  static unsigned long lastPhys = 0;
  unsigned long now = millis();
  float dt = (now - lastPhys) / 1000.0f;
  if (dt > 0.1f || lastPhys == 0) dt = 0.025f;
  lastPhys = now;
  float scale = dt / 0.05f;

  for (int i = 0; i < 5; i++) {
    if (digitOffsetY[i] != 0 || digitVelocity[i] != 0) {
      digitVelocity[i] += 3.0f * scale;
      digitOffsetY[i] += digitVelocity[i] * scale;
      if (digitOffsetY[i] >= 0) { digitOffsetY[i] = 0; digitVelocity[i] = 0; }
    }
  }
}

// ========== Colon blink ==========
static bool showColon() {
  return (millis() % 1000) < 500;
}

// ========== Bricks ==========
static void initBricks() {
  arkBrickCount = 0;
  for (int r = 0; r < ARK_BRICK_ROWS; r++)
    for (int c = 0; c < ARK_BRICK_COLS; c++) {
      arkBricks[r][c] = true;
      arkBrickCount++;
    }
}

static void drawBrick(int r, int c) {
  int x = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, brickColors[r]);
  tft.drawFastHLine(x, y, ARK_BRICK_W, TFT_WHITE);
  tft.drawFastHLine(x, y + ARK_BRICK_H - 1, ARK_BRICK_W, TFT_BLACK);
}

static void drawAllBricks() {
  for (int r = 0; r < ARK_BRICK_ROWS; r++)
    for (int c = 0; c < ARK_BRICK_COLS; c++)
      if (arkBricks[r][c]) drawBrick(r, c);
}

static void clearBrick(int r, int c) {
  int x = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, TFT_BLACK);
}

// ========== Ball ==========
static void spawnBall() {
  ballX = paddleX;
  ballY = ARK_PADDLE_Y - ARK_BALL_SIZE - 2;
  float angle = random(40, 140) * PI / 180.0f;
  ballVX = ARK_BALL_SPEED * cos(angle);
  ballVY = -ARK_BALL_SPEED * sin(angle);
  // Ensure minimum horizontal movement
  if (fabsf(ballVX) < 1.2f) ballVX = (ballVX >= 0) ? 1.2f : -1.2f;
  if (fabsf(ballVY) < 1.0f) ballVY = -2.0f;
  ballActive = true;
  paddleBouncesWithoutHit = 0;
}

static int digitX(int i);  // forward declaration

// Mark cached text dirty if ball erase overlaps it
static void markBallDamage(int bx, int by) {
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  // Check time digits and colon
  for (int i = 0; i < 5; i++) {
    int dx = digitX(i);
    int dw = (i == 2) ? COLON_W : DIGIT_W + 2;
    int dy = (i == 2) ? ARK_TIME_Y : (prevDigitY[i] ? prevDigitY[i] : ARK_TIME_Y);
    if (bx2 > dx && bx < dx + dw && by2 > dy && by < dy + DIGIT_H) {
      if (i == 2) prevColon = !showColon();  // force colon redraw
      else prevDigits[i] = 0;
    }
  }
  // Check date area
  if (by2 > ARK_DATE_Y && by < ARK_DATE_Y + 16) {
    prevDateStr[0] = '\0';
  }
}

// Check if ball rect overlaps the time digits or date text
static bool overlapsText(int bx, int by) {
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  // Time digits zone (exact bounds of HH:MM)
  if (by2 > ARK_TIME_Y && by < ARK_TIME_Y + DIGIT_H) {
    int tLeft = digitX(0) - 2;
    int tRight = digitX(4) + DIGIT_W + 4;
    if (bx2 > tLeft && bx < tRight) return true;
  }
  // Date zone (centered text ~150px wide)
  if (by2 > ARK_DATE_Y && by < ARK_DATE_Y + 16) {
    if (bx2 > 40 && bx < 200) return true;
  }
  return false;
}

static void drawBall() {
  if (prevBallX >= 0) {
    // Ball passes behind text — no erase needed, no damage to repair
    if (overlapsText(prevBallX, prevBallY)) {
      // skip — ball was never drawn here
    } else {
      tft.fillRect(prevBallX, prevBallY, ARK_BALL_SIZE, ARK_BALL_SIZE, TFT_BLACK);
    }
  }
  // Don't draw ball over time digits — it passes behind
  if (!overlapsText((int)ballX, (int)ballY)) {
    tft.fillRect((int)ballX, (int)ballY, ARK_BALL_SIZE, ARK_BALL_SIZE, TFT_WHITE);
  }
  prevBallX = (int)ballX;
  prevBallY = (int)ballY;
}

// ========== Paddle ==========
static void drawPaddle() {
  if (prevPaddleX != paddleX)
    tft.fillRect(prevPaddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, ARK_PADDLE_H, TFT_BLACK);
  tft.fillRect(paddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, ARK_PADDLE_H, TFT_CYAN);
  tft.drawFastHLine(paddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, TFT_WHITE);
  prevPaddleX = paddleX;
}

static void updatePaddle() {
  int target;
  if (ballActive && ballVY > 0) {
    // Ball falling - follow ball X position with slight offset for variety
    target = (int)ballX;
  } else {
    // Ball going up - drift toward center for variety
    target = LY_W / 2;
  }
  if (paddleX < target - 2) paddleX += ARK_PADDLE_SPEED;
  else if (paddleX > target + 2) paddleX -= ARK_PADDLE_SPEED;
  if (paddleX < ARK_PADDLE_W / 2) paddleX = ARK_PADDLE_W / 2;
  if (paddleX > SCREEN_W - ARK_PADDLE_W / 2) paddleX = SCREEN_W - ARK_PADDLE_W / 2;
}

// ========== Brick collision ==========
static bool checkBrickCollision() {
  int bx = (int)ballX, by = (int)ballY;
  bool hitX = false, hitY = false;
  bool hit = false;
  for (int r = 0; r < ARK_BRICK_ROWS; r++) {
    for (int c = 0; c < ARK_BRICK_COLS; c++) {
      if (!arkBricks[r][c]) continue;
      int rx = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
      int ry = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
      if (bx + ARK_BALL_SIZE > rx && bx < rx + ARK_BRICK_W &&
          by + ARK_BALL_SIZE > ry && by < ry + ARK_BRICK_H) {
        arkBricks[r][c] = false;
        arkBrickCount--;
        clearBrick(r, c);
        // Determine bounce axis from overlap (accumulated, not per-brick)
        float oL = (bx + ARK_BALL_SIZE) - rx, oR = (rx + ARK_BRICK_W) - bx;
        float oT = (by + ARK_BALL_SIZE) - ry, oB = (ry + ARK_BRICK_H) - by;
        if (min(oL, oR) < min(oT, oB)) hitX = true; else hitY = true;
        hit = true;
      }
    }
  }
  if (hitX) ballVX = -ballVX;
  if (hitY) ballVY = -ballVY;
  // Push ball out of brick zone to prevent re-collision next frame
  if (hit) {
    ballX += ballVX;
    ballY += ballVY;
    paddleBouncesWithoutHit = 0;
  }
  return hit;
}

// ========== Ball physics ==========
static void updateBallPhysics() {
  if (!ballActive) return;
  ballX += ballVX;
  ballY += ballVY;

  if (ballX <= 0) { ballX = 0; ballVX = fabsf(ballVX); }
  if (ballX >= SCREEN_W - ARK_BALL_SIZE) { ballX = SCREEN_W - ARK_BALL_SIZE; ballVX = -fabsf(ballVX); }
  if (ballY <= 0) { ballY = 0; ballVY = fabsf(ballVY); }

  // Paddle collision
  if (ballVY > 0 && ballY + ARK_BALL_SIZE >= ARK_PADDLE_Y && ballY < ARK_PADDLE_Y + ARK_PADDLE_H) {
    int pL = paddleX - ARK_PADDLE_W / 2, pR = paddleX + ARK_PADDLE_W / 2;
    if (ballX + ARK_BALL_SIZE >= pL && ballX <= pR) {
      ballY = ARK_PADDLE_Y - ARK_BALL_SIZE;
      float hit = (ballX + ARK_BALL_SIZE / 2.0f - pL) / (float)ARK_PADDLE_W;
      // Wider angle range: 30-150 degrees (was 30-150 but with steeper minimum)
      float angle = (150.0f - hit * 120.0f) * PI / 180.0f;
      ballVX = ARK_BALL_SPEED * cos(angle);
      ballVY = -ARK_BALL_SPEED * sin(angle);
      // Enforce minimum horizontal speed for more natural movement
      if (fabsf(ballVX) < 1.2f) ballVX = (ballVX >= 0) ? 1.2f : -1.2f;
      if (fabsf(ballVY) < 1.0f) ballVY = -1.5f;
      paddleBouncesWithoutHit++;
      // Ball stuck in a loop missing the last brick(s) - reset the board
      if (paddleBouncesWithoutHit >= 20) {
        paddleBouncesWithoutHit = 0;
        initBricks();
        drawAllBricks();
      }
    }
  }

  if (ballY > SCREEN_H) spawnBall();
  checkBrickCollision();
  if (arkBrickCount <= 0) { initBricks(); drawAllBricks(); }
}

// ========== Fragments ==========
static void spawnDigitFragments(int dx, int dy) {
  for (int i = 0; i < ARK_MAX_FRAGS; i++) {
    frags[i].active = true;
    frags[i].x = dx + random(0, DIGIT_W);
    frags[i].y = dy + random(0, DIGIT_H);
    frags[i].vx = random(-30, 30) / 10.0f;
    frags[i].vy = random(-40, -10) / 10.0f;
  }
  fragTimer = 30;
}

static void updateFragments() {
  if (fragTimer <= 0) return;
  fragTimer--;
  for (int i = 0; i < ARK_MAX_FRAGS; i++) {
    if (!frags[i].active) continue;
    tft.fillRect((int)frags[i].x, (int)frags[i].y, 3, 3, TFT_BLACK);
    frags[i].x += frags[i].vx;
    frags[i].y += frags[i].vy;
    frags[i].vy += 0.3f;
    if (frags[i].y > SCREEN_H || frags[i].x < -10 || frags[i].x > SCREEN_W + 10) {
      frags[i].active = false;
      continue;
    }
    tft.fillRect((int)frags[i].x, (int)frags[i].y, 3, 3, brickColors[random(0, BRICK_COLOR_COUNT)]);
  }
  if (fragTimer == 0) {
    for (int i = 0; i < ARK_MAX_FRAGS; i++) {
      if (frags[i].active) {
        tft.fillRect((int)frags[i].x, (int)frags[i].y, 3, 3, TFT_BLACK);
        frags[i].active = false;
      }
    }
  }
}

// ========== Calculate which digits change ==========
static void calcTargets(int hour, int mn) {
  numTargets = 0;
  int newMin = mn + 1, newHour = hour;
  if (newMin >= 60) { newMin = 0; newHour = (newHour + 1) % 24; }
  int oldD[4] = {hour / 10, hour % 10, mn / 10, mn % 10};
  int newD[4] = {newHour / 10, newHour % 10, newMin / 10, newMin % 10};
  int map[4] = {0, 1, 3, 4};
  for (int i = 3; i >= 0; i--) {
    if (oldD[i] != newD[i]) {
      targetDigits[numTargets] = map[i];
      targetValues[numTargets] = newD[i];
      numTargets++;
    }
  }
}

// ========== Update a digit value after break ==========
static void applyDigitValue(int di, int dv) {
  int ht = dispHour / 10, ho = dispHour % 10;
  int mt = dispMin / 10, mo = dispMin % 10;
  if (di == 0) { ht = dv; dispHour = ht * 10 + ho; }
  else if (di == 1) { ho = dv; dispHour = ht * 10 + ho; }
  else if (di == 3) { mt = dv; dispMin = mt * 10 + mo; }
  else if (di == 4) { mo = dv; dispMin = mt * 10 + mo; }
}

// ========== Draw time with Font 7 (smooth 7-segment) ==========
// Layout: HH:MM centered horizontally
// Each digit ~32px wide, colon ~12px, total ~148px

// X positions for 5 slots: d0 d1 : d3 d4
// Total width: 4*DIGIT_W + COLON_W = 4*32 + 12 = 140
#define TIME_TOTAL_W  (4 * DIGIT_W + COLON_W)
#define TIME_START_X  ((SCREEN_W - TIME_TOTAL_W) / 2)

static int digitX(int i) {
  // i: 0,1 = hour digits, 2 = colon, 3,4 = minute digits
  if (i < 2) return TIME_START_X + i * DIGIT_W;
  if (i == 2) return TIME_START_X + 2 * DIGIT_W;  // colon position
  return TIME_START_X + 2 * DIGIT_W + COLON_W + (i - 3) * DIGIT_W;
}

static void drawTime() {
  tft.setTextSize(1);  // no scaling, Font 7 is already 48px
  char digits[5];
  if (netSettings.use24h) {
    digits[0] = '0' + (dispHour / 10);
    digits[1] = '0' + (dispHour % 10);
  } else {
    int h = dispHour % 12;
    if (h == 0) h = 12;
    digits[0] = (h >= 10) ? '1' : ' ';
    digits[1] = '0' + (h % 10);
  }
  digits[2] = ':';
  digits[3] = '0' + (dispMin / 10);
  digits[4] = '0' + (dispMin % 10);

  bool colon = showColon();

  // Draw digits - only redraw changed ones
  tft.setTextFont(7);
  tft.setTextSize(1);
  tft.setTextColor(dispSettings.clockTimeColor, TFT_BLACK);

  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;  // skip colon slot, handled separately

    int x = digitX(i);
    int y = ARK_TIME_Y + (int)digitOffsetY[i];
    bool bouncing = (digitOffsetY[i] != 0 || digitVelocity[i] != 0);
    bool changed = (digits[i] != prevDigits[i]) || bouncing || (prevDigitY[i] != y);

    if (changed) {
      // Clear previous and current position
      int clearW = DIGIT_W + 2;  // +2 margin for font rendering
      if (prevDigitY[i] != 0)
        tft.fillRect(x, prevDigitY[i], clearW, DIGIT_H, TFT_BLACK);
      if (prevDigitY[i] != y)
        tft.fillRect(x, y, clearW, DIGIT_H, TFT_BLACK);

      tft.drawChar(digits[i], x, y, 7);
      prevDigits[i] = digits[i];
      prevDigitY[i] = y;
    }
  }

  // Colon - blinks, draw only when state changes
  if (colon != prevColon) {
    int cx = digitX(2);
    tft.fillRect(cx, ARK_TIME_Y, COLON_W, DIGIT_H, TFT_BLACK);
    if (colon) {
      tft.drawChar(':', cx, ARK_TIME_Y, 7);
    }
    prevColon = colon;
  }

  // AM/PM for 12h mode
  if (!netSettings.use24h) {
    tft.setTextFont(2);
    tft.setTextColor(dispSettings.clockDateColor, TFT_BLACK);
    int ampmX = digitX(4) + DIGIT_W + 2;
    tft.setCursor(ampmX, ARK_TIME_Y + DIGIT_H - 16);
    tft.print(dispHour < 12 ? "AM" : "PM");
  }
}

// ========== Reset ==========
void resetPongClock() {
  initialized = false;
}

// ========== Tick (call from loop, runs at own cadence) ==========
void tickPongClock() {
  if (!dpSettings.showClockAfterFinish && !dpSettings.keepDisplayOn) return;
  if (!dispSettings.pongClock) return;

  struct tm now;
  if (!getLocalTime(&now, 0)) return;

  // First-time init
  if (!initialized) {
    tft.fillScreen(TFT_BLACK);
    initBricks();
    drawAllBricks();
    spawnBall();
    dispHour = now.tm_hour;
    dispMin = now.tm_min;
    initialized = true;
    lastMinute = now.tm_min;
    for (int i = 0; i < 5; i++) { prevDigitY[i] = 0; digitOffsetY[i] = 0; digitVelocity[i] = 0; }
    for (int i = 0; i < ARK_MAX_FRAGS; i++) frags[i].active = false;
    memset(prevDigits, 0, sizeof(prevDigits));
    prevColon = false;
    prevBallX = -1;
    prevPaddleX = paddleX;
    // Force date redraw after fillScreen
    prevDateStr[0] = '\0';
  }

  // Time management
  if (!timeOverridden) { dispHour = now.tm_hour; dispMin = now.tm_min; }
  if (timeOverridden) {
    bool matches = (now.tm_hour == dispHour && now.tm_min == dispMin);
    bool timeout = (millis() - timeOverrideStart > ARK_TIME_OVERRIDE_MS);
    if (matches || timeout) {
      timeOverridden = false;
      if (timeout && !matches) { dispHour = now.tm_hour; dispMin = now.tm_min; }
    }
  }

  // Throttle to ~50fps
  unsigned long ms = millis();
  if (ms - lastUpdateMs < ARK_UPDATE_MS) return;
  lastUpdateMs = ms;

  int sec = now.tm_sec;
  int curMin = now.tm_min;

  // Detect minute change
  if (curMin != lastMinute) { lastMinute = curMin; animTriggered = false; }

  // Trigger digit transition at second 56 — all changing digits update
  // at once with a bounce animation (no fragment explosion)
  if (sec >= 56 && !animTriggered) {
    animTriggered = true;
    calcTargets(dispHour, dispMin);
    for (int t = 0; t < numTargets; t++) {
      int di = targetDigits[t];
      applyDigitValue(di, targetValues[t]);
      prevDigits[di] = 0;  // force redraw
      triggerBounce(di);
    }
    if (numTargets > 0) {
      timeOverridden = true;
      timeOverrideStart = millis();
    }
  }

  // Physics
  updateBallPhysics();
  updatePaddle();
  updateBounce();

  // Draw ball and paddle first (they erase and may damage text)
  drawBall();
  drawPaddle();

  // Draw text on top (repairs any ball/paddle damage via cache invalidation)
  // Date (Font 2, smooth)
  {
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char dateStr[28];
    int day = now.tm_mday, mon = now.tm_mon + 1, year = now.tm_year + 1900;
    switch (netSettings.dateFormat) {
      case 1:  snprintf(dateStr, sizeof(dateStr), "%s %02d-%02d-%04d", days[now.tm_wday], day, mon, year); break;
      case 2:  snprintf(dateStr, sizeof(dateStr), "%s %02d/%02d/%04d", days[now.tm_wday], mon, day, year); break;
      case 3:  snprintf(dateStr, sizeof(dateStr), "%s %04d-%02d-%02d", days[now.tm_wday], year, mon, day); break;
      case 4:  snprintf(dateStr, sizeof(dateStr), "%s %d %s %04d", days[now.tm_wday], day, months[now.tm_mon], year); break;
      case 5:  snprintf(dateStr, sizeof(dateStr), "%s %s %d, %04d", days[now.tm_wday], months[now.tm_mon], day, year); break;
      default: snprintf(dateStr, sizeof(dateStr), "%s %02d.%02d.%04d", days[now.tm_wday], day, mon, year); break;
    }
    if (strcmp(dateStr, prevDateStr) != 0) {
      tft.setTextFont(2);
      tft.setTextSize(1);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(dispSettings.clockDateColor, TFT_BLACK);
      tft.fillRect(LY_ARK_DATE_CLR_X, ARK_DATE_Y, LY_ARK_DATE_CLR_W, 16, TFT_BLACK);
      tft.drawString(dateStr, SCREEN_W / 2, ARK_DATE_Y);
      tft.setTextDatum(TL_DATUM);
      strlcpy(prevDateStr, dateStr, sizeof(prevDateStr));
    }
  }

  // Time (Font 7, on top of everything)
  drawTime();
}
