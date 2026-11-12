#ifndef LAYOUT_ROUND466_H
#define LAYOUT_ROUND466_H

// Layout profile: CO5300 1.75" round AMOLED 466x466 (DISPLAY_ROUND_466,
// ws_amoled_175). Visible area is the inscribed circle: center (233,233),
// r = 233. The GRAM is a full square, so drawing outside the circle is
// harmless (the bezel hides it) - but nothing meaningful may live there.
//
// This is layout_round480.h scaled by 466/480 (every pixel value rounded to the
// nearest integer; angles, ARK row/col counts and the storm shift unchanged),
// with the same 2x VLW font tier. Text therefore sits ~3% larger relative to
// the panel than on the 480 board. Clearances the 480 header calls out,
// re-checked at this size (FONT_BODY_2X is 39 px tall):
//   rim arc text band  190 + 20 = 210 < ring inner 229 - 14 = 215
//   Speedo status band 159 + 20 + 1 = 180 < big-arc inner AA edge 184
//   Speedo % digits    202 +/- 47 = 155..249, dots row at 140
//   Finished headline  chord at dy 70 inside r 215 = 406 > MAXW 369

// --- Screen dimensions ---
#define LY_W    466
#define LY_H    466

// Shared UI scale + text tier (see layout.h). Geometry is 466/240 of the 240
// profile; text keeps the 2x VLW tier, about 3% larger relative to the panel.
#define LY_SC(v)  ((v) * 466 / 240)

#define LY_F_SMALL  FONT_SMALL_2X
#define LY_F_BODY  FONT_BODY_2X
#define LY_F_LARGE  FONT_LARGE_2X

#define LY_GLOW_STORM_SEG_SHIFT  4   // 16 px of rim per bolt segment

// Icons block-scale by an integer factor; LY_SC(16) = 31 would round to 1x.
#define LY_ICON16  32

// =============================================================================
//  Round-specific constants (LY_RND_*)
// =============================================================================

// --- Rim progress ring (printing + finished screens) ---
#define LY_RND_RING_R  229
#define LY_RND_RING_T  14

// --- Curved rim text (status line on top, ETA line on bottom) ---
#define LY_RND_ARC_R  190
#define LY_RND_ARC_STATUS_HDEG  50
#define LY_RND_ARC_ETA_HDEG  45
#define LY_RND_ARC_STATUS_MAXW  311

// --- Printing screen (variant A: rim ring + 3 mini gauges) ---
#define LY_RND_DOTS_Y  82
#define LY_RND_PCT_Y  155
#define LY_RND_PRINT_PCT_Y  136
#define LY_RND_FIL_CLR_CAA  108
#define LY_RND_FIL_CLR_HDEG  23
#define LY_RND_FIL_TXT_CAA  111
#define LY_RND_FIL_DOT_AA  87
#define LY_RND_FIL_TXT_MAXW  124
#define LY_RND_RSTAT_CLR_CAA  254
#define LY_RND_RSTAT_CLR_HDEG  19
#define LY_RND_RSTAT_TXT_CAA  250
#define LY_RND_RSTAT_DOT_AA  269
#define LY_RND_RSTAT_TXT_MAXW  93
#define LY_RND_LAYER_Y  202
#define LY_RND_G_R  52
#define LY_RND_G_T  12
#define LY_RND_G_Y  280
#define LY_RND_G_X1  113
#define LY_RND_G_X2  233
#define LY_RND_G_X3  353

#define LY_RND_7SEG_SM_SCALE  1.55f
#define LY_RND_7SEG_SM_HALFH  37
#define LY_RND_7SEG_BG_SCALE  1.94f
#define LY_RND_7SEG_BG_HALFH  47

// --- Printing screen, "Speedo" skin (roundSkin = 1) ---
#define LY_RND_SPD_R  208
#define LY_RND_SPD_T  23
#define LY_RND_SPD_STATUS_Y  124
#define LY_RND_SPD_STATUS_R  159
#define LY_RND_SPD_STATUS_HDEG  52
#define LY_RND_SPD_DOTS_Y  140
#define LY_RND_SPD_PCT_Y  202
#define LY_RND_SPD_LAYER_Y  291
#define LY_RND_SPD_FIL_Y  330
#define LY_RND_SPD_TICK_RI  214
#define LY_RND_SPD_TICK_RO  223
#define LY_RND_SPD_TEMP_Y  369
#define LY_RND_SPD_NOZ_X  165
#define LY_RND_SPD_BED_X  301
#define LY_RND_SPD_ETA_R  202
#define LY_RND_SPD_ETA_HDEG  36

// --- Printing screen, "Rings" skin (roundSkin = 2) ---
#define LY_RND_RGS_R1  225
#define LY_RND_RGS_R2  194
#define LY_RND_RGS_R3  163
#define LY_RND_RGS_T  19
#define LY_RND_RGS_PCT_Y  183
#define LY_RND_RGS_TEMP_Y  245
#define LY_RND_RGS_NOZ_X  167
#define LY_RND_RGS_BED_X  299
#define LY_RND_RGS_ETA_Y  295
#define LY_RND_RGS_FIL_Y  334
#define LY_RND_RGS_DOTS_Y  361

// --- Idle screen (printer online) ---
#define LY_RND_IDLE_NAME_R  198
#define LY_RND_IDLE_G_Y  295
#define LY_RND_IDLE_G_R  58
#define LY_RND_IDLE_G_OFF  85
#define LY_RND_IDLE_WIFI_Y  408
#define LY_RND_IDLE_VER_Y  435

