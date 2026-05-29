#ifndef LAYOUT_320x480_H
#define LAYOUT_320x480_H

// Layout profile: 320x480 portrait (Guition JC3248W535, AXS15231B QSPI IPS).
// Redesigned layout that uses the extra screen real estate - does not simply
// stretch the 240x320 layout. Gauges are larger, AMS strip is always visible,
// and the ETA / bottom status areas are generously sized.
//
// Landscape (480x320) mirrors the CYD pattern: 360 px gauge area on the left,
// 120 px AMS sidebar on the right. Sprite-backed rendering applies the
// rotation at the sprite level (LovyanGFX setRotation on the 320x480 buffer);
// pushRawPixels then dumps the buffer in native memory order and the panel
// shows the correctly-rotated image.

// --- Feature flags ---
#define LAYOUT_HAS_AMS_STRIP  1
#define LAYOUT_HAS_LANDSCAPE  1

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
#define LY_GAUGE_T   10           // progress arc thickness (grows inward)
#define LY_TEMP_GAUGE_T 10        // temp/fan/humidity arc thickness - matches
                                  // progress arc so the 2x3 grid reads uniform
#define LY_GAUGE_VALUE_FONT FONT_XLARGE  // larger inscribed numbers (Inter 22pt)
#define LY_GAUGE_VALUE_NUDGE_Y (-4)     // bigger font pushes baseline down ~2 px;
                                        // lift primary 4 px to clear the secondary
#define LY_COL1      56
#define LY_COL2      160
#define LY_COL3      264
#define LY_ROW1      92           // top row center Y (gauge top edge y=44)
#define LY_ROW2      228          // bottom row center Y (gauge top edge y=180)

// 3x3 portrait variant (DisplaySettings.portrait9Slots = true).
// Drops the AMS strip in favour of a third gauge row. R shrinks 48->46 to
// keep row 3's label clear of ETA (y=380) with comfortable headroom.
#define LY_PORT9_GAUGE_R 46
#define LY_PORT9_ROW1    86
#define LY_PORT9_ROW2    196
#define LY_PORT9_ROW3    306

// --- AMS tray visualization zone (below gauge grid) ---
// Row 2 gauges bottom edge is at y=276 (228+48). Labels extend ~12px below,
// so AMS starts at y=295. Zone is 72 px tall with 42 px bars; ETA begins at
// y=380 and the bottom status bar is pinned near the screen edge (y=450) so
// the panel's lower ~40 px isn't left as dead space during a print.
#define LY_AMS_Y          295
#define LY_AMS_H          72
#define LY_AMS_BAR_H      42
#define LY_AMS_BAR_GAP    3
#define LY_AMS_GROUP_GAP  10
#define LY_AMS_LABEL_OFFY 4
#define LY_AMS_MARGIN     10
#define LY_AMS_BAR_MAX_W  42
#define LY_AMS_BAR_MAX_W_EXTRAS 42  // JC3248W535 has plenty of width — no need to shrink in extras mode

// --- Battery indicator placeholders ---
// JC3248W535 has no battery hardware exposed to BambuHelper — shouldShowBatteryIndicator()
// returns false at runtime, so these are never actually drawn. Defined only so the
// unconditionally-compiled drawBatteryPrefix/drawWifiSignalIndicator helpers in
// display_ui.cpp compile on this build. Values mirror layout_default.h scaled up.
#define LY_BAT_W       12
#define LY_BAT_H       24
#define LY_BAT_TEXT_X  18
#define LY_BAT_SHIFT_X 20

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        380
#define LY_ETA_H        46
#define LY_ETA_TEXT_Y   403

// --- Printing: file name line (fills the gap between ETA and bottom bar) ---
// FONT_BODY ~16 px tall, drawn with MC_DATUM. Zone clears LY_FILE_Y±LY_FILE_H/2,
// so text spans roughly y=429..447 and leaves 3 px to the bottom bar at y=450.
#define LY_FILE_Y       436
#define LY_FILE_H       18

// --- Printing: bottom status bar ---
#define LY_BOT_Y        450
#define LY_BOT_H        26
#define LY_BOT_CY       463

// --- Printing: WiFi signal indicator ---
// LY_WIFI_Y is only the default arg for drawWifiSignalIndicator(); print and
// idle screens both pass an explicit center Y. Kept aligned to LY_BOT_CY so the
// default lands on the bottom bar baseline if any caller relies on it.
#define LY_WIFI_X       6
#define LY_WIFI_Y       463

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

// ===========================================================================
// LANDSCAPE (480x320) - sprite-rotated. Logical canvas is 480 wide x 320 tall.
// Layout pattern: left 360 px = gauge area + ETA + bottom bar; right 120 px =
// status badge (top) + AMS vertical sidebar (below badge). The print-screen
// file-name line is intentionally dropped in landscape to keep r=48 gauges
// without compressing them; the 320 px height is too tight to fit it cleanly
// next to the ETA + bottom bar.
// ===========================================================================

// --- Gauge area width (left column reserved for gauges + ETA + bot bar) ---
#define LY_LAND_GAUGE_W       360

// --- Printing: 2x3 gauge grid (landscape) ---
// Same r=48 / FONT_XLARGE as portrait. Six gauges fit in the 360x215 left
// area with ~9 px gap between row 1 label band (ends y=135) and row 2 gauge
// top (y=144). Label offset is +47 from slot centre for !smallLabels.
#define LY_LAND_COL1          60
#define LY_LAND_COL2          180
#define LY_LAND_COL3          300
#define LY_LAND_ROW1          76     // gauge spans y=28..124, label band 111..135
#define LY_LAND_ROW2          192    // gauge spans y=144..240, label band 227..251
// 4-column landscape variant (DisplaySettings.landscape8Slots).
// Evenly spaced across 480px: 60 / 180 / 300 / 420. R=48 keeps gauges from
// touching neighbours (96px diameter on 120px column pitch).
#define LY_LAND8_COL1         60
#define LY_LAND8_COL2         180
#define LY_LAND8_COL3         300
#define LY_LAND8_COL4         420

