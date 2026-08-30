#ifndef LAYOUT_ROUND480_H
#define LAYOUT_ROUND480_H

// Layout profile: ST7701 2.8" round 480x480 (DISPLAY_ROUND_480, ws_lcd_28c).
// Visible area is the inscribed circle: center (240,240), r = 240.
// Widest safe text box centered at row y:
//   halfW = sqrtf(240.0f*240.0f - (y-240.0f)*(y-240.0f)) - margin
// The ST7701 GRAM is a full 480x480 square, so drawing outside the circle is
// harmless (the bezel hides it) - but nothing meaningful may live there.
//
// This is layout_round240.h at exactly 2x, with the 2x font tier. Every
// derivation in the 240 profile's comments has been redone at this radius;
// where a clearance was tight there it is called out again below.
//
// Angles do NOT scale. Arc-length budgets (*_MAXW, in px) do: the arc at 2x
// radius is twice as long and the 2x glyphs eat twice the arc, so the two
// cancel and the half-angles stay put while the MAXW doubles. Measured
// against the real inter_20 / inter_27 advances, which come out within ~1%
// of exactly 2x their 1x faces.

// --- Screen dimensions ---
#define LY_W    480
#define LY_H    480

// This profile is the only one that overrides layout.h's shared UI scale and
// text tier. See the block at the bottom of layout.h for what they cover.
#define LY_SC(v)  ((v) * 2)

#define LY_F_SMALL  FONT_SMALL_2X
#define LY_F_BODY   FONT_BODY_2X
#define LY_F_LARGE  FONT_LARGE_2X

#define LY_GLOW_STORM_SEG_SHIFT  4   // 16 px of rim per bolt segment

// =============================================================================
//  Round-specific constants (LY_RND_*)
// =============================================================================

// --- Rim progress ring (printing + finished screens) ---
// Outer radius 236 runs nearly flush to the physical edge (4 px reserve for
// bezel tolerance), maximizing the usable interior.
#define LY_RND_RING_R      236     // ring outer radius
#define LY_RND_RING_T      14      // ring thickness

// --- Curved rim text (status line on top, ETA line on bottom) ---
// Text arcs just inside the ring (ring inner = R - T = 222). Radius is the
// arc through the glyph centers; the band it occupies is r +/- fontHeight/2.
// FONT_BODY_2X is 39 px tall, so the band reaches 196 + 20 = 216 < 222.
#define LY_RND_ARC_R           196 // glyph-center radius for LY_RND_F_BODY arcs
#define LY_RND_ARC_STATUS_HDEG 50  // top clear sector: 12 o'clock +/- 50 deg
#define LY_RND_ARC_ETA_HDEG    45  // bottom clear sector: 6 o'clock +/- 45 deg
#define LY_RND_ARC_STATUS_MAXW 320 // ellipsize budget (px of arc length;
                                   // 100 deg at r=196 = ~342 px of arc)

// --- Printing screen (variant A: rim ring + 3 mini gauges) ---
#define LY_RND_DOTS_Y      84      // multi-printer dots row (below top arc)
#define LY_RND_PCT_Y       160     // big progress % (center datum); drying reuses this
#define LY_RND_PRINT_PCT_Y 140     // Rim printing %: 1.6x 7-seg digits (~77px)
                                   // span PCT_Y +/- 38. Band corners must stay
                                   // inside r=176 (the status text band).
