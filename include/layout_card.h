#ifndef LAYOUT_CARD_H
#define LAYOUT_CARD_H

// "Card" print-screen geometry (HAS_CARD_SKIN). One set today: 320x240
// landscape. 240x240 / portrait sets come later as #if branches here.

#define LY_CARD_W          320
#define LY_CARD_H          240
#define LY_CARD_PAD        12     // outer margin, > GLOW_THICKNESS_PX (8)

// Header
#define LY_CARD_HDR_CY     15     // name + pill centre line
#define LY_CARD_HDR_RULE   30     // rule under the header
#define LY_CARD_PILL_H     15
#define LY_CARD_PILL_PADX  7

// Left AMS column (cardLeft == 0)
#define LY_CARD_AMS_X      LY_CARD_PAD
#define LY_CARD_AMS_W      68
#define LY_CARD_AMS_HDR_Y  38     // "AMS 1" / humidity row (top)
#define LY_CARD_AMS_ROW_Y  51     // first tray row (top)
#define LY_CARD_AMS_ROW_H  22
#define LY_CARD_AMS_RULE_X (LY_CARD_AMS_X + LY_CARD_AMS_W + 6)
#define LY_CARD_HERO_X_AMS (LY_CARD_AMS_RULE_X + 8)

// Hero (right column, or full width)
#define LY_CARD_NAME_Y     44     // job name centre line
#define LY_CARD_REM_LBL_Y  70     // "REMAINING" (top)
#define LY_CARD_BIG_BASE   120    // baseline of the big percent / remaining value
#define LY_CARD_BAR_Y      130
#define LY_CARD_BAR_H      6
#define LY_CARD_LINE_CY    152    // layer / ETA line centre

// Bottom band
#define LY_CARD_BOT_RULE   174
#define LY_CARD_BOT_LBL_Y  182    // cell labels (top)
#define LY_CARD_BOT_BASE   220    // value baseline
#define LY_CARD_TEMP_MAX   6

// Finished / idle hero
#define LY_CARD_FIN_HEAD_CY  52
#define LY_CARD_FIN_LBL_Y    76
#define LY_CARD_FIN_NAME_CY  96
#define LY_CARD_FIN_ROW_Y    116  // DONE / FILAMENT labels (top)
#define LY_CARD_FIN_ROW_CY   140
#define LY_CARD_IDLE_HEAD_CY 58
#define LY_CARD_IDLE_SUB_CY  84
#define LY_CARD_IDLE_AMS_Y   112  // AMS label (top); swatches below
#define LY_CARD_IDLE_SW      18   // swatch size

#endif // LAYOUT_CARD_H
