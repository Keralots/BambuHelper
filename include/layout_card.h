#ifndef LAYOUT_CARD_H
#define LAYOUT_CARD_H

#include <stdint.h>

// "Card" print-screen geometry (HAS_CARD_SKIN). One set per orientation of the
// 240x320 panel, picked at render time. Bottom-band rows sit at a fixed offset
// from their rule: labels at rule + 8, value baseline at rule + 46.

struct CardGeo {
  int16_t W, H, pad;
  // header
  int16_t hdrCy, hdrRule, pillH, pillPadX;
  // printing hero
  int16_t nameCy, remLblY, bigBase, barY, barH, lineCy;
  // AMS: a left column (amsColW > 0, landscape) or a strip under the hero
  int16_t amsColW, amsHdrY, amsRowY, amsRowH;
  int16_t stripLblY, stripY, stripH;
  int16_t stripCols, stripRowH;   // 0 = one row of 4 slots
  // bottom band
  int16_t botRuleAms, botRuleNoAms;
  int16_t cellCols;          // 0 = every cell on one row
  int16_t cellRowH;
  // finished
  int16_t finHeadCy, finLblY, finNameCy, finRowY, finRowCy, finDoorCy, finBotRule;
  // idle
  int16_t idleHeadCy, idleSubCy, idleClockBase, idleClockRight;
  int16_t idleAmsY, idleSw, idleHtNewRow, idleBotRule;
  // bottom-band row offsets from the rule (label top, value baseline)
  int16_t cellLblOff, cellBaseOff;
  // plate thumbnail: size, top (beside-hero layouts)
  int16_t thumbSize, thumbY;
  // beside-hero thumbnail moves these (0 = layout has no such variant)
  int16_t tbBarY, tbLineCy, tbRemLblY, tbRemBase, tbRule;
  int16_t tbRemOnLine;       // 1 = REMAINING replaces ETA on the line (no own row)
  int16_t noAms;             // 1 = no room for an AMS column / strip
};

// 320x240 landscape. Outer pad 12 > GLOW_THICKNESS_PX (8).
static const CardGeo CARD_GEO_LAND = {
  320, 240, 12,
  15, 30, 15, 7,
  44, 70, 120, 130, 6, 152,
  68, 38, 51, 22,
  0, 0, 0,
  0, 0,
  174, 174, 0, 42,
  52, 76, 96, 116, 140, 158, 174,
  58, 84, 92, 1, 112, 18, 1, 174,
  8, 46,
  84, 40,
  0, 0, 0, 0, 0, 0,
  0,
};

// 240x320 portrait: hero at the full 216 px width (same as the landscape hero
// beside the AMS column), AMS as a 2x2 slot grid, temperatures on a 3x2 grid.
static const CardGeo CARD_GEO_PORT = {
  240, 320, 12,
  15, 30, 15, 7,
  42, 62, 106, 114, 6, 134,
  0, 0, 0, 0,
  152, 164, 19,
  2, 22,
  212, 162, 3, 48,
  52, 72, 90, 106, 126, 148, 166,
  50, 72, 134, 0, 146, 18, 0, 214,
  8, 46,
  76, 52,
  134, 154, 170, 200, 212, 0,
  0,
};

// 240x240 square: S1 layout. No AMS column/strip; temperatures 3x2 with
// tighter rows; idle clock small beside "Ready" (idleClockRight == 2).
static const CardGeo CARD_GEO_SQ = {
  240, 240, 12,
  15, 30, 15, 7,
  42, 56, 100, 112, 6, 132,
  0, 0, 0, 0,
  0, 0, 0,
  0, 0,
  144, 144, 3, 44,
  48, 64, 82, 96, 116, 132, 144,
  48, 70, 48, 2, 86, 16, 0, 148,
  6, 38,
  56, 48,
  112, 132, 0, 0, 144, 1,
  1,
};

#define LY_CARD_TEMP_MAX   6

#endif // LAYOUT_CARD_H