// --- AMS sidebar (landscape) ---
#define LY_LAND_AMS_X         365    // 5 px gap from right edge of gauge area (348)
#define LY_LAND_AMS_W         110    // sidebar width; 5 px right margin
#define LY_LAND_AMS_TOP       35     // below header badge band (ends y=30)
// 0-2 AMS units: column ends 5 px above bot bar so bar can sweep full 480 px.
// 3-4 AMS units: column runs to screen bottom; bot bar limited to gauge area.
#define LY_LAND_AMS_BOT_SHORT 280
#define LY_LAND_AMS_BOT_FULL  318

// --- Header bar (landscape) ---
// Portrait header is 26 px tall (y=10..36) and collides with the gauge row 1
// top edge (LY_LAND_ROW1=76, gauge top y=28) in landscape. Use the same
// compact header as CYD/ws_lcd_200 (y=7..27, h=20) so the fillRect band
// stops 1 px above the gauge top and does not eat into the arc.
#define LY_LAND_HDR_Y         7
#define LY_LAND_HDR_H         20
#define LY_LAND_HDR_CY        17
#define LY_LAND_HDR_DOT_CY    10

// --- Header badge (landscape, right column) ---
#define LY_LAND_BADGE_Y       7
#define LY_LAND_BADGE_H       23
#define LY_LAND_BADGE_CY      18

// --- Printing: ETA / info zone (landscape) ---
#define LY_LAND_ETA_Y         252
#define LY_LAND_ETA_H         28
#define LY_LAND_ETA_TEXT_Y    266

// --- Printing: bottom status bar (landscape) ---
#define LY_LAND_BOT_Y         285
#define LY_LAND_BOT_H         35
#define LY_LAND_BOT_CY        302
#define LY_LAND_WIFI_Y        302

// --- Finished screen (landscape) ---
// Gauges side-by-side, centred cluster on the gauge-area mid-line (x=180 since
// AMS sidebar is not drawn on the finished screen). With r=48 the pair sits at
// x=130 and x=350 (full 480 px width used; finished screen does not reserve
// the right column).
#define LY_LAND_FIN_GL        130
#define LY_LAND_FIN_GR        350
#define LY_LAND_FIN_GY        80
#define LY_LAND_FIN_TEXT_Y    160
#define LY_LAND_FIN_FILE_Y    200
#define LY_LAND_FIN_KWH_Y     230
#define LY_LAND_FIN_BOT_Y     285
#define LY_LAND_FIN_BOT_H     35
#define LY_LAND_FIN_WIFI_Y    302

// --- Idle screen (with printer, landscape) ---
// Two-gauge layout centred in the 360 px gauge area when AMS sidebar visible,
// or in the full 480 px canvas when no AMS. Code in drawIdle() picks the
// effective scrW dynamically and uses these Y values either way.
#define LY_LAND_IDLE_NAME_Y     30
#define LY_LAND_IDLE_STATE_Y    60
#define LY_LAND_IDLE_STATE_H    24
#define LY_LAND_IDLE_STATE_TY   72
#define LY_LAND_IDLE_DOT_Y      100
#define LY_LAND_IDLE_GAUGE_R    46
#define LY_LAND_IDLE_GAUGE_Y    195
#define LY_LAND_IDLE_G_OFFSET   72

// --- Idle screen (no printer, landscape) ---
#define LY_LAND_IDLE_NP_TITLE_Y 40
#define LY_LAND_IDLE_NP_WIFI_Y  85
#define LY_LAND_IDLE_NP_DOT_Y   115
#define LY_LAND_IDLE_NP_MSG_Y   155
#define LY_LAND_IDLE_NP_OPEN_Y  195
#define LY_LAND_IDLE_NP_IP_Y    240

// --- AP mode screen (landscape) ---
#define LY_LAND_AP_TITLE_Y      30
#define LY_LAND_AP_SSID_LBL_Y   70
#define LY_LAND_AP_SSID_Y       100
#define LY_LAND_AP_PASS_LBL_Y   140
#define LY_LAND_AP_PASS_Y       170
#define LY_LAND_AP_OPEN_Y       210
#define LY_LAND_AP_IP_Y         245

// --- Simple clock (centred in 320 px landscape height) ---
#define LY_LAND_CLK_CLEAR_Y     50
#define LY_LAND_CLK_CLEAR_H     220
#define LY_LAND_CLK_TIME_Y      140
#define LY_LAND_CLK_AMPM_Y      195
#define LY_LAND_CLK_DATE_Y      235

// --- Pong/Breakout clock (portrait, scaled for 320x480) ---
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

// --- Pong/Breakout clock (landscape 480x320) ---
// 13 cols of 30 px bricks + 12 gaps of 2 px = 414 px; centred at startX=33.
// Paddle near bottom of 320 px play field, time centred vertically.
#define LY_LAND_ARK_BRICK_COLS    13
#define LY_LAND_ARK_BRICK_START_Y 40
#define LY_LAND_ARK_PADDLE_Y      290
#define LY_LAND_ARK_PADDLE_W      50
#define LY_LAND_ARK_TIME_Y        170
#define LY_LAND_ARK_DATE_Y        10

#endif // LAYOUT_320x480_H
