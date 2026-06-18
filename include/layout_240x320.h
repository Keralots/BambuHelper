#ifndef LAYOUT_240x320_H
#define LAYOUT_240x320_H

// Layout profile: 240x320 portrait (CYD ILI9341, Waveshare ST7789)
// Same gauge grid as 240x240 but bottom-anchored elements moved to 320px.

// --- Feature flags ---
// Permanent AMS strip below the gauges (replaces the 240x240 row-2 toggle).
#define LAYOUT_HAS_AMS_STRIP  1
// CYD rotation 1/3 = 320x240 landscape with AMS sidebar on the right.
#define LAYOUT_HAS_LANDSCAPE  1

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
#define LY_TEMP_GAUGE_T 6        // temp/fan/humidity arc thickness
#define LY_GAUGE_VALUE_FONT FONT_LARGE  // primary value font (Inter 19pt)
#define LY_GAUGE_VALUE_NUDGE_Y 0        // extra Y shift for primary value when
                                        // a secondary line is shown below it
#define LY_COL1      42
#define LY_COL2      120
#define LY_COL3      198
#define LY_ROW1      60
#define LY_ROW2      148

// 3x3 portrait variant (DisplaySettings.portrait9Slots = true).
// Drops the AMS strip in favour of a third gauge row. R shrinks 32->28 so
// row 3 + label fits above ETA (y=260) with ~10px clearance.
#define LY_PORT9_GAUGE_R 28
#define LY_PORT9_ROW1    60
#define LY_PORT9_ROW2    134
#define LY_PORT9_ROW3    208

// --- Split dual-printer screen (top/bottom bands, 3 gauges each) ---
// Two 160px bands; roomier than 240x240 so R grows to 34.
#define LAYOUT_HAS_SPLIT   1
#define LY_SPLIT_SLOTS     3
#define LY_SPLIT_DIV_Y     160
#define LY_SPLIT_GAUGE_R   34
#define LY_SPLIT_GAUGE_T   LY_GAUGE_T
#define LY_SPLIT_BAR_H     5
#define LY_SPLIT_BAR_MARGIN 8
// Band A (top, y 0..159). Gauge row vertically centred in the area below the
// progress bar (R is capped at 34 by the 78px column pitch, so centring is the
// only lever to fill the 160px band - a larger R would collide horizontally).
// progress bar on top, name row beneath it
#define LY_SPLIT_A_BAR_Y   10
#define LY_SPLIT_A_HDR_CY  26
#define LY_SPLIT_A_ROW1    90
// Band B (bottom, y 160..319) = Band A + 160
#define LY_SPLIT_B_BAR_Y   170
#define LY_SPLIT_B_HDR_CY  186
#define LY_SPLIT_B_ROW1    250

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
#define LY_AMS_BAR_MAX_W_EXTRAS 26  // cap for enhanced portrait mode (1 AMS, with filament type labels)

// --- CYD landscape mode (rotation 1 or 3 = 320x240 actual) ---
// Left 240px: gauge area (same grid positions as portrait/default 240x240)
// Right 80px: status badge + AMS vertical strip
// ETA + bottom bar use 240x240-style Y to fit within 240px height.
// Gauge grid positions in landscape match portrait — CYD landscape keeps the
// same 2x3 cluster at x=42/120/198, y=60/148 and just shifts everything else
// (ETA, bot bar) up to fit within 240px height while the right column adds
// the AMS sidebar.
#define LY_LAND_COL1        LY_COL1
#define LY_LAND_COL2        LY_COL2
#define LY_LAND_COL3        LY_COL3
#define LY_LAND_ROW1        LY_ROW1
#define LY_LAND_ROW2        LY_ROW2
// 4-column landscape variant (DisplaySettings.landscape8Slots = true).
// Drops the right-side AMS sidebar and lays out a symmetric 2x4 grid across
// the full 320px width. Same R/rows as the 3-col grid so other zones (ETA,
// bottom bar) keep their landscape Y positions.
#define LY_LAND8_COL1       40
#define LY_LAND8_COL2       120
#define LY_LAND8_COL3       200
#define LY_LAND8_COL4       280
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
#define LY_LAND_AMS_TOP     40    // below header-row badge (badge spans y=7..27)
#define LY_LAND_AMS_BOT_FULL  236 // AMS bottom when bottom-bar limited to 240 (3-4 AMS)
#define LY_LAND_AMS_BOT_SHORT 210 // AMS bottom when bottom-bar spans 320 (0-2 AMS)
// Status badge — sits on the same row as the printer name (header line) but
// in the right column (x=240..320). Header fillRect is capped to 240 when the
// AMS column is active, so the badge survives header repaints.
#define LY_LAND_BADGE_Y     7
#define LY_LAND_BADGE_H     20
#define LY_LAND_BADGE_CY    17

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

// --- Battery indicator (Waveshare boards only) - vertical icon ---
#define LY_BAT_W       8
#define LY_BAT_H       16
#define LY_BAT_TEXT_X  12
#define LY_BAT_SHIFT_X 14

// --- Idle screen (with printer) - same as default ---
#define LY_IDLE_NAME_Y      30
#define LY_IDLE_STATE_Y     50
#define LY_IDLE_STATE_H     20
#define LY_IDLE_STATE_TY    60
#define LY_IDLE_DOT_Y       85
#define LY_IDLE_GAUGE_R     30
#define LY_IDLE_GAUGE_Y     140
#define LY_IDLE_G_OFFSET    55

// --- Idle screen: AMS zone (portrait only, below gauges) ---
#define LY_IDLE_AMS_Y       185
#define LY_IDLE_AMS_H       56
#define LY_IDLE_AMS_BAR_H   32

