#ifndef LAYOUT_ROUND240_H
#define LAYOUT_ROUND240_H

// Layout profile: GC9A01 1.28" round 240x240 (DISPLAY_ROUND_240).
// Visible area is the inscribed circle: center (120,120), r = 120.
// Widest safe text box centered at row y:
//   halfW = sqrtf(120.0f*120.0f - (y-120.0f)*(y-120.0f)) - margin
// The GC9A01 GRAM is a full 240x240 square, so drawing outside the circle is
// harmless (pixels simply aren't shown) - but nothing meaningful may live there.
//
// Round-specific screens (dashboard, idle, clock, finished, AP, connecting)
// branch on DISPLAY_ROUND_240 in display_ui.cpp / clock_mode.cpp and use the
// LY_RND_* constants. The legacy LY_* names below keep shared code compiling;
// values are pulled toward the center so any path that does fall through to
// them stays inside the circle.

// --- Screen dimensions ---
#define LY_W    240
#define LY_H    240

// =============================================================================
//  Round-specific constants (LY_RND_*)
// =============================================================================

// --- Rim progress ring (printing + finished screens) ---
// Outer radius 118 runs nearly flush to the physical edge (2 px reserve for
// bezel tolerance on cheap modules), maximizing the usable interior.
#define LY_RND_RING_R      118     // ring outer radius
#define LY_RND_RING_T      7       // ring thickness

// --- Curved rim text (status line on top, ETA line on bottom) ---
// Text arcs just inside the ring (ring inner = R - T = 111). Radius is the
// arc through the glyph centers; the band it occupies is r +/- fontHeight/2.
#define LY_RND_ARC_R           98  // glyph-center radius for FONT_BODY arcs
#define LY_RND_ARC_STATUS_HDEG 55  // top clear sector: 12 o'clock +/- 55 deg
#define LY_RND_ARC_ETA_HDEG    45  // bottom clear sector: 6 o'clock +/- 45 deg
#define LY_RND_ARC_STATUS_MAXW 180 // ellipsize budget (px of arc length)

// --- Printing screen (variant A: rim ring + 3 mini gauges) ---
#define LY_RND_DOTS_Y      42      // multi-printer dots row (below top arc)
#define LY_RND_PCT_Y       80      // big progress % (center datum)
#define LY_RND_LAYER_Y     104     // "layer n / total" line (center datum)
#define LY_RND_G_R         27      // mini gauge radius
#define LY_RND_G_T         6       // mini gauge arc thickness
#define LY_RND_G_Y         144     // mini gauge row center Y
#define LY_RND_G_X1        58      // nozzle gauge center X
#define LY_RND_G_X2        120     // bed gauge center X
#define LY_RND_G_X3        182     // fan gauge center X

// --- Idle screen (printer online) ---
#define LY_RND_IDLE_NAME_R   102   // curved printer name radius (no ring here)
#define LY_RND_IDLE_G_Y      152   // nozzle/bed gauge row center Y
#define LY_RND_IDLE_G_R      30
#define LY_RND_IDLE_G_OFF    44    // gauge X offset from center (120 +/- 44)
#define LY_RND_IDLE_WIFI_Y   210   // centered wifi bars baseline
#define LY_RND_IDLE_VER_Y    224   // version string (center datum)

// --- Clock face ---
#define LY_RND_CLK_TICK_RO   118   // tick outer radius
#define LY_RND_CLK_TICK_RI   112   // minor tick inner radius
#define LY_RND_CLK_TICK_RIM  108   // major tick inner radius (12/3/6/9)
#define LY_RND_CLK_DOT_Y     30    // MQTT-connected dot center Y
#define LY_RND_CLK_TIME_Y    120   // time (center datum)
#define LY_RND_CLK_DATE_Y    162   // date line (center datum)
#define LY_RND_CLK_INFO_Y    196   // optional name+IP footer (center datum)

// --- Finished screen ---
#define LY_RND_FIN_CHK_Y     92    // checkmark circle center Y
#define LY_RND_FIN_CHK_R     32
#define LY_RND_FIN_TEXT_Y    156   // "Print Complete!" (center datum)
#define LY_RND_FIN_FILE_Y    178   // filename (center datum)
#define LY_RND_FIN_TIME_Y    198   // total time (center datum)

// =============================================================================
//  Legacy LY_* constants (shared code paths; pulled into the circle)
// =============================================================================

