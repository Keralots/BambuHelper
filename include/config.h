#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  Firmware version
// =============================================================================
#define FW_VERSION          "v3.8.1"

// Board variant — injected into the web UI for OTA asset filtering.
// Normally set via build_flags in platformio.ini; this is a fallback.
#ifndef BOARD_VARIANT
#define BOARD_VARIANT       "esp32s3"
#endif

// =============================================================================
//  Display
// =============================================================================
#include "layout.h"
#define SCREEN_W        LY_W
#define SCREEN_H        LY_H
#ifndef BACKLIGHT_PIN
#define BACKLIGHT_PIN   TFT_BL  // TFT_eSPI: set via -D TFT_BL=N; LovyanGFX: set via -D BACKLIGHT_PIN=N
#endif
// Resolve/validate the generic DIY display flags (no-op unless BOARD_IS_DIY).
// Included here, after BACKLIGHT_PIN, so isDiyReservedPin() can reference it.
#include "diy_display_config.h"
#define BACKLIGHT_CH    0
#define BACKLIGHT_FREQ  5000
#define BACKLIGHT_RES   8

// =============================================================================
//  Color palette (RGB565)
// =============================================================================
#define CLR_BG          0x0000   // black
#define CLR_CARD        0x1926   // dark card bg
#define CLR_HEADER      0x10A2   // header bar bg
// Text colors are user-configurable (#163), so these two are only the factory
// DEFAULTS. The live values are CLR_TEXT / CLR_TEXT_DIM, defined in settings.h
// as dispSettings lookups - a drawing file must include settings.h to paint
// text, and gets a compile error rather than a silently unthemed screen if it
// forgets. Do not reintroduce CLR_TEXT here.
#define CLR_TEXT_DEFAULT     0xFFFF   // white
#define CLR_TEXT_DIM_DEFAULT 0xC618   // gray text
#define CLR_TEXT_DARK        0x7BEF   // darker gray (chrome: inactive dots,
                                      // arc tracks - not text, stays fixed)
#define CLR_GREEN        0x07E0   // bright green
#define CLR_GREEN_DARK   0x0400   // dark green (track)
#define CLR_BLUE         0x34DF   // accent blue
#define CLR_ORANGE       0xFBE0   // nozzle accent
#define CLR_CYAN         0x07FF   // bed accent
#define CLR_RED          0xF800   // error / hot
#define CLR_YELLOW       0xFFE0   // pause / warm
#define CLR_GOLD         0xFEA0   // progress near done
#define CLR_TRACK        0x18E3   // arc background track

// =============================================================================
//  MQTT / Bambu
// =============================================================================
#define BAMBU_PORT                  8883
#define BAMBU_USERNAME              "bblp"
#define BAMBU_BUFFER_SIZE           40960   // 40KB - H2C with 3 AMS sends ~33KB
#define BAMBU_RECONNECT_INTERVAL    10000   // 10s between attempts
#define BAMBU_BACKOFF_PHASE1        5       // first N attempts at normal interval
#define BAMBU_BACKOFF_PHASE2_MS     60000   // 60s after phase 1 exhausted
#define BAMBU_BACKOFF_PHASE2        10      // next N attempts at phase 2 interval
#define BAMBU_BACKOFF_PHASE3_MS     120000  // 120s after phase 2 exhausted
#define BAMBU_STALE_TIMEOUT         60000   // 60s no data = stale
#define BAMBU_PRINT_STALE_TIMEOUT   120000  // cloud: 120s no core print data = stale (LAN uses BAMBU_STALE_TIMEOUT)
#define BAMBU_PUSHALL_INTERVAL      30000   // request full status every 30s (LAN)
#define BAMBU_PUSHALL_INITIAL_DELAY 2000    // wait 2s after connect
#define BAMBU_CLOUD_IDLE_PUSHALL_INTERVAL 300000 // cloud: poll full status every 5 min while NOT printing, to catch a cloud-dropped IDLE->RUNNING delta. Safe: stricter than the 2-min recovery throttle, far above the ~30s LAN cadence that risks access_denied, within the documented >=5-min pushall guidance.
#define BAMBU_MIN_FREE_HEAP         40000   // min heap for TLS allocation
#define BAMBU_KEEPALIVE             60
#define BAMBU_CLOUD_KEEPALIVE       30      // cloud: longer than LAN to tolerate internet jitter

