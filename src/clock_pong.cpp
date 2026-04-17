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
#include <time.h>

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
#define TIME_TOTAL_W      (4 * DIGIT_W + COLON_W)
#define ARK_MAX_BRICK_COLS 13
#define ARK_LAND_BRICK_COLS 13

// ========== Gameplay constants (not layout-dependent) ==========
#define ARK_BALL_SIZE     4
#define ARK_PADDLE_H      4
#define ARK_BALL_SPEED    3.0f
#define ARK_PADDLE_SPEED  4
#define ARK_UPDATE_MS     20    // ~50fps
#define ARK_MAX_FRAGS     20
#define ARK_TIME_OVERRIDE_MS 60000
#define ARK_PADDLE_JITTER_DEG 4.0f
#define ARK_STICKY_JITTER_DEG 10.0f
#define ARK_STICKY_MS     120
#define ARK_STICKY_TRIGGER_BOUNCES 4

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
static_assert(ARK_BRICK_COLS <= ARK_MAX_BRICK_COLS,
              "ARK_BRICK_COLS exceeds arkBricks storage");
static_assert(ARK_LAND_BRICK_COLS <= ARK_MAX_BRICK_COLS,
              "ARK_LAND_BRICK_COLS exceeds arkBricks storage");

// ========== Fragment struct ==========
struct PongFragment {
  float x, y, vx, vy;
  bool active;
};

struct PongLayout {
  int16_t screenW;
  int16_t screenH;
  int16_t brickRows;
  int16_t brickCols;
  int16_t brickStartX;
  int16_t brickStartY;
  int16_t paddleY;
  int16_t paddleW;
  int16_t timeY;
  int16_t dateY;
  int16_t timeStartX;
  int16_t dateClearX;
  int16_t dateClearW;
};

// ========== State ==========
static bool arkBricks[ARK_BRICK_ROWS][ARK_MAX_BRICK_COLS];
static int arkBrickCount = 0;

static float ballX, ballY, ballVX, ballVY;
static bool ballActive = false;
static bool ballStuckToPaddle = false;
static unsigned long ballStickyUntilMs = 0;
static float ballStickyHit = 0.5f;
static int prevBallX = -1, prevBallY = -1;

static PongLayout pongLayout = {
  SCREEN_W,
  SCREEN_H,
  ARK_BRICK_ROWS,
  ARK_BRICK_COLS,
  ARK_BRICK_START_X,
  ARK_BRICK_START_Y,
  ARK_PADDLE_Y,
  ARK_PADDLE_W,
  ARK_TIME_Y,
  ARK_DATE_Y,
  (SCREEN_W - TIME_TOTAL_W) / 2,
  LY_ARK_DATE_CLR_X,
  LY_ARK_DATE_CLR_W
};

static int paddleX = SCREEN_W / 2, prevPaddleX = SCREEN_W / 2;
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

static void refreshPongLayout() {
  PongLayout next = {
    SCREEN_W,
    SCREEN_H,
    ARK_BRICK_ROWS,
    ARK_BRICK_COLS,
    ARK_BRICK_START_X,
    ARK_BRICK_START_Y,
    ARK_PADDLE_Y,
    ARK_PADDLE_W,
    ARK_TIME_Y,
    ARK_DATE_Y,
    0,
    LY_ARK_DATE_CLR_X,
    LY_ARK_DATE_CLR_W
  };

#if defined(DISPLAY_240x320)
  const int16_t scrW = (int16_t)tft.width();
  const int16_t scrH = (int16_t)tft.height();
  if (scrW > 0 && scrH > 0) {
    next.screenW = scrW;
    next.screenH = scrH;
    if (scrW > scrH) {
      next.brickCols = ARK_LAND_BRICK_COLS;
      next.brickStartX = (scrW - (next.brickCols * ARK_BRICK_W +
                                  (next.brickCols - 1) * ARK_BRICK_GAP)) / 2;
      next.brickStartY = 28;
      next.paddleY = 224;
      next.paddleW = 30;
      next.timeY = 130;
      next.dateY = 8;
      next.dateClearX = 0;
      next.dateClearW = scrW;
    }
  }
#endif

  next.timeStartX = (next.screenW - TIME_TOTAL_W) / 2;
  pongLayout = next;
}

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
  const PongLayout& layout = pongLayout;
  arkBrickCount = 0;
  for (int r = 0; r < ARK_BRICK_ROWS; r++) {
    for (int c = 0; c < ARK_MAX_BRICK_COLS; c++) {
      bool active = (r < layout.brickRows && c < layout.brickCols);
      arkBricks[r][c] = active;
      if (active) arkBrickCount++;
    }
  }
}