// --- Clock face ---
#define LY_RND_CLK_TICK_RO  229
#define LY_RND_CLK_TICK_RI  217
#define LY_RND_CLK_TICK_RIM  210
#define LY_RND_CLK_DOT_Y  58
#define LY_RND_CLK_TIME_Y  233
#define LY_RND_CLK_DATE_Y  315
#define LY_RND_CLK_INFO_Y  381

// --- Finished screen ---
#define LY_RND_FIN_CHK_Y  179
#define LY_RND_FIN_CHK_R  62
#define LY_RND_FIN_TEXT_Y  303
#define LY_RND_FIN_TEXT_MAXW  369
#define LY_RND_FIN_FILE_Y  346
#define LY_RND_FIN_TIME_Y  384

// =============================================================================
//  Legacy LY_* constants (shared code paths; pulled into the circle)
// =============================================================================

// --- LED progress bar: unused on round (rim ring replaces it); keep it a
//     short centered stub so any fallthrough draw stays visible ---
#define LY_BAR_W  78
#define LY_BAR_H  6

// --- Header bar: no header on round screens; centered fallbacks ---
#define LY_HDR_Y  58
#define LY_HDR_H  39
#define LY_HDR_NAME_X  136
#define LY_HDR_CY  78
#define LY_HDR_BADGE_RX  136
#define LY_HDR_DOT_CY  66

// --- Gauge grid fallbacks (safe centers inside the circle) ---
#define LY_GAUGE_R  58
#define LY_GAUGE_T  12
#define LY_TEMP_GAUGE_T  12
#define LY_GAUGE_VALUE_FONT FONT_LARGE_2X
#define LY_GAUGE_VALUE_NUDGE_Y  0
#define LY_COL1  136
#define LY_COL2  233
#define LY_COL3  330
#define LY_ROW1  155
#define LY_ROW2  311

// --- AMS strip: not shown on round (no room inside the circle) ---
#define LY_AMS_Y  214
#define LY_AMS_H  116
#define LY_AMS_BAR_H  70
#define LY_AMS_BAR_GAP  4
#define LY_AMS_GROUP_GAP  16
#define LY_AMS_LABEL_OFFY  8
#define LY_AMS_MARGIN  58
#define LY_AMS_BAR_MAX_W  50
#define LY_AMS_BAR_MAX_W_EXTRAS  43

// --- ETA / info zone ---
#define LY_ETA_Y  381
#define LY_ETA_H  47
#define LY_ETA_TEXT_Y  404

// --- Bottom status bar ---
#define LY_BOT_Y  416
#define LY_BOT_H  31
#define LY_BOT_CY  431

// --- WiFi signal indicator: corner position is invisible on round; round
//     screens draw it near the center column (icon + dBm text extend right
//     from LY_WIFI_X, so 175 puts the block roughly centered) ---
#define LY_WIFI_X  175
#define LY_WIFI_Y  400

// --- Battery indicator (AXP2101 battery gauge) ---
#define LY_BAT_W  16
#define LY_BAT_H  31
#define LY_BAT_TEXT_X  23
#define LY_BAT_SHIFT_X  27

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y  105
#define LY_IDLE_STATE_Y  140
#define LY_IDLE_STATE_H  39
#define LY_IDLE_STATE_TY  159
#define LY_IDLE_DOT_Y  202
#define LY_IDLE_GAUGE_R  58
#define LY_IDLE_GAUGE_Y  295
#define LY_IDLE_G_OFFSET  85

// --- Idle screen (no printer): centered stack, chord-checked ---
#define LY_IDLE_NP_TITLE_Y  109
#define LY_IDLE_NP_WIFI_Y  171
#define LY_IDLE_NP_DOT_Y  210
#define LY_IDLE_NP_MSG_Y  264
#define LY_IDLE_NP_OPEN_Y  307
#define LY_IDLE_NP_IP_Y  357

// --- Finished screen fallbacks ---
#define LY_FIN_GAUGE_R  54
#define LY_FIN_GL  163
#define LY_FIN_GR  303
#define LY_FIN_GY  179
#define LY_FIN_TEXT_Y  303
#define LY_FIN_FILE_Y  346
#define LY_FIN_BOT_Y  384
#define LY_FIN_BOT_H  39
#define LY_FIN_WIFI_Y  400

// --- AP mode screen: centered stack, chord-checked ---
#define LY_AP_TITLE_Y  101
#define LY_AP_SSID_LBL_Y  167
#define LY_AP_SSID_Y  210
#define LY_AP_PASS_LBL_Y  260
#define LY_AP_PASS_Y  295
#define LY_AP_OPEN_Y  346
#define LY_AP_IP_Y  388

// --- Simple clock (legacy constants; round face uses LY_RND_CLK_*) ---
#define LY_CLK_CLEAR_Y  116
#define LY_CLK_CLEAR_H  233
#define LY_CLK_TIME_Y  194
#define LY_CLK_AMPM_Y  262
#define LY_CLK_DATE_Y  301

// --- Pong/Breakout clock: disabled on round (rectangular walls);
//     constants kept for compilation of the shared translation unit ---
#define LY_ARK_BRICK_ROWS  5
#define LY_ARK_COLS  10
#define LY_ARK_BRICK_W  43
#define LY_ARK_BRICK_H  16
#define LY_ARK_BRICK_GAP  4
#define LY_ARK_START_X  6
#define LY_ARK_START_Y  54
#define LY_ARK_PADDLE_Y  435
#define LY_ARK_PADDLE_W  58
#define LY_ARK_TIME_Y  252
#define LY_ARK_DATE_Y  16
#define LY_ARK_DIGIT_W  62
#define LY_ARK_DIGIT_H  93
#define LY_ARK_COLON_W  23
#define LY_ARK_DATE_CLR_X  78
#define LY_ARK_DATE_CLR_W  311

#endif // LAYOUT_ROUND466_H