// =============================================================================
//  WiFi
// =============================================================================
#define WIFI_AP_PREFIX      "BambuHelper-"
#define WIFI_AP_PASSWORD    "bambu1234"
#define WIFI_CONNECT_TIMEOUT 15000  // 15s STA connect timeout
#define WIFI_RECONNECT_TIMEOUT 60000 // 60s before re-entering AP mode
#define WIFI_BACKOFF_PHASE1_MS    10000   // 10s between attempts in phase 1
#define WIFI_BACKOFF_PHASE2_MS    30000   // 30s between attempts after phase 1
#define WIFI_BACKOFF_PHASE3_MS    60000   // 60s between attempts after phase 2
#define WIFI_BACKOFF_PHASE2_START 5       // start phase 2 after this many attempts
#define WIFI_BACKOFF_PHASE3_START 10      // start phase 3 after this many attempts
#define WIFI_STA_PROBE_INTERVAL   120000  // 2 min between STA probes while in AP mode
#define WIFI_STA_PROBE_CHECK_MS    10000  // 10s after probe start before checking result
#define WIFI_AP_FALLBACK_MS       900000  // 15 min in phase 3 before falling back to AP

// Improv WiFi serial setup window on first boot (no stored credentials).
// During this window the device listens for Improv commands from the web
// flasher; on timeout it falls back to the AP captive portal.
// 3 minutes is generous on purpose: ESP Web Tools needs ~15s to probe after
// flash, then the user has to find their SSID, fetch the password from a
// router sticker / password manager, and type it in. Anything shorter
// punishes slow users with a "you missed the dialog" cliff.
#define IMPROV_SETUP_WINDOW_MS    180000  // 3 min window for Improv setup at first boot

// =============================================================================
//  NVS
// =============================================================================
#define NVS_NAMESPACE       "bambu"

// Any epoch below this means NTP has not answered yet and the RTC is still at
// its power-on value, so a wall-clock reading must not be trusted or displayed.
#define NTP_SYNCED_EPOCH    1704067200UL   // 2024-01-01 00:00:00 UTC

// =============================================================================
//  Multi-printer
// =============================================================================
#define MAX_PRINTERS          4       // NVS config slots
// Simultaneous TLS+MQTT sessions. Each session costs ~40 KB MQTT buffer +
// ~40-50 KB mbedTLS. Beyond two, the connections only fit on PSRAM boards:
// there the 40 KB buffers land in PSRAM, leaving internal SRAM free for the
// handshake. On no-PSRAM S3 boards (esp32s3, ws_lcd_200, ws_lcd_154) a third
// handshake runs out of SRAM mid-flight and the watchdog freezes the device,
// so they stay at two - matching the long-standing 2-printer behavior.
// PSRAM boards run up to four (= MAX_PRINTERS); hardware-confirmed that four
// TLS sessions establish without the freeze, with ~51 KB internal heap free.
#ifdef BOARD_HAS_PSRAM
#define MAX_ACTIVE_PRINTERS   4       // max simultaneous MQTT connections
#else
#define MAX_ACTIVE_PRINTERS   2
#endif

// =============================================================================
//  Camera live view (issue #120)
// =============================================================================
// A P1/A1 LAN camera needs a 2nd TLS socket + a full-JPEG PSRAM buffer + touch.
// Only these two boards qualify (8 MB PSRAM + touch); BOARD_HAS_PSRAM is too
// broad (it also covers esp32s3_zero/_320/sensecap). Every camera code path is
// compiled behind BOARD_HAS_CAMERA; other boards link no-op stubs.
#if defined(BOARD_IS_JC3248W535) || defined(BOARD_IS_WS350)
#define BOARD_HAS_CAMERA      1
#else
#define BOARD_HAS_CAMERA      0
#endif