static void drawBrick(int r, int c) {
  const PongLayout& layout = pongLayout;
  int x = layout.brickStartX + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = layout.brickStartY + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, brickColors[r]);
  tft.drawFastHLine(x, y, ARK_BRICK_W, TFT_WHITE);
  tft.drawFastHLine(x, y + ARK_BRICK_H - 1, ARK_BRICK_W, TFT_BLACK);
}

static void drawAllBricks() {
  const PongLayout& layout = pongLayout;
  for (int r = 0; r < layout.brickRows; r++)
    for (int c = 0; c < layout.brickCols; c++)
      if (arkBricks[r][c]) drawBrick(r, c);
}

static void clearBrick(int r, int c) {
  const PongLayout& layout = pongLayout;
  int x = layout.brickStartX + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = layout.brickStartY + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, TFT_BLACK);
}

// ========== Ball ==========
static void launchBallFromPaddle(float hit, float jitterDeg) {
  const PongLayout& layout = pongLayout;
  hit = constrain(hit, 0.08f, 0.92f);
  ballX = paddleX - layout.paddleW / 2 + hit * layout.paddleW - ARK_BALL_SIZE / 2.0f;
  ballY = layout.paddleY - ARK_BALL_SIZE;

  float angleDeg = 150.0f - hit * 120.0f + jitterDeg;
  if (angleDeg < 30.0f) angleDeg = 30.0f;
  if (angleDeg > 150.0f) angleDeg = 150.0f;
  float angle = angleDeg * PI / 180.0f;

  ballVX = ARK_BALL_SPEED * cos(angle);
  ballVY = -ARK_BALL_SPEED * sin(angle);
  if (fabsf(ballVX) < 1.2f) ballVX = (ballVX >= 0) ? 1.2f : -1.2f;
  if (fabsf(ballVY) < 1.0f) ballVY = -1.5f;

  ballActive = true;
  ballStuckToPaddle = false;
  ballStickyUntilMs = 0;
}

static bool shouldUseStickyAssist() {
  if (paddleBouncesWithoutHit < ARK_STICKY_TRIGGER_BOUNCES) return false;
  int chancePct = 15 + (paddleBouncesWithoutHit - ARK_STICKY_TRIGGER_BOUNCES) * 10;
  if (chancePct > 70) chancePct = 70;
  return random(100) < chancePct;
}

static void stickBallToPaddle(float hit) {
  const PongLayout& layout = pongLayout;
  float assistOffset = random(-18, 19) / 100.0f;
  ballStickyHit = constrain(hit + assistOffset, 0.18f, 0.82f);
  ballStuckToPaddle = true;
  ballStickyUntilMs = millis() + ARK_STICKY_MS;
  ballActive = true;
  ballVX = 0;
  ballVY = 0;
  ballX = paddleX - layout.paddleW / 2 + ballStickyHit * layout.paddleW - ARK_BALL_SIZE / 2.0f;
  ballY = layout.paddleY - ARK_BALL_SIZE;
}

static void spawnBall() {
  ballStuckToPaddle = false;
  ballStickyUntilMs = 0;
  launchBallFromPaddle(0.5f, (float)random(-50, 51) / 10.0f);
  paddleBouncesWithoutHit = 0;
}

static int digitX(int i);  // forward declaration

// Mark cached text dirty if ball erase overlaps it
static void markBallDamage(int bx, int by) {
  const PongLayout& layout = pongLayout;
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  // Check time digits and colon
  for (int i = 0; i < 5; i++) {
    int dx = digitX(i);
    int dw = (i == 2) ? COLON_W : DIGIT_W + 2;
    int dy = (i == 2) ? layout.timeY : (prevDigitY[i] ? prevDigitY[i] : layout.timeY);
    if (bx2 > dx && bx < dx + dw && by2 > dy && by < dy + DIGIT_H) {
      if (i == 2) prevColon = !showColon();  // force colon redraw
      else prevDigits[i] = 0;
    }
  }
  // Check date area
  if (by2 > layout.dateY && by < layout.dateY + 16) {
    prevDateStr[0] = '\0';
  }
}