// Active filament, curved along the upper-left rim. Angles carry over from the
// 240 profile unchanged; only the text budget doubles. Measured inter_20
// advances: "PLA Basic"=115, "PLA Matte"=118, "PETG Trans"=135; 128 px is the
// ceiling with the swatch dot on the arc - same coverage as 64 px at 1x.
#define LY_RND_FIL_CLR_CAA   108   // filament sector clear-band center
#define LY_RND_FIL_CLR_HDEG  23    // clear-band half-angle
#define LY_RND_FIL_TXT_CAA   111   // type text sub-sector center (drawArcAA)
#define LY_RND_FIL_DOT_AA    87    // swatch dot center angle (drawArcAA)
#define LY_RND_FIL_TXT_MAXW  128   // ellipsize budget (px of arc length)
// Right-side mirror sector: door state when the printer has a sensor, else
// speed mode. Narrower than the filament sector (door/speed strings are short).
#define LY_RND_RSTAT_CLR_CAA  254  // clear-band center (drawArcAA)
#define LY_RND_RSTAT_CLR_HDEG 19   // clear-band half-angle
#define LY_RND_RSTAT_TXT_CAA  250  // text sub-sector center (drawArcAA)
#define LY_RND_RSTAT_DOT_AA   269  // status dot center angle (drawArcAA)
#define LY_RND_RSTAT_TXT_MAXW 96   // ellipsize budget (px of arc length)
#define LY_RND_LAYER_Y     208     // "layer n / total" line (center datum)
#define LY_RND_G_R         54      // mini gauge radius
#define LY_RND_G_T         12      // mini gauge arc thickness
#define LY_RND_G_Y         288     // mini gauge row center Y
#define LY_RND_G_X1        116     // nozzle gauge center X
#define LY_RND_G_X2        240     // bed gauge center X
#define LY_RND_G_X3        364     // fan gauge center X

// 7-seg progress digits, 2x the 240 profile. The base built-in face is
// ~48 px; these are the setTextSize() multiplier and the half-height in px.
#define LY_RND_7SEG_SM_SCALE  1.6f   // Rim / Rings (~77 px)
#define LY_RND_7SEG_SM_HALFH  38
#define LY_RND_7SEG_BG_SCALE  2.0f   // Speedo (~96 px)
#define LY_RND_7SEG_BG_HALFH  48

// --- Printing screen, "Speedo" skin (roundSkin = 1) ---
// One large 240-degree gauge arc (start 60, sweep 240, gap at 6 o'clock);
// the bottom gap becomes the text zone for temps, ETA stays curved.
#define LY_RND_SPD_R        214    // big arc outer radius (inner = 190)
#define LY_RND_SPD_T        24     // big arc thickness
#define LY_RND_SPD_STATUS_Y 128    // printer name + state (straight, center datum)
// Curved status arc for the Speedo skin. The text band plus its wipe reaches
// ~R + fh/2 + 1 = 164 + 20 + 1 = 185, clearing the big arc's inner AA edge
// at 189 by 4 px - the same relative margin the 240 profile keeps.
#define LY_RND_SPD_STATUS_R    164 // curved status glyph-center radius
#define LY_RND_SPD_STATUS_HDEG 52  // top clear sector half-angle
#define LY_RND_SPD_DOTS_Y   144    // multi-printer dots row (above the digits)
#define LY_RND_SPD_PCT_Y    208    // big progress % (center datum; 2.0x 7-seg
                                   // digits ~96 px span PCT_Y +/- 48 ->
                                   // 160..256, clearing the dots row by 6 px)
#define LY_RND_SPD_LAYER_Y  300    // layer/watts line (center datum, LY_RND_F_BODY)
#define LY_RND_SPD_FIL_Y    340    // active filament line (dot + material type)
// Speedometer scale ticks at 0/25/50/75/100%, radial between the arc outer
// edge (214 + AA) and the bezel reserve (236).
#define LY_RND_SPD_TICK_RI  220
#define LY_RND_SPD_TICK_RO  230
#define LY_RND_SPD_TEMP_Y   380    // nozzle/bed readouts in the arc gap
#define LY_RND_SPD_NOZ_X    170    // nozzle readout center X
#define LY_RND_SPD_BED_X    310    // bed readout center X
// Curved ETA in the gap below the temps. Sector capped at 36 deg so the band
// clear can't clip the temp readout corners.
#define LY_RND_SPD_ETA_R    208    // curved ETA glyph-center radius
#define LY_RND_SPD_ETA_HDEG 36     // curved ETA clear sector half-angle