// =============================================================================
//  Board capability derivation
// =============================================================================
// Logic files test these capabilities, never board names. The board -> capability
// mapping lives ONLY here (plus lgfx_boards.h / diy_display_config.h and the named
// resolver blocks). Each new capability is ALWAYS defined 0/1 and tested with
// `#if CAP`, so a typo in a consumer resolves to 0 at build time and the full-env
// build matrix exercises both branches. See the "Board capability macros" section
// in CLAUDE.md. (Pre-existing presence-only macros such as BOARD_HAS_PSRAM /
// BOARD_HAS_BATTERY / BOARD_HAS_*_AUDIO are grandfathered - do not blanket-convert.)

// JC3248W535 AXS15231B QSPI panel cannot address an arbitrary Y per draw, so all
// drawing renders into a full-frame PSRAM sprite flushed once per loop tick. The
// guarded code is AXS-typed (Panel_AXS15231B_AGFX, fixed 320x480) and is 1:1 with
// JC today - a different full-frame panel (e.g. the CO5300 AMOLED) needs its own
// path, not this one.
// Round panel. All three print skins, the round clock face, the edge-glow ring
// and the round idle/finished/AP screens are shared; only the LY_RND_* geometry
// and the font tier differ per resolution.
//
// NOTE: derived here, so it is only visible to a TU that has already reached
// this point in config.h. include/layout.h and src/display_gauges.h are both
// included earlier than that and MUST keep the raw two-flag test.
#if defined(DISPLAY_ROUND_240) || defined(DISPLAY_ROUND_480)
#define DISPLAY_IS_ROUND  1
#else
#define DISPLAY_IS_ROUND  0
#endif

#if defined(BOARD_IS_JC3248W535)
#define PANEL_REQUIRES_AXS_FRAME_SPRITE  1
#else
#define PANEL_REQUIRES_AXS_FRAME_SPRITE  0
#endif

// Display reset/control routed through an I2C IO expander instead of GPIOs:
// SenseCAP (PCA9535) and ws_lcd_350 (TCA9554). This cap only signals "bring up
// Wire.h for the expander"; the per-chip register bring-up stays board-specific
// (different chip, address and pins - see initDisplay()).
#if defined(BOARD_IS_SENSECAP) || defined(BOARD_IS_WS350)
#define PANEL_HAS_IO_EXPANDER  1
#else
#define PANEL_HAS_IO_EXPANDER  0
#endif

// ESP32-C3 radio: capping AP/STA TX power works around a C3 range/brownout issue
// (see wifi_manager.cpp). Expressed as a capability so the workaround is portable
// if another board ever needs it.
#if defined(BOARD_IS_C3)
#define BOARD_NEEDS_WIFI_TX_LIMIT  1
#else
#define BOARD_NEEDS_WIFI_TX_LIMIT  0
#endif

// Onboard RGB status LED. The colour LED feature itself is board-independent -
// any board can drive a hand-wired RGB LED or a WS2812 off free GPIOs - so these
// two capabilities only say "this board already has one soldered on", which buys
// exactly two things: sane pin defaults in the portal, and permission for the LED
// driver to claim pins the deny-list otherwise reserves (see isOnboardRgbPin() in
// led.cpp). They are mutually exclusive because the two kinds of onboard RGB share
// nothing in silicon:
//
//   BOARD_HAS_ONBOARD_RGB_PWM    three plain GPIOs, one per colour, three LEDC
//                                channels (CYD family, common anode)
//   BOARD_HAS_ONBOARD_RGB_PIXEL  one data line into a WS2812, bit-banged over RMT
#if defined(DISPLAY_CYD) || defined(BOARD_IS_TZT_2432)
#define BOARD_HAS_ONBOARD_RGB_PWM    1
#else
#define BOARD_HAS_ONBOARD_RGB_PWM    0
#endif

#if defined(BOARD_IS_S3_ZERO) || defined(BOARD_IS_ES3N28P)
#define BOARD_HAS_ONBOARD_RGB_PIXEL  1
#else
#define BOARD_HAS_ONBOARD_RGB_PIXEL  0
#endif