// Check if ball rect overlaps the time digits or date text
static bool overlapsText(int bx, int by) {
  const PongLayout& layout = pongLayout;
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  // Time digits zone (exact bounds of HH:MM)
  if (by2 > layout.timeY && by < layout.timeY + DIGIT_H) {
    int tLeft = digitX(0) - 2;
    int tRight = digitX(4) + DIGIT_W + 4;
    if (bx2 > tLeft && bx < tRight) return true;
  }
  // Date zone (centered text clear width, scaled to the active screen width)
  if (by2 > layout.dateY && by < layout.dateY + 16) {
    int dateLeft = (layout.screenW - LY_ARK_DATE_CLR_W) / 2;
    int dateRight = dateLeft + LY_ARK_DATE_CLR_W;
    if (bx2 > dateLeft && bx < dateRight) return true;
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
  const PongLayout& layout = pongLayout;
  if (prevPaddleX != paddleX)
    tft.fillRect(prevPaddleX - layout.paddleW / 2, layout.paddleY, layout.paddleW, ARK_PADDLE_H, TFT_BLACK);
  tft.fillRect(paddleX - layout.paddleW / 2, layout.paddleY, layout.paddleW, ARK_PADDLE_H, TFT_CYAN);
  tft.drawFastHLine(paddleX - layout.paddleW / 2, layout.paddleY, layout.paddleW, TFT_WHITE);
  prevPaddleX = paddleX;
}

static void updatePaddle() {
  const PongLayout& layout = pongLayout;
  if (ballStuckToPaddle) return;

  int target;
  if (ballActive && ballVY > 0) {
    // Ball falling - follow ball X position with slight offset for variety
    target = (int)ballX;
  } else {
    // Ball going up - drift toward center for variety
    target = layout.screenW / 2;
  }
  if (paddleX < target - 2) paddleX += ARK_PADDLE_SPEED;
  else if (paddleX > target + 2) paddleX -= ARK_PADDLE_SPEED;
  if (paddleX < layout.paddleW / 2) paddleX = layout.paddleW / 2;
  if (paddleX > layout.screenW - layout.paddleW / 2)
    paddleX = layout.screenW - layout.paddleW / 2;
}

// ========== Brick collision ==========
static bool checkBrickCollision() {
  const PongLayout& layout = pongLayout;
  int bx = (int)ballX, by = (int)ballY;
  bool hitX = false, hitY = false;
  bool hit = false;
  for (int r = 0; r < layout.brickRows; r++) {
    for (int c = 0; c < layout.brickCols; c++) {
      if (!arkBricks[r][c]) continue;
      int rx = layout.brickStartX + c * (ARK_BRICK_W + ARK_BRICK_GAP);
      int ry = layout.brickStartY + r * (ARK_BRICK_H + ARK_BRICK_GAP);
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
  const PongLayout& layout = pongLayout;
  if (!ballActive) return;
  if (ballStuckToPaddle) {
    ballX = paddleX - layout.paddleW / 2 + ballStickyHit * layout.paddleW - ARK_BALL_SIZE / 2.0f;
    ballY = layout.paddleY - ARK_BALL_SIZE;
    if ((long)(millis() - ballStickyUntilMs) >= 0) {
      float jitter = (float)random(-(int)(ARK_STICKY_JITTER_DEG * 10.0f),
                                   (int)(ARK_STICKY_JITTER_DEG * 10.0f) + 1) / 10.0f;
      launchBallFromPaddle(ballStickyHit, jitter);
    }
    return;
  }

  ballX += ballVX;
  ballY += ballVY;

  if (ballX <= 0) { ballX = 0; ballVX = fabsf(ballVX); }
  if (ballX >= layout.screenW - ARK_BALL_SIZE) {
    ballX = layout.screenW - ARK_BALL_SIZE;
    ballVX = -fabsf(ballVX);
  }
  if (ballY <= 0) { ballY = 0; ballVY = fabsf(ballVY); }

  // Paddle collision
  if (ballVY > 0 && ballY + ARK_BALL_SIZE >= layout.paddleY &&
      ballY < layout.paddleY + ARK_PADDLE_H) {
    int pL = paddleX - layout.paddleW / 2, pR = paddleX + layout.paddleW / 2;
    if (ballX + ARK_BALL_SIZE >= pL && ballX <= pR) {
      float hit = (ballX + ARK_BALL_SIZE / 2.0f - pL) / (float)layout.paddleW;
      paddleBouncesWithoutHit++;
      if (shouldUseStickyAssist()) {
        stickBallToPaddle(hit);
      } else {
        float jitter = (float)random(-(int)(ARK_PADDLE_JITTER_DEG * 10.0f),
                                     (int)(ARK_PADDLE_JITTER_DEG * 10.0f) + 1) / 10.0f;
        launchBallFromPaddle(hit, jitter);
      }
      // Ball stuck in a loop missing the last brick(s) - reset the board
      if (paddleBouncesWithoutHit >= 20) {
        paddleBouncesWithoutHit = 0;
        initBricks();
        drawAllBricks();
      }
    }
  }

  if (ballY > layout.screenH) spawnBall();
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
  const PongLayout& layout = pongLayout;
  if (fragTimer <= 0) return;
  fragTimer--;
  for (int i = 0; i < ARK_MAX_FRAGS; i++) {
    if (!frags[i].active) continue;
    tft.fillRect((int)frags[i].x, (int)frags[i].y, 3, 3, TFT_BLACK);
    frags[i].x += frags[i].vx;
    frags[i].y += frags[i].vy;
    frags[i].vy += 0.3f;
    if (frags[i].y > layout.screenH || frags[i].x < -10 || frags[i].x > layout.screenW + 10) {
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

static int digitX(int i) {
  const PongLayout& layout = pongLayout;
  // i: 0,1 = hour digits, 2 = colon, 3,4 = minute digits
  if (i < 2) return layout.timeStartX + i * DIGIT_W;
  if (i == 2) return layout.timeStartX + 2 * DIGIT_W;  // colon position
  return layout.timeStartX + 2 * DIGIT_W + COLON_W + (i - 3) * DIGIT_W;
}

static void drawTime() {
  const PongLayout& layout = pongLayout;
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
    int y = layout.timeY + (int)digitOffsetY[i];
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
    tft.fillRect(cx, layout.timeY, COLON_W, DIGIT_H, TFT_BLACK);
    if (colon) {
      tft.drawChar(':', cx, layout.timeY, 7);
    }
    prevColon = colon;
  }

  // AM/PM for 12h mode
  if (!netSettings.use24h) {
    tft.setTextFont(2);
    tft.setTextColor(dispSettings.clockDateColor, TFT_BLACK);
    int ampmX = digitX(4) + DIGIT_W + 2;
    tft.setCursor(ampmX, layout.timeY + DIGIT_H - 16);
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

  PongLayout prevLayout = pongLayout;
  refreshPongLayout();
  if (initialized &&
      (prevLayout.screenW != pongLayout.screenW ||
       prevLayout.screenH != pongLayout.screenH ||
       prevLayout.brickCols != pongLayout.brickCols ||
       prevLayout.paddleY != pongLayout.paddleY ||
       prevLayout.timeY != pongLayout.timeY)) {
    initialized = false;
  }

  struct tm now;
  if (!getLocalTime(&now, 0)) return;

  // First-time init
  if (!initialized) {
    tft.fillScreen(TFT_BLACK);
    paddleX = pongLayout.screenW / 2;
    prevPaddleX = paddleX;
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
    ballStuckToPaddle = false;
    ballStickyUntilMs = 0;
    ballStickyHit = 0.5f;
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
      tft.fillRect(pongLayout.dateClearX, pongLayout.dateY, pongLayout.dateClearW, 16, TFT_BLACK);
      tft.drawString(dateStr, pongLayout.screenW / 2, pongLayout.dateY);
      tft.setTextDatum(TL_DATUM);
      strlcpy(prevDateStr, dateStr, sizeof(prevDateStr));
    }
  }

  // Time (Font 7, on top of everything)
  drawTime();
}
