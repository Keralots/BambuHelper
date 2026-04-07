#ifndef LAYOUT_1_8_H
#define LAYOUT_1_8_H

// Layout profile: ST7735 128x160 (1.8" KMRTM2018)
// Scaled from 240x240 layout

// --- Screen dimensions ---
#define LY_W    128
#define LY_H    160

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   100  // smaller for 128px width
#define LY_BAR_H   3    // scaled from 5

// --- Header bar ---
#define LY_HDR_Y        4   // scaled from 7
#define LY_HDR_H        24  // larger for two-line status and full font height
#define LY_HDR_NAME_X   3   // scaled from 6
#define LY_HDR_CY       14  // header line baseline
#define LY_HDR_STATE_Y  22  // second line for running status
#define LY_HDR_BADGE_RX 4   // scaled from 8
#define LY_HDR_DOT_CY   8   // scaled from 10

// --- Printing: 2x3 gauge grid ---
#define LY_GAUGE_R   10  // tighter radius for 128px width
#define LY_GAUGE_T   4   // scaled from 6
#define LY_COL1      24  // left column
#define LY_COL2      64  // middle column
#define LY_COL3      104 // right column
#define LY_ROW1      40  // more vertical spacing
#define LY_ROW2      82  // second row lowered for better breathing room

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        100  // moved down below gauges
#define LY_ETA_H        24  // taller for centered text and bar
#define LY_ETA_TEXT_Y   102 // text anchor
#define LY_ETA_BAR_Y    116
#define LY_ETA_BAR_W    80
#define LY_ETA_BAR_H    4

// --- Printing: bottom status bar ---
#define LY_BOT_Y    136 // lower to give ETA breathing room
#define LY_BOT_H    16  // taller bottom bar
#define LY_BOT_CY   152 // bottom text center

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X    3   // scaled from 4
#define LY_WIFI_Y    150 // moved slightly up

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      20  // scaled from 30
#define LY_IDLE_STATE_Y     33  // scaled from 50
#define LY_IDLE_STATE_H     13  // scaled from 20
#define LY_IDLE_STATE_TY    40  // scaled from 60
#define LY_IDLE_DOT_Y       50  // scaled

// --- Idle gauges ---
#define LY_IDLE_G_OFFSET    24  // scaled
#define LY_IDLE_GAUGE_Y     90  // scaled
#define LY_IDLE_GAUGE_R     16  // scaled

// --- Idle no printer ---
#define LY_IDLE_NP_TITLE_Y  20
#define LY_IDLE_NP_WIFI_Y   40
#define LY_IDLE_NP_DOT_Y    50
#define LY_IDLE_NP_MSG_Y    70
#define LY_IDLE_NP_OPEN_Y   90
#define LY_IDLE_NP_IP_Y     110

// --- Access Point screen ---
#define LY_AP_TITLE_Y       18
#define LY_AP_SSID_LBL_Y    30
#define LY_AP_SSID_Y        44
#define LY_AP_PASS_LBL_Y    64
#define LY_AP_PASS_Y        76
#define LY_AP_OPEN_Y        96
#define LY_AP_IP_Y         108

// --- Finished screen ---
#define LY_FIN_BOT_Y        140
#define LY_FIN_BOT_H        20
#define LY_FIN_WIFI_Y       150
#define LY_FIN_GAUGE_R      18
#define LY_FIN_GL           30
#define LY_FIN_GR           98
#define LY_FIN_GY           74
#define LY_FIN_TEXT_Y       36
#define LY_FIN_FILE_Y       52

// --- Simple clock ---
#define LY_CLK_CLEAR_Y   30
#define LY_CLK_CLEAR_H   100
#define LY_CLK_TIME_Y    80
#define LY_CLK_AMPM_Y    95
#define LY_CLK_DATE_Y    115

// --- AMS display ---
#define LY_AMS_Y        100
#define LY_AMS_H        40
#define LY_AMS_MARGIN   5
#define LY_AMS_GROUP_GAP 5
#define LY_AMS_BAR_GAP  2
#define LY_AMS_BAR_MAX_W 20
#define LY_AMS_BAR_H    20
#define LY_AMS_LABEL_OFFY 10

// --- Landscape mode (for compatibility) ---
#define LY_LAND_ETA_Y        100
#define LY_LAND_ETA_H        30
#define LY_LAND_ETA_TEXT_Y   115
#define LY_LAND_BOT_Y        140
#define LY_LAND_BOT_H        20
#define LY_LAND_BOT_CY       150
#define LY_LAND_FIN_BOT_Y    140
#define LY_LAND_FIN_BOT_H    20
#define LY_LAND_FIN_WIFI_Y   150
#define LY_LAND_AMS_X        10
#define LY_LAND_AMS_TOP      50
#define LY_LAND_AMS_W        100
#define LY_LAND_AMS_BOT      120

// --- Pong clock (arkanoid-style) ---
#define LY_ARK_BRICK_ROWS  5
#define LY_ARK_COLS        8
#define LY_ARK_BRICK_W     10
#define LY_ARK_BRICK_GAP   2
#define LY_ARK_START_X     5
#define LY_ARK_BRICK_H     6
#define LY_ARK_START_Y     20
#define LY_ARK_PADDLE_Y    147
#define LY_ARK_COLON_W     6
#define LY_ARK_DIGIT_W     16
#define LY_ARK_TIME_Y      75
#define LY_ARK_DIGIT_H     26
#define LY_ARK_DATE_Y      100
#define LY_ARK_PADDLE_W    32
#define LY_ARK_DATE_CLR_X  0
#define LY_ARK_DATE_CLR_W  128

#endif // LAYOUT_1_8_H