#if BOARD_HAS_ONBOARD_RGB_PWM && BOARD_HAS_ONBOARD_RGB_PIXEL
#error "A board has one kind of onboard RGB LED, not both"
#endif

#define BOARD_HAS_ONBOARD_RGB  (BOARD_HAS_ONBOARD_RGB_PWM || BOARD_HAS_ONBOARD_RGB_PIXEL)

// The WS2812 pixel driver pulls in the RMT peripheral driver (~12 KB flash where
// the three-pin PWM path is a few hundred bytes). It used to be dropped on the
// 1.75 MB C3 boards, whose image would otherwise land past its app slot - but the
// -fno-exceptions flash reclaim on those envs (see the esp32c3 build_flags) freed
// ~148 KB, so every board can now afford it. All boards offer single / RGB /
// WS2812.
#define HAS_LED_PIXEL  1

#if BOARD_HAS_ONBOARD_RGB_PIXEL && !HAS_LED_PIXEL
#error "A board with an onboard WS2812 cannot drop the pixel driver"
#endif

// HMS / print_error reporting. Four independent capabilities:
//
//   HAS_HMS_UI           the feature exists at all - state fields, parser,
//                        badge, error screen, alerts
//   HAS_ERROR_TEXT_TABLE embedded print_error sentences (~36 KB flash)
//   HAS_FULL_HMS_TABLE   embedded HMS sentences (~194 KB flash)
//   HAS_HMS_KNOWN_TABLE  embedded HMS key set, no sentences (~6 KB flash) -
//                        derived, on wherever HAS_FULL_HMS_TABLE is off
//
// A code with no embedded sentence still shows "<MODULE> <SEVERITY>" plus the
// formatted code (~100 bytes). That is the normal case for HMS codes on 4 MB
// boards, where the sentence comes from the web portal instead.
//
// HAS_HMS_UI is on for every env. HAS_ERROR_TEXT_TABLE is off on the C3s, and
// HAS_FULL_HMS_TABLE below is on only for the big-flash boards.
//
// The C3 exception came back in v3.8.0, and the reason is worth writing down
// because the obvious measurement lies. PlatformIO's "Flash: used" line is the
// linker section sum; the image that OTA uploads and that Full.bin embeds is
// `firmware.bin`, roughly 88 KB larger on this target because of segment
// padding. Judged by the PIO number the stock esp32c3 had 78 KB spare - judged
// by the image it was 7,408 bytes OVER its 1,835,008 B app slot, which would
// have overrun app1 and bricked the board on its next OTA. Always size a 4 MB
// board with `stat .pio/build/<env>/firmware.bin`, never with the build summary.
//
// Dropping the print_error sentences here buys 36 KB of image and leaves the
// stock C3 ~28 KB and the round one ~39 KB. The cost is confined to the device
// screen, which falls back to "<MODULE> <SEVERITY>" plus the code: the portal
// still shows the full wording, because it resolves both domains from the
// published mirror in the user's browser whenever the device has no text of its
// own. So the rule on a C3 is simply "codes on the screen, sentences in the
// portal" - which is what its HMS codes already did.
#define HAS_HMS_UI            1
#if BOARD_IS_C3
#define HAS_ERROR_TEXT_TABLE  0
#else
#define HAS_ERROR_TEXT_TABLE  1
#endif

// The full HMS table only fits envs built on the 8 MB / 16 MB partition
// tables, and no flash-size macro exists to key off - so this is enumerated
// per board, which is exactly what this block is the allowed site for.
// Fielded 16 MB devices never reflashed since v3.7.4 still carry the old
// 1.75 MB OTA slot and will be told to repartition by the portal's update
// checker; that is a known, accepted one-time migration.
#if defined(BOARD_IS_WS154) || defined(BOARD_IS_WS200) || \
    defined(BOARD_IS_WS280) || defined(BOARD_IS_WS350) || \
    defined(BOARD_IS_JC3248W535) || defined(BOARD_IS_SC01PLUS) || \
    defined(BOARD_IS_ES3N28P) || defined(BOARD_IS_SC05X) || \
    defined(BOARD_IS_SENSECAP) || defined(BOARD_IS_AMOLED216)