// --- Printing screen, "Rings" skin (roundSkin = 2) ---
// Three concentric full-circle rings: progress / nozzle / bed.
// Interior disc r = 148 keeps the center text clears (widest: % at cx+/-100,
// corner dist ~134) off the bed ring.
#define LY_RND_RGS_R1       232    // progress ring outer radius
#define LY_RND_RGS_R2       200    // nozzle ring outer radius
#define LY_RND_RGS_R3       168    // bed ring outer radius
#define LY_RND_RGS_T        20     // ring thickness (all three)
#define LY_RND_RGS_PCT_Y    188    // big progress % (center datum)
#define LY_RND_RGS_TEMP_Y   252    // nozzle/bed readouts (center datum)
#define LY_RND_RGS_NOZ_X    172    // nozzle readout center X (spread apart so the
#define LY_RND_RGS_BED_X    308    // two readouts + markers don't touch)
#define LY_RND_RGS_ETA_Y    304    // remaining line (center datum)
#define LY_RND_RGS_FIL_Y    344    // active filament line (dot + material type;
                                   // band +/-80 x +/-16 -> corner dist 144 < 148)
#define LY_RND_RGS_DOTS_Y   372    // multi-printer dots row (tight chord: the
                                   // clear band must stay inside the r=148 disc)

// --- Idle screen (printer online) ---
#define LY_RND_IDLE_NAME_R   204   // curved printer name radius (no ring here)
#define LY_RND_IDLE_G_Y      304   // nozzle/bed gauge row center Y
#define LY_RND_IDLE_G_R      60
#define LY_RND_IDLE_G_OFF    88    // gauge X offset from center (240 +/- 88)
#define LY_RND_IDLE_WIFI_Y   420   // centered wifi bars baseline
#define LY_RND_IDLE_VER_Y    448   // version string (center datum)

// --- Clock face ---
#define LY_RND_CLK_TICK_RO   236   // tick outer radius
#define LY_RND_CLK_TICK_RI   224   // minor tick inner radius
#define LY_RND_CLK_TICK_RIM  216   // major tick inner radius (12/3/6/9)
#define LY_RND_CLK_DOT_Y     60    // MQTT-connected dot center Y
#define LY_RND_CLK_TIME_Y    240   // time (center datum)
#define LY_RND_CLK_DATE_Y    324   // date line (center datum)
#define LY_RND_CLK_INFO_Y    392   // optional name+IP footer (center datum)

// --- Finished screen ---
#define LY_RND_FIN_CHK_Y     184   // checkmark circle center Y
#define LY_RND_FIN_CHK_R     64
#define LY_RND_FIN_TEXT_Y    312   // "Print Complete!" (center datum)
// Rim-safe ink width for the headline. Ring inner edge is R - T = 222; the
// LY_RND_F_BODY line at y=312 reaches dy = 72 from the center, so the usable
// chord is 2*sqrt(222^2 - 72^2) = 420. 380 keeps a margin. The filename line
// sits lower (dy = 116 -> chord 378) and uses its own tighter 300.
#define LY_RND_FIN_TEXT_MAXW 380
#define LY_RND_FIN_FILE_Y    356   // filename (center datum)
#define LY_RND_FIN_TIME_Y    396   // total time (center datum)

// =============================================================================
//  Legacy LY_* constants (shared code paths; pulled into the circle)
// =============================================================================

// --- LED progress bar: unused on round (rim ring replaces it); keep it a
//     short centered stub so any fallthrough draw stays visible ---
#define LY_BAR_W   80
#define LY_BAR_H   6

// --- Header bar: no header on round screens; centered fallbacks ---
#define LY_HDR_Y        60
#define LY_HDR_H        40
#define LY_HDR_NAME_X   140
#define LY_HDR_CY       80
#define LY_HDR_BADGE_RX 140
#define LY_HDR_DOT_CY   68

