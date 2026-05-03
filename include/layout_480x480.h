#ifndef LAYOUT_480x480_H
#define LAYOUT_480x480_H

// Layout profile: SenseCAP Indicator ST7701S 480x480
// Scaled 2x from the 240x240 default layout for crisp rendering on
// the high-resolution display.

// --- Screen dimensions ---
#define LY_W    480
#define LY_H    480

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   472
#define LY_BAR_H   10

// --- Header bar ---
#define LY_HDR_Y        14
#define LY_HDR_H        40
#define LY_HDR_NAME_X   12
#define LY_HDR_CY       34
#define LY_HDR_BADGE_RX 16
#define LY_HDR_DOT_CY   20

// --- Printing: 2x3 gauge grid (2x of 240x240) ---
#define LY_GAUGE_R   64
#define LY_GAUGE_T   12
#define LY_COL1      84
#define LY_COL2      240
#define LY_COL3      396
#define LY_ROW1      120
#define LY_ROW2      296

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        380
#define LY_ETA_H        60
#define LY_ETA_TEXT_Y   410

// --- Printing: bottom status bar ---
#define LY_BOT_Y    444
#define LY_BOT_H    36
#define LY_BOT_CY   462

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X    8
#define LY_WIFI_Y    462

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      60
#define LY_IDLE_STATE_Y      100
#define LY_IDLE_STATE_H      40
#define LY_IDLE_STATE_TY     120
#define LY_IDLE_DOT_Y        170
#define LY_IDLE_GAUGE_R      60
#define LY_IDLE_GAUGE_Y      280
#define LY_IDLE_G_OFFSET      110

// --- Idle screen (no printer) ---
#define LY_IDLE_NP_TITLE_Y  80
#define LY_IDLE_NP_WIFI_Y   160
#define LY_IDLE_NP_DOT_Y    210
#define LY_IDLE_NP_MSG_Y    280
#define LY_IDLE_NP_OPEN_Y   330
#define LY_IDLE_NP_IP_Y    400

// --- Finished screen ---
#define LY_FIN_GAUGE_R   64
#define LY_FIN_GL        144
#define LY_FIN_GR        336
#define LY_FIN_GY        160
#define LY_FIN_TEXT_Y    296
#define LY_FIN_FILE_Y    356
#define LY_FIN_BOT_Y    440
#define LY_FIN_BOT_H    40
#define LY_FIN_WIFI_Y   462

// --- AP mode screen ---
#define LY_AP_TITLE_Y     80
#define LY_AP_SSID_LBL_Y 160
#define LY_AP_SSID_Y     220
#define LY_AP_PASS_LBL_Y 280
#define LY_AP_PASS_Y      316
#define LY_AP_OPEN_Y      370
#define LY_AP_IP_Y        420

// --- Simple clock ---
#define LY_CLK_CLEAR_Y   100
#define LY_CLK_CLEAR_H   280
#define LY_CLK_TIME_Y    200
#define LY_CLK_AMPM_Y    270
#define LY_CLK_DATE_Y    310

// --- Pong/Breakout clock ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS          10
#define LY_ARK_BRICK_W      44
#define LY_ARK_BRICK_H      16
#define LY_ARK_BRICK_GAP    4
#define LY_ARK_START_X      6
#define LY_ARK_START_Y      56
#define LY_ARK_PADDLE_Y     448
#define LY_ARK_PADDLE_W     60
#define LY_ARK_TIME_Y       260
#define LY_ARK_DATE_Y       16
#define LY_ARK_DIGIT_W      64
#define LY_ARK_DIGIT_H      96
#define LY_ARK_COLON_W      24
#define LY_ARK_DATE_CLR_X   80
#define LY_ARK_DATE_CLR_W   320

#endif // LAYOUT_480x480_H