#define HAS_FULL_HMS_TABLE  1
#else
#define HAS_FULL_HMS_TABLE  0
#endif

// Whether Bambu describes a code at all decides whether it reaches the screen
// (issue #164 - an X2D standing on two codes its own UI does not list, because
// Bambu registers them and publishes no sentence). Answering that needs the key
// set, not the text: a board carrying the full table already has it, and every
// other board gets hms_known.h, the same 2014 keys with the sentences dropped
// and grouped by attr - ~6 KB instead of ~194 KB. Both tables come out of the
// same generator run, so the two paths can never disagree.
#if HAS_HMS_UI && !HAS_FULL_HMS_TABLE
#define HAS_HMS_KNOWN_TABLE  1
#else
#define HAS_HMS_KNOWN_TABLE  0
#endif

// The portal's "Printer Errors" section is markup, and markup is flash: it
// costs ~7 KB, the same on every board. Kept as its own capability - and
// PAGE_HTML kept split into three streamed literals - so a board that runs out
// of app slot can still drop the settings page while keeping the parts that
// matter without a keyboard: the badge, the error screen, the cancel wording.
// Such a board runs on the defaults (reporting on, important-only, no alert
// channels), which a settings import can override.
//
// Defined in terms of HAS_HMS_UI rather than as a literal, so "the section
// cannot exist without the feature" is structural - which is why there is no
// #error guard for that pairing below.
#define HAS_HMS_WEB_UI  HAS_HMS_UI

#if HAS_FULL_HMS_TABLE && !HAS_HMS_UI
#error "HAS_FULL_HMS_TABLE requires HAS_HMS_UI"
#endif

// Published mirror of Bambu's error-text feed, read by the portal page in the
// user's browser - never by the device. e.bambulab.com sends no CORS header at
// all, so the page cannot read the feed directly; GitHub Pages sends
// "Access-Control-Allow-Origin: *", and release.py refreshes this copy.
// Boards that carry the full table on-device resolve text themselves and only
// fall back to this for codes the table does not have.
#define HMS_MIRROR_URL  "https://keralots.github.io/BambuHelper/errors/hms_en.json"

// Signing into a Bambu account from the device itself (portal form -> cloud
// token, no Companion Tool). Every board carries it: gzipping the portal's CSS
// and JS freed ~77 KB of app slot, which is far more than the flow costs even
// on the 1.75 MB C3 boards. Kept as a capability so a future flash-starved
// board can drop it without touching anything but this block.
#define HAS_CLOUD_LOGIN  1

// =============================================================================
//  Display rotation (multi-printer)
// =============================================================================
#define ROTATE_INTERVAL_MS    60000   // default auto-rotate: 1 min
#define ROTATE_MIN_MS         10000   // min allowed interval (10s)
#define ROTATE_MAX_MS         600000  // max allowed interval (10 min)

// =============================================================================
//  Gauge full-scale ranges (user-configurable arc maxima)
//  Defaults cover every Bambu printer; lowering a scale makes the arc sweep
//  fuller for a printer's normal range (better visual resolution).
// =============================================================================
#define GAUGE_NOZZLE_SCALE_DEFAULT  300   // C
#define GAUGE_NOZZLE_SCALE_MIN      100
#define GAUGE_NOZZLE_SCALE_MAX      400
#define GAUGE_BED_SCALE_DEFAULT     120   // C
#define GAUGE_BED_SCALE_MIN         40
#define GAUGE_BED_SCALE_MAX         150
#define GAUGE_CHAMBER_SCALE_DEFAULT 60    // C (also AMS unit-temp gauge)
#define GAUGE_CHAMBER_SCALE_MIN     30
#define GAUGE_CHAMBER_SCALE_MAX     120
#define GAUGE_POWER_SCALE_DEFAULT   1000  // W (Tasmota power gauge)
#define GAUGE_POWER_SCALE_MIN       100
#define GAUGE_POWER_SCALE_MAX       5000