// --- Gauge grid fallbacks (safe centers inside the circle) ---
#define LY_GAUGE_R   60
#define LY_GAUGE_T   12
#define LY_TEMP_GAUGE_T 12
#define LY_GAUGE_VALUE_FONT FONT_LARGE_2X
#define LY_GAUGE_VALUE_NUDGE_Y 0
#define LY_COL1      140
#define LY_COL2      240
#define LY_COL3      340
#define LY_ROW1      160
#define LY_ROW2      320

// --- AMS strip: not shown on round (no room inside the circle) ---
#define LY_AMS_Y                220
#define LY_AMS_H                120
#define LY_AMS_BAR_H            72
#define LY_AMS_BAR_GAP          4
#define LY_AMS_GROUP_GAP        16
#define LY_AMS_LABEL_OFFY       8
#define LY_AMS_MARGIN           60
#define LY_AMS_BAR_MAX_W        52
#define LY_AMS_BAR_MAX_W_EXTRAS 44

// --- ETA / info zone ---
#define LY_ETA_Y        392
#define LY_ETA_H        48
#define LY_ETA_TEXT_Y   416

// --- Bottom status bar ---
#define LY_BOT_Y    428
#define LY_BOT_H    32
#define LY_BOT_CY   444

// --- WiFi signal indicator: corner position is invisible on round; round
//     screens draw it near the center column (icon + dBm text extend right
//     from LY_WIFI_X, so 180 puts the block roughly centered) ---
#define LY_WIFI_X    180
#define LY_WIFI_Y    412

// --- Battery indicator (ws_lcd_28c has a battery ADC) ---
#define LY_BAT_W       16
#define LY_BAT_H       32
#define LY_BAT_TEXT_X  24
#define LY_BAT_SHIFT_X 28

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      108
#define LY_IDLE_STATE_Y     144
#define LY_IDLE_STATE_H     40
#define LY_IDLE_STATE_TY    164
#define LY_IDLE_DOT_Y       208
#define LY_IDLE_GAUGE_R     60
#define LY_IDLE_GAUGE_Y     304
#define LY_IDLE_G_OFFSET    88

// --- Idle screen (no printer): centered stack, chord-checked ---
#define LY_IDLE_NP_TITLE_Y  112
#define LY_IDLE_NP_WIFI_Y   176
#define LY_IDLE_NP_DOT_Y    216
#define LY_IDLE_NP_MSG_Y    272
#define LY_IDLE_NP_OPEN_Y   316
#define LY_IDLE_NP_IP_Y     368

// --- Finished screen fallbacks ---
#define LY_FIN_GAUGE_R   56
#define LY_FIN_GL        168
#define LY_FIN_GR        312
#define LY_FIN_GY        184
#define LY_FIN_TEXT_Y    312
#define LY_FIN_FILE_Y    356
#define LY_FIN_BOT_Y     396
#define LY_FIN_BOT_H     40
#define LY_FIN_WIFI_Y    412

// --- AP mode screen: centered stack, chord-checked ---
#define LY_AP_TITLE_Y     104
#define LY_AP_SSID_LBL_Y  172
#define LY_AP_SSID_Y      216
#define LY_AP_PASS_LBL_Y  268
#define LY_AP_PASS_Y      304
#define LY_AP_OPEN_Y      356
#define LY_AP_IP_Y        400

// --- Simple clock (legacy constants; round face uses LY_RND_CLK_*) ---
#define LY_CLK_CLEAR_Y   120
#define LY_CLK_CLEAR_H   240
#define LY_CLK_TIME_Y    200
#define LY_CLK_AMPM_Y    270
#define LY_CLK_DATE_Y    310

// --- Pong/Breakout clock: disabled on round (rectangular walls);
//     constants kept for compilation of the shared translation unit ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS         10
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

#endif // LAYOUT_ROUND480_H
