#ifndef LAYOUT_480x272_H
#define LAYOUT_480x272_H

// Layout profile: 480x272 landscape-native (Guition JC4827W543, NV3041A QSPI).
//
// Modelled on the "portrait" macro set of layout_480x480.h (the smallest set
// the renderer needs: no AMS strip, no split screen, no landscape variant) but
// laid out for a wide, short panel. The NV3041A only supports 0/180 degree
// rotation, so this layout has NO landscape flag - the base LY_* values ARE
// the landscape geometry and rotations 1/3 are folded to 0/2 by display_ui.cpp.
//
// Vertical budget (272 px):
//   0..7     LED progress bar
//   7..27    header (compact, like CYD landscape)
//   28..    gauge row 1  (r=40: arc 30..110, label band to ~122)
//   ..      gauge row 2  (arc 126..206, label band to ~218)
//   220..244 ETA zone
//   246..272 bottom status bar

// --- Screen dimensions ---
#define LY_W    480
#define LY_H    272

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   476
#define LY_BAR_H   7

// --- Header bar ---
#define LY_HDR_Y        7
#define LY_HDR_H        20
#define LY_HDR_NAME_X   8
#define LY_HDR_CY       17
#define LY_HDR_BADGE_RX 10
#define LY_HDR_DOT_CY   10

// --- Printing: 2x3 gauge grid ---
// 480 px wide split in three 160 px columns. r=40 (not 48 like 320x480) so two
// rows + labels + ETA + bottom bar fit in 272 px.
#define LY_GAUGE_R   40
#define LY_GAUGE_T   9
#define LY_TEMP_GAUGE_T 9
#define LY_GAUGE_VALUE_FONT FONT_LARGE   // Inter 19pt (FONT_XLARGE is 320x480-only)
#define LY_GAUGE_VALUE_NUDGE_Y (-2)
#define LY_COL1      80
#define LY_COL2      240
#define LY_COL3      400
#define LY_ROW1      70           // arc spans y=30..110
#define LY_ROW2      166          // arc spans y=126..206

// --- AMS strip ("AMS view" toggle: replaces gauge row 2, like 240x240) ---
// Band covers the row-2 arc + label area (y=124..218) so the toggle wipe in
// display_ui.cpp clears exactly the gauges it replaces.
#define LY_AMS_Y                124
#define LY_AMS_H                94
#define LY_AMS_BAR_H            52
#define LY_AMS_BAR_GAP          3
#define LY_AMS_GROUP_GAP        14
#define LY_AMS_LABEL_OFFY       4
#define LY_AMS_MARGIN           12
#define LY_AMS_BAR_MAX_W        44
#define LY_AMS_BAR_MAX_W_EXTRAS 40

// --- Battery indicator geometry ---
// The JC4827W543 has a battery connector but no sense divider is wired up in
// firmware yet; shouldShowBatteryIndicator() stays false. Kept so the shared
// helpers compile.
#define LY_BAT_W       12
#define LY_BAT_H       24
#define LY_BAT_TEXT_X  18
#define LY_BAT_SHIFT_X 20

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        220
#define LY_ETA_H        24
#define LY_ETA_TEXT_Y   232

// --- Printing: bottom status bar ---
#define LY_BOT_Y        246
#define LY_BOT_H        26
#define LY_BOT_CY       259

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X       6
#define LY_WIFI_Y       259

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      42
#define LY_IDLE_STATE_Y     68
#define LY_IDLE_STATE_H     26
#define LY_IDLE_STATE_TY    81
#define LY_IDLE_DOT_Y       110
#define LY_IDLE_GAUGE_R     44
#define LY_IDLE_GAUGE_Y     185
#define LY_IDLE_G_OFFSET    110

// --- Idle screen (no printer) ---
#define LY_IDLE_NP_TITLE_Y  40
#define LY_IDLE_NP_WIFI_Y   85
#define LY_IDLE_NP_DOT_Y    112
#define LY_IDLE_NP_MSG_Y    150
#define LY_IDLE_NP_OPEN_Y   185
#define LY_IDLE_NP_IP_Y     222

// --- Finished screen ---
#define LY_FIN_GAUGE_R   44
#define LY_FIN_GL        170
#define LY_FIN_GR        310
#define LY_FIN_GY        100
#define LY_FIN_TEXT_Y    172
#define LY_FIN_FILE_Y    202
#define LY_FIN_KWH_Y     224
#define LY_FIN_BOT_Y     246
#define LY_FIN_BOT_H     26
#define LY_FIN_WIFI_Y    259

// --- AP mode screen ---
#define LY_AP_TITLE_Y     34
#define LY_AP_SSID_LBL_Y  76
#define LY_AP_SSID_Y      104
#define LY_AP_PASS_LBL_Y  140
#define LY_AP_PASS_Y      166
#define LY_AP_OPEN_Y      206
#define LY_AP_IP_Y        238

// --- Simple clock (centred in 272 px) ---
#define LY_CLK_CLEAR_Y   50
#define LY_CLK_CLEAR_H   180
#define LY_CLK_TIME_Y    120
#define LY_CLK_AMPM_Y    165
#define LY_CLK_DATE_Y    200

// --- Pong/Breakout clock ---
#define LY_ARK_BRICK_ROWS   4
#define LY_ARK_COLS         12
#define LY_ARK_BRICK_W      36
#define LY_ARK_BRICK_H      10
#define LY_ARK_BRICK_GAP    3
#define LY_ARK_START_X      6
#define LY_ARK_START_Y      30
#define LY_ARK_PADDLE_Y     258
#define LY_ARK_PADDLE_W     60
#define LY_ARK_TIME_Y       150
#define LY_ARK_DATE_Y       12
#define LY_ARK_DIGIT_W      40
#define LY_ARK_DIGIT_H      60
#define LY_ARK_COLON_W      16
#define LY_ARK_DATE_CLR_X   140
#define LY_ARK_DATE_CLR_W   200

#endif // LAYOUT_480x272_H