// =============================================================================
//  Physical button
// =============================================================================
#if defined(BOARD_IS_DIY)
#define BUTTON_DEFAULT_PIN    0       // DIY: no button assumed; user configures in web UI
#elif defined(DISPLAY_240x320)
#define BUTTON_DEFAULT_PIN    0       // CYD: GPIO4 is RGB LED, not usable
#elif defined(BOARD_IS_SENSECAP)
#define BUTTON_DEFAULT_PIN    38      // SenseCAP Indicator: GPIO38 (inverted, normally HIGH)
#else
#define BUTTON_DEFAULT_PIN    4       // default GPIO for physical button
#endif

// =============================================================================
//  Display refresh
// =============================================================================
#if defined(BOARD_IS_SENSECAP)
#define DISPLAY_UPDATE_MS          100    // ~10 Hz refresh (PSRAM framebuffer can handle it)
#else
#define DISPLAY_UPDATE_MS          250    // ~4 Hz refresh rate
#endif
#define DISPLAY_STATE_TIMEOUT_MS   60000  // 60s timeout for intermediate display states

// =============================================================================
//  Edge glow (border light effect on print complete / failed)
// =============================================================================
#define GLOW_THICKNESS_PX        8         // border band thickness (rectangular)
#if defined(BOARD_IS_JC3248W535)
#define GLOW_ANIM_MS             80        // every tick flushes the full QSPI frame - keep modest
#elif DISPLAY_IS_ROUND
#define GLOW_ANIM_MS             40        // round ring = a few fillArc wedges, 25 fps fine
#else
#define GLOW_ANIM_MS             40        // ~25 fps band animation
#endif
#define GLOW_SWEEP_LAP_MS        1200UL    // sweep head: one full lap around the border
#define GLOW_PULSE_PERIOD_MS     2000UL    // pulse breathing cycle
#define GLOW_FADE_MS             500UL     // fade-out when a burst ends
#define GLOW_BURST_MS            60000UL   // burst duration
#define GLOW_REMIND_ON_MS        5000UL    // reminder pulse length
#define GLOW_REMIND_EVERY_MS     300000UL  // reminder interval (5 min)
#define GLOW_CONT_CEILING_MS     1800000UL // until-dismissed degrades to reminder after 30 min

// Round ring variant (DISPLAY_IS_ROUND): the glow owns an annular band at the
// panel rim (a full circle) instead of four straight edge strips. Sweep is a
// single-pass per-pixel band rasterizer (no fillArc, so no angular-seam specks
// and no whole-ring strobe); Pulse and Storm use native fillArc. fillArc scans
// its full bounding box per call (~O(r^2)), so those keep their fill count low.
// Outer radius reaches the panel edge to fully cover the finished screen's gold
// rim and its anti-aliased fringe (drawn at r118, AA out to ~r119); a shy ring
// leaves single gold pixels peeking through the moving sweep.
#define GLOW_RING_MARGIN         0         // px from panel edge to ring outer radius
#define GLOW_RING_T              11        // ring thickness (outer 120 -> inner 109 on 240 round)
#define GLOW_SWEEP_TAIL_DEG      90        // sweep comet tail length (deg of the circle)

