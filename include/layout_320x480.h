#ifndef LAYOUT_320x480_H
#define LAYOUT_320x480_H

// Layout profile: 320x480 portrait (Guition JC3248W535, AXS15231B QSPI IPS).
// Redesigned layout that uses the extra screen real estate — does not simply
// stretch the 240x320 layout. Gauges are larger, AMS strip is always visible,
// and the ETA / bottom status areas are generously sized.

// --- Screen dimensions ---
#define LY_W    320
#define LY_H    480

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   316
#define LY_BAR_H   7

// --- Header bar ---
#define LY_HDR_Y        10
#define LY_HDR_H        26
#define LY_HDR_NAME_X   8
#define LY_HDR_CY       23        // vertical center of header text
#define LY_HDR_BADGE_RX 10        // badge right margin from SCREEN_W
#define LY_HDR_DOT_CY   13        // multi-printer indicator dot Y

// --- Printing: 2x3 gauge grid (3 columns, 2 rows) ---
// 320px wide split into 3 columns of ~107px — gauges are r=48, spacing tuned
// so left/right edges sit ~5px from the screen edges.
#define LY_GAUGE_R   48           // radius for all gauges (was 32 on 240x)
#define LY_GAUGE_T   9            // progress arc thickness (was 6 on 240x)
#define LY_COL1      56
#define LY_COL2      160
#define LY_COL3      264
#define LY_ROW1      92           // top row center Y (gauge top edge y=44)
#define LY_ROW2      228          // bottom row center Y (gauge top edge y=180)

// --- AMS tray visualization zone (below gauge grid) ---
// Row 2 gauges bottom edge is at y=276 (228+48). Labels extend ~12px below,
// so AMS starts at y=295 with 4px gap under it (ETA begins at y=355).
#define LY_AMS_Y          295
#define LY_AMS_H          56
#define LY_AMS_BAR_H      32
#define LY_AMS_BAR_GAP    3
#define LY_AMS_GROUP_GAP  10
#define LY_AMS_LABEL_OFFY 4
#define LY_AMS_MARGIN     10
#define LY_AMS_BAR_MAX_W  42

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        360
#define LY_ETA_H        46
#define LY_ETA_TEXT_Y   383

// --- Printing: bottom status bar ---
#define LY_BOT_Y        414
#define LY_BOT_H        26
#define LY_BOT_CY       427

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X       6
#define LY_WIFI_Y       452

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      45
#define LY_IDLE_STATE_Y     75
#define LY_IDLE_STATE_H     28
#define LY_IDLE_STATE_TY    89
#define LY_IDLE_DOT_Y       125
#define LY_IDLE_GAUGE_R     46
#define LY_IDLE_GAUGE_Y     210
#define LY_IDLE_G_OFFSET    80

// --- Idle screen: AMS zone (below gauges) ---
#define LY_IDLE_AMS_Y       275
#define LY_IDLE_AMS_H       80
#define LY_IDLE_AMS_BAR_H   46

// --- Idle screen (no printer) ---
#define LY_IDLE_NP_TITLE_Y  60
#define LY_IDLE_NP_WIFI_Y   120
#define LY_IDLE_NP_DOT_Y    150
#define LY_IDLE_NP_MSG_Y    200
#define LY_IDLE_NP_OPEN_Y   240
#define LY_IDLE_NP_IP_Y     290

// --- Finished screen (portrait, vertically centered) ---
#define LY_FIN_GAUGE_R   48
#define LY_FIN_GL        96
#define LY_FIN_GR        224
#define LY_FIN_GY        150
#define LY_FIN_TEXT_Y    245
#define LY_FIN_FILE_Y    290
#define LY_FIN_KWH_Y     320
#define LY_FIN_AMS_Y     345
#define LY_FIN_AMS_H     65
#define LY_FIN_AMS_BAR_H 38
#define LY_FIN_BOT_Y     436
#define LY_FIN_BOT_H     28
#define LY_FIN_WIFI_Y    458

// --- AP mode screen ---
#define LY_AP_TITLE_Y     60
#define LY_AP_SSID_LBL_Y  120
#define LY_AP_SSID_Y      160
#define LY_AP_PASS_LBL_Y  210
#define LY_AP_PASS_Y      240
#define LY_AP_OPEN_Y      280
#define LY_AP_IP_Y        315

// --- Simple clock (centered in 480px height) ---
#define LY_CLK_CLEAR_Y   110
#define LY_CLK_CLEAR_H   280
#define LY_CLK_TIME_Y    210
#define LY_CLK_AMPM_Y    265
#define LY_CLK_DATE_Y    310

// --- Pong/Breakout clock (scaled for 320x480) ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS         10
#define LY_ARK_BRICK_W      30        // 10 cols * 30 + 9 gaps * 2 = 318 (fits 320)
#define LY_ARK_BRICK_H      12
#define LY_ARK_BRICK_GAP    2
#define LY_ARK_START_X      1
#define LY_ARK_START_Y      40
#define LY_ARK_PADDLE_Y     460
#define LY_ARK_PADDLE_W     44
#define LY_ARK_TIME_Y       220
#define LY_ARK_DATE_Y       10
#define LY_ARK_DIGIT_W      42
#define LY_ARK_DIGIT_H      64
#define LY_ARK_COLON_W      16
#define LY_ARK_DATE_CLR_X   50
#define LY_ARK_DATE_CLR_W   220

#endif // LAYOUT_320x480_H