// --- Idle screen (no printer) - same as default ---
#define LY_IDLE_NP_TITLE_Y  40
#define LY_IDLE_NP_WIFI_Y   80
#define LY_IDLE_NP_DOT_Y    105
#define LY_IDLE_NP_MSG_Y    140
#define LY_IDLE_NP_OPEN_Y   165
#define LY_IDLE_NP_IP_Y     200

// --- Finished screen (portrait, vertically centered) ---
#define LY_FIN_GAUGE_R   32
#define LY_FIN_GL        72
#define LY_FIN_GR        168
#define LY_FIN_GY        100
#define LY_FIN_TEXT_Y    168
#define LY_FIN_FILE_Y   198
#define LY_FIN_KWH_Y    218
#define LY_FIN_AMS_Y    238
#define LY_FIN_AMS_H    45
#define LY_FIN_AMS_BAR_H 26
#define LY_FIN_BOT_Y    290
#define LY_FIN_BOT_H    22
#define LY_FIN_WIFI_Y   308
// --- Finished screen (landscape overrides - fit within 240px height, full 320 wide) ---
// Gauges side-by-side, centered cluster on x=160 (32px gap between R=32 gauges).
// Vertical layout: gauges 18..82, "Print Complete!" centered at 118, file 150,
// kWh 178 (clear band 169..187 — does NOT touch file at 142..158), bot 216..236.
#define LY_LAND_FIN_GL       112   // left gauge center X (left edge x=80)
#define LY_LAND_FIN_GR       208   // right gauge center X (right edge x=240)
#define LY_LAND_FIN_GY        50   // gauge row center Y (R=32 → spans 18..82)
#define LY_LAND_FIN_TEXT_Y   118   // "Print Complete!" (font 4 ~26px tall)
#define LY_LAND_FIN_FILE_Y   150   // file name (font 2 ~16px tall, spans 142..158)
#define LY_LAND_FIN_KWH_Y    178   // kWh row (font 2) — clear band kwhY-9..kwhY+9 = 169..187
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

// --- Landscape stubs for non-printing screens ---
// CYD / ws_lcd_200 keep portrait values in landscape (they fit fine in 240px
// landscape height; this matches today's behaviour before LAYOUT_HAS_LANDSCAPE
// was generalised to drive these screens). JC3248W535 overrides these with
// real landscape values in layout_320x480.h.
#define LY_LAND_IDLE_NAME_Y     LY_IDLE_NAME_Y
#define LY_LAND_IDLE_STATE_Y    LY_IDLE_STATE_Y
#define LY_LAND_IDLE_STATE_H    LY_IDLE_STATE_H
#define LY_LAND_IDLE_STATE_TY   LY_IDLE_STATE_TY
#define LY_LAND_IDLE_DOT_Y      LY_IDLE_DOT_Y
#define LY_LAND_IDLE_GAUGE_R    LY_IDLE_GAUGE_R
#define LY_LAND_IDLE_GAUGE_Y    LY_IDLE_GAUGE_Y
#define LY_LAND_IDLE_G_OFFSET   LY_IDLE_G_OFFSET
#define LY_LAND_IDLE_NP_TITLE_Y LY_IDLE_NP_TITLE_Y
#define LY_LAND_IDLE_NP_WIFI_Y  LY_IDLE_NP_WIFI_Y
#define LY_LAND_IDLE_NP_DOT_Y   LY_IDLE_NP_DOT_Y
#define LY_LAND_IDLE_NP_MSG_Y   LY_IDLE_NP_MSG_Y
#define LY_LAND_IDLE_NP_OPEN_Y  LY_IDLE_NP_OPEN_Y
#define LY_LAND_IDLE_NP_IP_Y    LY_IDLE_NP_IP_Y
#define LY_LAND_AP_TITLE_Y      LY_AP_TITLE_Y
#define LY_LAND_AP_SSID_LBL_Y   LY_AP_SSID_LBL_Y
#define LY_LAND_AP_SSID_Y       LY_AP_SSID_Y
#define LY_LAND_AP_PASS_LBL_Y   LY_AP_PASS_LBL_Y
#define LY_LAND_AP_PASS_Y       LY_AP_PASS_Y
#define LY_LAND_AP_OPEN_Y       LY_AP_OPEN_Y
#define LY_LAND_AP_IP_Y         LY_AP_IP_Y
#define LY_LAND_CLK_CLEAR_Y     LY_CLK_CLEAR_Y
#define LY_LAND_CLK_CLEAR_H     LY_CLK_CLEAR_H
#define LY_LAND_HDR_Y           LY_HDR_Y
#define LY_LAND_HDR_H           LY_HDR_H
#define LY_LAND_HDR_CY          LY_HDR_CY
#define LY_LAND_HDR_DOT_CY      LY_HDR_DOT_CY

// --- Pong/Breakout clock (portrait, 240x320) ---
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

// --- Pong/Breakout clock (landscape, 320x240 - CYD / ws_lcd_200 rotation 1/3) ---
// Preserves the values previously hard-coded in clock_pong.cpp so CYD/ws_lcd_200
// landscape Pong stays pixel-identical after the gate is generalised.
#define LY_LAND_ARK_BRICK_COLS    13
#define LY_LAND_ARK_BRICK_START_Y 28
#define LY_LAND_ARK_PADDLE_Y      224
#define LY_LAND_ARK_PADDLE_W      30
#define LY_LAND_ARK_TIME_Y        130
#define LY_LAND_ARK_DATE_Y        8

#endif // LAYOUT_240x320_H