// =============================================================================
//  Buzzer (optional passive buzzer)
// =============================================================================
#if defined(BOARD_IS_DIY)
// DIY: no buzzer assumed - the generic C3 default (GPIO3) is often the display's
// MOSI, and the S3 default (GPIO5) a common SCLK. Disable; user picks a free pin.
#define BUZZER_DEFAULT_PIN    0
#elif defined(BOARD_IS_C3)
#define BUZZER_DEFAULT_PIN    3       // C3: GPIO 3 (GPIO 5 is backlight)
#elif defined(BOARD_IS_SENSECAP)
// SenseCAP Indicator: no buzzer hardware connected to ESP32-S3 GPIO.
// The onboard MLT-8530 is on the RP2040 co-processor (not directly accessible).
// Buzzer disabled (pin 0 = disabled in buzzer backend).
#define BUZZER_DEFAULT_PIN    0
#elif defined(BOARD_IS_ES3N28P)
// QD ES3N28P 2.8": no GPIO buzzer hardware. This board also defines
// DISPLAY_240x320, so without this dedicated branch it would fall through to the
// CYD default (GPIO 26) - which on an ESP32-S3 sits inside the flash/PSRAM
// region. The generic S3 fallback (GPIO 5) is no good either: GPIO 5 is this
// board's I2S BCK (audio). initBuzzer() drives the pin regardless of the enabled
// flag, so disable (0) to leave audio/PSRAM alone. (I2C SCL is GPIO 15 here.)
#define BUZZER_DEFAULT_PIN    0
#elif defined(BOARD_IS_SC05X)
// Panlee SC05_X / ZX2D80CE02S: no buzzer hardware is wired in this target, and
// the DISPLAY_240x320 fallback would pick GPIO26 (inside the ESP32-S3
// flash/PSRAM range). Disable the GPIO buzzer backend by default.
#define BUZZER_DEFAULT_PIN    0
#elif defined(BOARD_IS_WS200)
// Waveshare ESP32-S3-Touch-LCD-2.0: GPIO 4 is free and is the pin the wiring
// docs recommend. Neither generic fallback works here - the DISPLAY_240x320
// branch below would hand out the CYD's GPIO 26 (inside the ESP32-S3
// flash/PSRAM range), and the S3 fallback GPIO 5 is this board's battery ADC.
#define BUZZER_DEFAULT_PIN    4
#elif defined(BOARD_IS_WS280)
// Waveshare ESP32-S3-Touch-LCD-2.8: no buzzer hardware, and both generic
// fallbacks are wrong - GPIO 26 (CYD) is in the S3 flash/PSRAM range and
// GPIO 5 is this board's backlight. GPIO 4 is the CST328 touch IRQ here, so it
// is not a safe shared default either. Disable (0); this board is not
// hardware-tested in-house, so the user picks a free pin in the web UI.
#define BUZZER_DEFAULT_PIN    0
#elif defined(DISPLAY_CYD) || defined(BOARD_IS_TZT_2432)
// ESP32-classic CYD boards only. This used to key off DISPLAY_240x320, which
// also matches the ESP32-S3 boards that reuse the same layout profile
// (ws_lcd_200, ws_lcd_280, esp32s3_zero_320) - on those, GPIO 26 is a
// flash/PSRAM bus pin, not a free GPIO.
#define BUZZER_DEFAULT_PIN    26      // CYD: GPIO 26
#elif defined(BOARD_IS_WS350)
// Waveshare ESP32-S3-Touch-LCD-3.5: no buzzer hardware. Critically, the generic
// S3 default (GPIO 5) is this board's display SCLK — initBuzzer() drives the pin
// as a GPIO regardless of the enabled flag, which would hijack SCLK and freeze
// the panel after boot. Disable (0) so the SPI clock is left alone.
#define BUZZER_DEFAULT_PIN    0
#elif defined(BOARD_IS_SC01PLUS)
// Panlee WT32-SC01 Plus: the generic S3 default (GPIO 5) is this board's touch
// I2C SCL — initBuzzer() drives the pin regardless of the enabled flag, which
// would hijack SCL and break the FT6336 touch. Disable (0). (The board does have
// a real I2S audio amp on LRCK=35/BCLK=36/DOUT=37; a dedicated backend could
// drive it later, but audio is unverifiable without the hardware.)
#define BUZZER_DEFAULT_PIN    0
#else
#define BUZZER_DEFAULT_PIN    5       // S3: GPIO 5
#endif

#if defined(DISPLAY_CYD)
// 2.8" ESP32-32E CYD clone (lcdwiki.com/2.8inch_ESP32-32E_Display): same
// display/touch/backlight pinout as the classic ESP32-2432S028, but the
// speaker amp (FM8002E) has an ACTIVE-LOW enable pin on GPIO4 - low = amp on,
// high = mute (classic: GPIO4 is the red LED, amp hardwired on) - and the RGB
// LED red channel moves to GPIO22. Audio out stays on GPIO26.
// Selected at runtime via dispSettings.cyd32eVariant (Settings checkbox).
#define CYD32E_AMP_EN_PIN     4
#define CYD32E_LED_R_PIN      22
#define CYD32E_LED_G_PIN      16
#define CYD32E_LED_B_PIN      17
#endif

