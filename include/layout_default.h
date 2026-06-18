#ifndef LAYOUT_DEFAULT_H
#define LAYOUT_DEFAULT_H

// Layout profile: ST7789 240x240 (ESP32-S3 Super Mini)
// All LY_* values extracted from existing hardcoded coordinates.

// --- Screen dimensions ---
#define LY_W    240
#define LY_H    240

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   236
#define LY_BAR_H   5

// --- Header bar ---
#define LY_HDR_Y        7
#define LY_HDR_H        20
#define LY_HDR_NAME_X   6
#define LY_HDR_CY       17      // vertical center of header text
#define LY_HDR_BADGE_RX 8       // badge right margin from SCREEN_W
#define LY_HDR_DOT_CY   10      // multi-printer indicator dot Y

// --- Printing: 2x3 gauge grid ---
#define LY_GAUGE_R   32      // radius for all gauges
#define LY_GAUGE_T   6       // progress arc thickness
#define LY_TEMP_GAUGE_T 6    // temp/fan/humidity arc thickness
#define LY_GAUGE_VALUE_FONT FONT_LARGE  // primary value font (Inter 19pt)
#define LY_GAUGE_VALUE_NUDGE_Y 0        // extra Y shift for primary value when
                                        // a secondary line is shown below it
#define LY_COL1      42      // left column center X
#define LY_COL2      120     // middle column center X
#define LY_COL3      198     // right column center X
#define LY_ROW1      60      // row 1 center Y
#define LY_ROW2      148     // row 2 center Y

// --- Split dual-printer screen (top/bottom bands, 3 gauges each) ---
// Two 120px bands. Each band: header line (name + state dot), thin progress
// bar, then a single row of 3 gauges reusing the gaugeSlots[0..2] config.
#define LAYOUT_HAS_SPLIT   1
#define LY_SPLIT_SLOTS     3      // gauges drawn per band (gaugeSlots[0..2])
#define LY_SPLIT_DIV_Y     120    // divider line Y (band boundary)
#define LY_SPLIT_GAUGE_R   30
#define LY_SPLIT_GAUGE_T   LY_GAUGE_T
#define LY_SPLIT_BAR_H     4
#define LY_SPLIT_BAR_MARGIN 8
// Band A (top, y 0..119): progress bar on top, name row beneath it
#define LY_SPLIT_A_BAR_Y   6
#define LY_SPLIT_A_HDR_CY  20
#define LY_SPLIT_A_ROW1    60
// Band B (bottom, y 120..239)
#define LY_SPLIT_B_BAR_Y   126
#define LY_SPLIT_B_HDR_CY  140
#define LY_SPLIT_B_ROW1    180

// --- AMS tray visualization (replaces gauge row 2 when amsView enabled) ---
// Row 1 gauges (R=32 at Y=60) end at y=92 with labels to ~95.
// ETA starts at Y=190. Available band: ~98 px.
#define LY_AMS_Y                110
#define LY_AMS_H                70
#define LY_AMS_BAR_H            40
#define LY_AMS_BAR_GAP          2
#define LY_AMS_GROUP_GAP        8
#define LY_AMS_LABEL_OFFY       4
#define LY_AMS_MARGIN           8
#define LY_AMS_BAR_MAX_W        30
#define LY_AMS_BAR_MAX_W_EXTRAS 26

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        190
#define LY_ETA_H        30
#define LY_ETA_TEXT_Y   207

// --- Printing: bottom status bar ---
#define LY_BOT_Y    222
#define LY_BOT_H    18
#define LY_BOT_CY   232     // vertical center of bottom bar items

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X    4
#define LY_WIFI_Y    232

// --- Battery indicator (Waveshare boards only) - vertical icon ---
#define LY_BAT_W       8     // body width (8 px footprint)
#define LY_BAT_H       16    // total height incl. 2 px nub on top
#define LY_BAT_TEXT_X  12    // X for "%" text after icon (relative to LY_WIFI_X)
#define LY_BAT_SHIFT_X 14    // shift swatch+filament right when battery icon present

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      30
#define LY_IDLE_STATE_Y     50      // fillRect Y for state badge clear
#define LY_IDLE_STATE_H     20
#define LY_IDLE_STATE_TY    60      // state text Y
#define LY_IDLE_DOT_Y       85
#define LY_IDLE_GAUGE_R     30
#define LY_IDLE_GAUGE_Y     140
#define LY_IDLE_G_OFFSET    55      // gauge X offset from center

// --- Idle screen (no printer) ---
#define LY_IDLE_NP_TITLE_Y  40
#define LY_IDLE_NP_WIFI_Y   80
#define LY_IDLE_NP_DOT_Y    105
#define LY_IDLE_NP_MSG_Y    140
#define LY_IDLE_NP_OPEN_Y   165
#define LY_IDLE_NP_IP_Y     200

// --- Finished screen ---
#define LY_FIN_GAUGE_R   32
#define LY_FIN_GL        72       // left gauge center X
#define LY_FIN_GR        168      // right gauge center X
#define LY_FIN_GY        80       // gauges center Y
#define LY_FIN_TEXT_Y    148      // "Print Complete!" Y
#define LY_FIN_FILE_Y   178      // filename Y
#define LY_FIN_BOT_Y    218      // bottom bar Y
#define LY_FIN_BOT_H    22
#define LY_FIN_WIFI_Y   230      // WiFi signal Y

// --- AP mode screen ---
#define LY_AP_TITLE_Y     40
#define LY_AP_SSID_LBL_Y  80
#define LY_AP_SSID_Y      110
#define LY_AP_PASS_LBL_Y  140
#define LY_AP_PASS_Y       158
#define LY_AP_OPEN_Y      185
#define LY_AP_IP_Y        210

// --- Simple clock ---
#define LY_CLK_CLEAR_Y   50
#define LY_CLK_CLEAR_H   140
#define LY_CLK_TIME_Y    100
#define LY_CLK_AMPM_Y    135
#define LY_CLK_DATE_Y    155

// --- Pong/Breakout clock ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS          10
#define LY_ARK_BRICK_W      22
#define LY_ARK_BRICK_H      8
#define LY_ARK_BRICK_GAP    2
#define LY_ARK_START_X      3
#define LY_ARK_START_Y      28
#define LY_ARK_PADDLE_Y     224
#define LY_ARK_PADDLE_W     30
#define LY_ARK_TIME_Y       130
#define LY_ARK_DATE_Y       8
#define LY_ARK_DIGIT_W      32
#define LY_ARK_DIGIT_H      48
#define LY_ARK_COLON_W      12
#define LY_ARK_DATE_CLR_X   40
#define LY_ARK_DATE_CLR_W   160

#endif // LAYOUT_DEFAULT_H
