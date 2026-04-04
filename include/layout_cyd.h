#ifndef LAYOUT_CYD_H
#define LAYOUT_CYD_H

// Layout profile: ILI9341 240x320 portrait (ESP32-2432S028 "CYD")
// MVP: same layout as 240x240 but bottom-anchored elements moved to 320px.
// The extra ~80px between gauge rows and ETA is intentionally unused for now.

// --- Screen dimensions ---
#define LY_W    240
#define LY_H    320

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   236
#define LY_BAR_H   5

// --- Header bar (same as default) ---
#define LY_HDR_Y        7
#define LY_HDR_H        20
#define LY_HDR_NAME_X   6
#define LY_HDR_CY       17
#define LY_HDR_BADGE_RX 8
#define LY_HDR_DOT_CY   10

// --- Printing: 2x3 gauge grid (same as default) ---
#define LY_GAUGE_R   32
#define LY_GAUGE_T   6
#define LY_COL1      42
#define LY_COL2      120
#define LY_COL3      198
#define LY_ROW1      60
#define LY_ROW2      148

// --- AMS tray visualization zone (CYD portrait, between gauges and ETA) ---
// Gauge row 2 labels extend to ~y=187, so AMS starts at 190 to avoid overlap.
#define LY_AMS_Y          190   // top of AMS zone (below gauge row 2 labels)
#define LY_AMS_H          56    // total height (190+56=246, 4px gap before ETA at 250)
#define LY_AMS_BAR_H      32    // color bar height
#define LY_AMS_BAR_GAP    2     // gap between bars within one AMS
#define LY_AMS_GROUP_GAP  8     // gap between AMS unit groups
#define LY_AMS_LABEL_OFFY 4     // label offset below bars
#define LY_AMS_MARGIN     8     // left/right margin
#define LY_AMS_BAR_MAX_W  30    // max bar width (cap for 1 AMS)

// --- CYD landscape mode (rotation 1 or 3 = 320x240 actual) ---
// Left 240px: gauge area (same grid positions as portrait/default 240x240)
// Right 80px: AMS vertical strip
// ETA + bottom bar use 240x240-style Y to fit within 240px height.
#define LY_LAND_GAUGE_W     240   // gauge area width (left side)
#define LY_LAND_ETA_Y       190   // ETA zone Y (same as default 240x240)
#define LY_LAND_ETA_H       30
#define LY_LAND_ETA_TEXT_Y  207
#define LY_LAND_BOT_Y       222   // bottom status bar Y
#define LY_LAND_BOT_H       18
#define LY_LAND_BOT_CY      232
#define LY_LAND_WIFI_Y      232
// AMS vertical strip (right side)
#define LY_LAND_AMS_X       244   // left edge of AMS column (4px gap from gauges)
#define LY_LAND_AMS_W       72    // usable width
#define LY_LAND_AMS_TOP     28    // below header
#define LY_LAND_AMS_BOT     236   // near bottom edge

// --- Printing: ETA / info zone (moved down for 320px) ---
#define LY_ETA_Y        260
#define LY_ETA_H        30
#define LY_ETA_TEXT_Y   277

// --- Printing: bottom status bar (anchored to bottom) ---
#define LY_BOT_Y    298
#define LY_BOT_H    18
#define LY_BOT_CY   308

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X    4
#define LY_WIFI_Y    308

// --- Idle screen (with printer) - same as default ---
#define LY_IDLE_NAME_Y      30
#define LY_IDLE_STATE_Y     50
#define LY_IDLE_STATE_H     20
#define LY_IDLE_STATE_TY    60
#define LY_IDLE_DOT_Y       85
#define LY_IDLE_GAUGE_R     30
#define LY_IDLE_GAUGE_Y     140
#define LY_IDLE_G_OFFSET    55

// --- Idle screen (no printer) - same as default ---
#define LY_IDLE_NP_TITLE_Y  40
#define LY_IDLE_NP_WIFI_Y   80
#define LY_IDLE_NP_DOT_Y    105
#define LY_IDLE_NP_MSG_Y    140
#define LY_IDLE_NP_OPEN_Y   165
#define LY_IDLE_NP_IP_Y     200

// --- Finished screen (portrait) ---
#define LY_FIN_GAUGE_R   32
#define LY_FIN_GL        72
#define LY_FIN_GR        168
#define LY_FIN_GY        80
#define LY_FIN_TEXT_Y    148
#define LY_FIN_FILE_Y   178
#define LY_FIN_BOT_Y    290
#define LY_FIN_BOT_H    22
#define LY_FIN_WIFI_Y   308
// --- Finished screen (landscape overrides - fit within 240px height) ---
#define LY_LAND_FIN_BOT_Y    216
#define LY_LAND_FIN_BOT_H    20
#define LY_LAND_FIN_WIFI_Y   228

// --- AP mode screen (same as default) ---
#define LY_AP_TITLE_Y     40
#define LY_AP_SSID_LBL_Y  80
#define LY_AP_SSID_Y      110
#define LY_AP_PASS_LBL_Y  140
#define LY_AP_PASS_Y       158
#define LY_AP_OPEN_Y      185
#define LY_AP_IP_Y        210

// --- Simple clock (centered in 320px height) ---
#define LY_CLK_CLEAR_Y   70
#define LY_CLK_CLEAR_H   200
#define LY_CLK_TIME_Y    140
#define LY_CLK_AMPM_Y    175
#define LY_CLK_DATE_Y    205

// --- Pong/Breakout clock ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS          10
#define LY_ARK_BRICK_W      22
#define LY_ARK_BRICK_H      8
#define LY_ARK_BRICK_GAP    2
#define LY_ARK_START_X      3
#define LY_ARK_START_Y      28
#define LY_ARK_PADDLE_Y     304
#define LY_ARK_PADDLE_W     30
#define LY_ARK_TIME_Y       150
#define LY_ARK_DATE_Y       8
#define LY_ARK_DIGIT_W      32
#define LY_ARK_DIGIT_H      48
#define LY_ARK_COLON_W      12
#define LY_ARK_DATE_CLR_X   40
#define LY_ARK_DATE_CLR_W   160

#endif // LAYOUT_CYD_H