// =============================================================================
//  Status LED (optional: single PWM channel, 3-pin RGB, or one WS2812)
// =============================================================================
// LEDC channel budget. analogWrite() (the backlight) allocates downward from the
// top of the channel bank - 15 on ESP32, 7 on S3, 5 on C3 - and tone() sits on
// channel 0, so 2/3/4 are free on every target this firmware builds for.
#define LED_PWM_CH      2     // LEDC channel (single mode; RGB mode: red)
#define LED_PWM_CH_G    3     // RGB mode: green
#define LED_PWM_CH_B    4     // RGB mode: blue
#define LED_PWM_FREQ    5000  // PWM frequency (Hz)
#define LED_PWM_RES     8     // PWM resolution (bits) -> 0..255 duty

#if defined(BOARD_IS_DIY)
#define LED_DEFAULT_PIN 0     // DIY: no status LED assumed; user configures in web UI
#elif defined(DISPLAY_CYD)
#define LED_DEFAULT_PIN 22    // CYD: GPIO 22 on P3 connector
#elif defined(BOARD_IS_SENSECAP)
#define LED_DEFAULT_PIN 0     // SenseCAP Indicator: no dedicated LED pin (user must configure)
#else
#define LED_DEFAULT_PIN 0     // other boards: user must configure
#endif

// Onboard RGB pin defaults. Only ever used to pre-fill the portal's pin fields
// and to let the driver claim those pins back from the deny-list; a board with
// no onboard RGB simply starts the three fields at 0 and the user types pins in.
#if BOARD_HAS_ONBOARD_RGB_PWM
// CYD / TZT onboard RGB is common ANODE - the shared leg sits on 3V3, so a LOW
// level lights the channel. Red moves to GPIO 22 on the ESP32-32E clone, which
// is a runtime variant (dispSettings.cyd32eVariant), resolved in led.cpp.
#define ONBOARD_RGB_R_PIN     4
#define ONBOARD_RGB_G_PIN     16
#define ONBOARD_RGB_B_PIN     17
#define ONBOARD_RGB_ANODE     1
#else
#define ONBOARD_RGB_R_PIN     0
#define ONBOARD_RGB_G_PIN     0
#define ONBOARD_RGB_B_PIN     0
#define ONBOARD_RGB_ANODE     0
#endif

#if BOARD_HAS_ONBOARD_RGB_PIXEL
#if defined(BOARD_IS_ES3N28P)
#define ONBOARD_RGB_DATA_PIN  42
#else
#define ONBOARD_RGB_DATA_PIN  21    // Waveshare ESP32-S3-Zero
#endif
#else
#define ONBOARD_RGB_DATA_PIN  0
#endif

// LED effect tuning (ms periods)
#define LED_BREATH_PERIOD_MS      2000   // breathing pulse 0->peak->0
#define LED_HEARTBEAT_PERIOD_MS   1500   // pyk-pyk-pause cycle
#define LED_PAUSE_PERIOD_MS       3500   // slow breath while printer paused
#define LED_ERROR_STROBE_MS        180   // strobe half-period (180 on / 180 off)
#define LED_ERROR_STROBE_DEFAULT_S  60   // error strobe auto-off after N s (0 = never)
#define LED_TICK_MIN_INTERVAL_MS    16   // throttle ledTick() to ~60Hz
#define LED_TEST_DURATION_S          8   // /led/test runs the chosen effect for 8s

// Default per-state colours, 0xRRGGBB. Intensity is the brightness slider's job,
// so these are saturated hues: white = standby, blue = working, amber = wants
// attention, green = done, red = fault.
#define LED_COLOR_IDLE_DEFAULT      0xFFFFFF
#define LED_COLOR_PRINTING_DEFAULT  0x0080FF
#define LED_COLOR_PAUSED_DEFAULT    0xFF9000
#define LED_COLOR_FINISHED_DEFAULT  0x00FF40
#define LED_COLOR_ERROR_DEFAULT     0xFF0000

#endif // CONFIG_H
