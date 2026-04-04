#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  Firmware version
// =============================================================================
#define FW_VERSION          "v2.5"

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
#define BACKLIGHT_PIN   TFT_BL  // set by build flags per board
#define BACKLIGHT_CH    0
#define BACKLIGHT_FREQ  5000
#define BACKLIGHT_RES   8

// =============================================================================
//  Color palette (RGB565)
// =============================================================================
#define CLR_BG          0x0000   // black
#define CLR_CARD        0x1926   // dark card bg
#define CLR_HEADER      0x10A2   // header bar bg
#define CLR_TEXT         0xFFFF   // white
#define CLR_TEXT_DIM     0xC618   // gray text
#define CLR_TEXT_DARK    0x7BEF   // darker gray
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
#define BAMBU_PUSHALL_INTERVAL      30000   // request full status every 30s
#define BAMBU_PUSHALL_INITIAL_DELAY 2000    // wait 2s after connect
#define BAMBU_MIN_FREE_HEAP         40000   // min heap for TLS allocation
#define BAMBU_KEEPALIVE             60

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

// =============================================================================
//  NVS
// =============================================================================
#define NVS_NAMESPACE       "bambu"

// =============================================================================
//  Multi-printer
// =============================================================================
#define MAX_PRINTERS          4       // NVS config slots
#define MAX_ACTIVE_PRINTERS   2       // max simultaneous MQTT connections

// =============================================================================
//  Display rotation (multi-printer)
// =============================================================================
#define ROTATE_INTERVAL_MS    60000   // default auto-rotate: 1 min
#define ROTATE_MIN_MS         10000   // min allowed interval (10s)
#define ROTATE_MAX_MS         600000  // max allowed interval (10 min)

// =============================================================================
//  Physical button
// =============================================================================
#ifdef DISPLAY_CYD
#define BUTTON_DEFAULT_PIN    0       // CYD: GPIO4 is RGB LED, not usable
#else
#define BUTTON_DEFAULT_PIN    4       // default GPIO for physical button
#endif

// =============================================================================
//  Display refresh
// =============================================================================
#define DISPLAY_UPDATE_MS   250   // ~4 Hz refresh rate
#define DISPLAY_STATE_TIMEOUT_MS   60000  // 60s timeout for intermediate display states

// =============================================================================
//  Buzzer (optional passive buzzer)
// =============================================================================
#define BUZZER_DEFAULT_PIN    5       // default GPIO for buzzer

#endif // CONFIG_H