// --- LED progress bar: unused on round (rim ring replaces it); keep it a
//     short centered stub so any fallthrough draw stays visible ---
#define LY_BAR_W   40
#define LY_BAR_H   3

// --- Header bar: no header on round screens; centered fallbacks ---
#define LY_HDR_Y        30
#define LY_HDR_H        20
#define LY_HDR_NAME_X   70
#define LY_HDR_CY       40
#define LY_HDR_BADGE_RX 70
#define LY_HDR_DOT_CY   34

// --- Gauge grid fallbacks (safe centers inside the circle) ---
#define LY_GAUGE_R   30
#define LY_GAUGE_T   6
#define LY_TEMP_GAUGE_T 6
#define LY_GAUGE_VALUE_FONT FONT_LARGE
#define LY_GAUGE_VALUE_NUDGE_Y 0
#define LY_COL1      70
#define LY_COL2      120
#define LY_COL3      170
#define LY_ROW1      80
#define LY_ROW2      160

// --- AMS strip: not shown on round v1 (no room inside the circle) ---
#define LY_AMS_Y                110
#define LY_AMS_H                60
#define LY_AMS_BAR_H            36
#define LY_AMS_BAR_GAP          2
#define LY_AMS_GROUP_GAP        8
#define LY_AMS_LABEL_OFFY       4
#define LY_AMS_MARGIN           30
#define LY_AMS_BAR_MAX_W        26
#define LY_AMS_BAR_MAX_W_EXTRAS 22

// --- ETA / info zone ---
#define LY_ETA_Y        196
#define LY_ETA_H        24
#define LY_ETA_TEXT_Y   208

// --- Bottom status bar: chord at y=222 is only ~86 px wide; keep items near
//     the center column ---
#define LY_BOT_Y    214
#define LY_BOT_H    16
#define LY_BOT_CY   222

// --- WiFi signal indicator: corner position is invisible on round; round
//     screens draw it near the center column (icon + dBm text extend right
//     from LY_WIFI_X, so 90 puts the block roughly centered) ---
#define LY_WIFI_X    90
#define LY_WIFI_Y    206

// --- Battery indicator: no battery on this board; safe fallbacks ---
#define LY_BAT_W       8
#define LY_BAT_H       16
#define LY_BAT_TEXT_X  12
#define LY_BAT_SHIFT_X 14

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      54
#define LY_IDLE_STATE_Y     72
#define LY_IDLE_STATE_H     20
#define LY_IDLE_STATE_TY    82
#define LY_IDLE_DOT_Y       104
#define LY_IDLE_GAUGE_R     30
#define LY_IDLE_GAUGE_Y     152
#define LY_IDLE_G_OFFSET    44

// --- Idle screen (no printer): centered stack, chord-checked ---
#define LY_IDLE_NP_TITLE_Y  56
#define LY_IDLE_NP_WIFI_Y   88
#define LY_IDLE_NP_DOT_Y    108
#define LY_IDLE_NP_MSG_Y    136
#define LY_IDLE_NP_OPEN_Y   158
#define LY_IDLE_NP_IP_Y     184

// --- Finished screen fallbacks ---
#define LY_FIN_GAUGE_R   28
#define LY_FIN_GL        84
#define LY_FIN_GR        156
#define LY_FIN_GY        92
#define LY_FIN_TEXT_Y    156
#define LY_FIN_FILE_Y    178
#define LY_FIN_BOT_Y     198
#define LY_FIN_BOT_H     20
#define LY_FIN_WIFI_Y    206

// --- AP mode screen: centered stack, chord-checked ---
#define LY_AP_TITLE_Y     52
#define LY_AP_SSID_LBL_Y  86
#define LY_AP_SSID_Y      108
#define LY_AP_PASS_LBL_Y  134
#define LY_AP_PASS_Y      152
#define LY_AP_OPEN_Y      178
#define LY_AP_IP_Y        200

// --- Simple clock (legacy constants; round face uses LY_RND_CLK_*) ---
#define LY_CLK_CLEAR_Y   60
#define LY_CLK_CLEAR_H   120
#define LY_CLK_TIME_Y    100
#define LY_CLK_AMPM_Y    135
#define LY_CLK_DATE_Y    155

// --- Pong/Breakout clock: disabled on round (rectangular walls);
//     constants kept for compilation of the shared translation unit ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS         10
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

#endif // LAYOUT_ROUND240_H
