#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  Display
// =============================================================================
#define SCREEN_W        240
#define SCREEN_H        240
#define BACKLIGHT_PIN   TFT_BL  // GPIO 13
#define BACKLIGHT_CH    0
#define BACKLIGHT_FREQ  5000
#define BACKLIGHT_RES   8

// =============================================================================
//  Color palette (RGB565)
// =============================================================================
#define CLR_BG          0x0861   // very dark navy
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
#define BAMBU_CLOUD_BROKER          "us.mqtt.bambulab.com"
#define BAMBU_API_BASE              "https://api.bambulab.com"
#define BAMBU_BUFFER_SIZE           16384   // 16KB for full pushall
#define BAMBU_RECONNECT_INTERVAL    10000   // 10s between attempts
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

// =============================================================================
//  NVS
// =============================================================================
#define NVS_NAMESPACE       "bambu"

// =============================================================================
//  Multi-printer (future)
// =============================================================================
#define MAX_PRINTERS        4

// =============================================================================
//  Display refresh
// =============================================================================
#define DISPLAY_UPDATE_MS   250   // ~4 Hz refresh rate

#endif // CONFIG_H
