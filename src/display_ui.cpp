#include "display_ui.h"
#include "display_gauges.h"
#include "display_anim.h"
#include "clock_mode.h"
#include "clock_pong.h"
#include "icons.h"
#include "config.h"
#include "layout.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "settings.h"
#include "tasmota.h"
#include <WiFi.h>
#include <time.h>

// =============================================================================
//  LovyanGFX board-specific configurations
// =============================================================================

#if defined(BOARD_IS_S3)
// --- ESP32-S3 Super Mini + ST7789 240x240 ------------------------------------
class LGFX_S3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_S3() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 9;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = 8;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_S3 _tft_instance;

#elif defined(DISPLAY_CYD)
// --- ESP32-2432S028 (CYD) + ILI9341 240x320 ---------------------------------
class LGFX_CYD : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_CYD() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = VSPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 27000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 14;
      cfg.pin_mosi   = 13;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 2;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs    = 15;
      cfg.pin_rst   = 12;
      cfg.pin_busy  = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.invert        = false;
      cfg.rgb_order     = false;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_CYD _tft_instance;

#elif defined(BOARD_IS_C3)
// --- ESP32-C3 Super Mini + ST7789 240x280 ------------------------------------
class LGFX_C3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_C3() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 21;
      cfg.pin_mosi   = 20;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 7;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 6;
      cfg.pin_rst  = 10;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 280;
      cfg.panel_width   = 240;
      cfg.panel_height  = 280;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_C3 _tft_instance;

#else
  #error "No board variant defined. Add BOARD_IS_S3, DISPLAY_CYD, or BOARD_IS_C3 to build_flags."
#endif

// Global pointer + reference — accessed via `tft` throughout the codebase
lgfx::LovyanGFX* tft_ptr = &_tft_instance;
lgfx::LovyanGFX& tft     = *tft_ptr;

// Use user-configured bg color instead of hardcoded CLR_BG
#undef  CLR_BG
#define CLR_BG  (dispSettings.bgColor)

static ScreenState currentScreen = SCREEN_SPLASH;
static ScreenState prevScreen = SCREEN_SPLASH;
static bool forceRedraw = true;
static unsigned long lastDisplayUpdate = 0;

// Previous state for smart redraw
static BambuState prevState;
static bool prevWaitingForDoor = false;
static unsigned long connectScreenStart = 0;

// ---------------------------------------------------------------------------
//  Smooth gauge interpolation - values lerp toward MQTT actuals each frame
// ---------------------------------------------------------------------------
static float smoothNozzleTemp   = 0;
static float smoothBedTemp      = 0;
static float smoothPartFan     = 0;
static float smoothAuxFan      = 0;
static float smoothChamberFan  = 0;
static float smoothChamberTemp = 0;
static float smoothHeatbreakFan= 0;
static bool  smoothInited      = false;

static bool gaugesAnimating = false;       // true while arcs are interpolating
static const unsigned long GAUGE_ANIM_MS = 80; // ~12 Hz during animation

static const float SMOOTH_ALPHA = 0.09f;  // per frame at 12Hz — ~1s to settle
static const float SNAP_THRESH  = 0.5f;   // snap when within 0.5 of target

static void smoothLerp(float& cur, float target) {
  float diff = target - cur;
  if (fabsf(diff) < SNAP_THRESH) cur = target;
  else cur += diff * SMOOTH_ALPHA;
}

// Returns true if any gauge is still animating
static bool tickGaugeSmooth(const BambuState& s, bool snap) {
  if (snap || !smoothInited) {
    smoothNozzleTemp   = s.nozzleTemp;
    smoothBedTemp      = s.bedTemp;
    smoothPartFan      = s.coolingFanPct;
    smoothAuxFan       = s.auxFanPct;
    smoothChamberFan   = s.chamberFanPct;
    smoothChamberTemp  = s.chamberTemp;
    smoothHeatbreakFan = s.heatbreakFanPct;
    smoothInited = true;
    return false;
  }
  smoothLerp(smoothNozzleTemp,   s.nozzleTemp);
  smoothLerp(smoothBedTemp,      s.bedTemp);
  smoothLerp(smoothPartFan,      (float)s.coolingFanPct);
  smoothLerp(smoothAuxFan,       (float)s.auxFanPct);
  smoothLerp(smoothChamberFan,   (float)s.chamberFanPct);
  smoothLerp(smoothChamberTemp,  s.chamberTemp);
  smoothLerp(smoothHeatbreakFan, (float)s.heatbreakFanPct);

  const float ANIM_EPS = 0.01f;
  return (fabsf(smoothNozzleTemp   - s.nozzleTemp)              > ANIM_EPS) ||
         (fabsf(smoothBedTemp      - s.bedTemp)                 > ANIM_EPS) ||
         (fabsf(smoothPartFan      - (float)s.coolingFanPct)    > ANIM_EPS) ||
         (fabsf(smoothAuxFan       - (float)s.auxFanPct)        > ANIM_EPS) ||
         (fabsf(smoothChamberFan   - (float)s.chamberFanPct)    > ANIM_EPS) ||
         (fabsf(smoothChamberTemp  - s.chamberTemp)             > ANIM_EPS) ||
         (fabsf(smoothHeatbreakFan - (float)s.heatbreakFanPct)  > ANIM_EPS);
}

// ---------------------------------------------------------------------------
//  Backlight
// ---------------------------------------------------------------------------
static uint8_t lastAppliedBrightness = 0;

void setBacklight(uint8_t level) {
#if defined(BACKLIGHT_PIN) && BACKLIGHT_PIN >= 0
  analogWrite(BACKLIGHT_PIN, level);
#endif
  lastAppliedBrightness = level;
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initDisplay() {
  Serial.println("Display: pre-init delay...");
  delay(500);
  Serial.println("Display: calling _tft_instance.init()...");
  _tft_instance.init();  // LovyanGFX configures SPI from the board class above
#if defined(BOARD_IS_S3) || defined(BOARD_IS_C3)
  _tft_instance.invertDisplay(true);  // ST7789 requires color inversion
#endif
  Serial.println("Display: tft.init() done");
#if defined(DISPLAY_240x320) && !defined(DISPLAY_CYD)
  // Clear entire GRAM at rotation 0 first (guarantees all 240x320 pixels
  // are addressed). Without this, rotations 1/3 leave 80px of uninitialized
  // VRAM visible as garbage noise on the extra screen edge.
  // Skipped for CYD — LovyanGFX Panel_ILI9341 handles GRAM differently.
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
  tft.setRotation(dispSettings.rotation);
#if defined(DISPLAY_240x320)
  if (dispSettings.invertColors) _tft_instance.invertDisplay(false);
#endif
  Serial.println("Display: setRotation done");
  tft.fillScreen(CLR_BG);
  Serial.println("Display: fillScreen done");

#if defined(TOUCH_CS) && !defined(USE_XPT2046)
  // LovyanGFX touch calibration
  uint16_t calData[8] = {0, 0, 0, 65535, 0, 65535, 65535, 65535};
  tft.setTouchCalibrate(calData);
  Serial.println("Display: touch calibration set");
#endif

#if defined(BACKLIGHT_PIN) && BACKLIGHT_PIN >= 0
  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(200);
#endif

  memset(&prevState, 0, sizeof(prevState));

  // Splash screen
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("BambuHelper", SCREEN_W / 2, SCREEN_H / 2 - 20);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Printer Monitor", SCREEN_W / 2, SCREEN_H / 2 + 10);
  tft.setTextFont(1);
  tft.drawString(FW_VERSION, SCREEN_W / 2, SCREEN_H / 2 + 30);
}

void applyDisplaySettings() {
#if defined(DISPLAY_240x320)
  // Pre-clear entire GRAM at rotation 0 to prevent garbage on edges
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
  tft.setRotation(dispSettings.rotation);
#if defined(DISPLAY_240x320)
  _tft_instance.invertDisplay(dispSettings.invertColors ? false : true);
#endif
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
  lastDisplayUpdate = 0;  // bypass throttle so redraw is immediate after fillScreen
  // Reset clock/pong so they redraw fully after fillScreen cleared everything
  if (currentScreen == SCREEN_CLOCK) {
    if (dispSettings.pongClock) resetPongClock();
    else resetClock();
  }
}

void triggerDisplayTransition() {
  // Clear previous state so everything redraws for the new printer
  memset(&prevState, 0, sizeof(prevState));
  smoothInited = false;  // snap gauges to new printer's values
  resetGaugeTextCache();
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
}

void setScreenState(ScreenState state) {
  currentScreen = state;
}

ScreenState getScreenState() {
  return currentScreen;
}

// ---------------------------------------------------------------------------
//  Nozzle label helper (dual nozzle H2D/H2C)
// ---------------------------------------------------------------------------
static const char* nozzleLabel(const BambuState& s) {
  if (!s.dualNozzle) return "Nozzle";
  return s.activeNozzle == 0 ? "Nozzle R" : "Nozzle L";
}

// ---------------------------------------------------------------------------
//  Speed level name helper
// ---------------------------------------------------------------------------
static const char* speedLevelName(uint8_t level) {
  switch (level) {
    case 1: return "Silent";
    case 2: return "Std";
    case 3: return "Sport";
    case 4: return "Ludicr";
    default: return "---";
  }
}

static uint16_t speedLevelColor(uint8_t level) {
  switch (level) {
    case 1: return CLR_BLUE;
    case 2: return CLR_GREEN;
    case 3: return CLR_ORANGE;
    case 4: return CLR_RED;
    default: return CLR_TEXT_DIM;
  }
}

// ---------------------------------------------------------------------------
//  Screen: AP Mode
// ---------------------------------------------------------------------------
static void drawAPMode() {
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Setup", SCREEN_W / 2, LY_AP_TITLE_Y);

  // Instructions
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connect to WiFi:", SCREEN_W / 2, LY_AP_SSID_LBL_Y);

  // AP SSID
  tft.setTextColor(CLR_CYAN, CLR_BG);
  tft.setTextFont(4);
  char ssid[32];
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_PREFIX, mac);
  tft.drawString(ssid, SCREEN_W / 2, LY_AP_SSID_Y);

  // Password
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Password:", SCREEN_W / 2, LY_AP_PASS_LBL_Y);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(WIFI_AP_PASSWORD, SCREEN_W / 2, LY_AP_PASS_Y);

  // IP
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Then open:", SCREEN_W / 2, LY_AP_OPEN_Y);
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("192.168.4.1", SCREEN_W / 2, LY_AP_IP_Y);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting WiFi
// ---------------------------------------------------------------------------
static void drawConnectingWiFi() {
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to WiFi", SCREEN_W / 2, SCREEN_H / 2 - 20);

  int16_t tw = tft.textWidth("Connecting to WiFi");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 - 26, CLR_TEXT);

  // Slide bar
  const int16_t barW = 180;
  const int16_t barH = 8;
  drawSlideBar(tft, (SCREEN_W - barW) / 2, SCREEN_H / 2 + 4,
               barW, barH, CLR_BLUE, CLR_TRACK);
}

// ---------------------------------------------------------------------------
//  Screen: WiFi Connected (show IP)
// ---------------------------------------------------------------------------
static void drawWiFiConnected() {
  if (!forceRedraw) return;

  tft.setTextDatum(MC_DATUM);

  // Checkmark circle with tick
  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 - 40;
  tft.fillCircle(cx, cy, 25, CLR_GREEN);
  // Draw thick tick mark (3px wide)
  for (int i = -1; i <= 1; i++) {
    tft.drawLine(cx - 12, cy + i,     cx - 4, cy + 8 + i, CLR_BG);  // short leg
    tft.drawLine(cx - 4,  cy + 8 + i, cx + 12, cy - 6 + i, CLR_BG); // long leg
  }

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Connected", SCREEN_W / 2, SCREEN_H / 2 + 10);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, SCREEN_H / 2 + 40);
}

// ---------------------------------------------------------------------------
//  Screen: OTA firmware update in progress
// ---------------------------------------------------------------------------
#include "web_server.h"
static void drawOtaUpdate() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT, CLR_BG);

  // Title
  tft.setTextFont(4);
  tft.drawString("Updating", SCREEN_W / 2, SCREEN_H / 2 - 60);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("BambuHelper firmware", SCREEN_W / 2, SCREEN_H / 2 - 36);

  // Progress bar
  int pct = getOtaAutoProgress();
  const int16_t barX = 20, barY = SCREEN_H / 2 - 10;
  const int16_t barW = SCREEN_W - 40, barH = 14;
  tft.fillRoundRect(barX, barY, barW, barH, 4, CLR_TRACK);
  if (pct > 0) {
    int16_t fill = (int16_t)((pct / 100.0f) * barW);
    tft.fillRoundRect(barX, barY, fill, barH, 4, CLR_GREEN);
  }

  // Percentage
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(pctBuf, SCREEN_W / 2, SCREEN_H / 2 + 14);

  // Status
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString(getOtaAutoStatus(), SCREEN_W / 2, SCREEN_H / 2 + 34);

  // Warning
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.drawString("Do not power off", SCREEN_W / 2, SCREEN_H / 2 + 58);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting MQTT
// ---------------------------------------------------------------------------
static void drawConnectingMQTT() {
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to Printer", SCREEN_W / 2, SCREEN_H / 2 - 40);

  int16_t tw = tft.textWidth("Connecting to Printer");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 - 46, CLR_TEXT);
  tft.setTextDatum(MC_DATUM);

  // Slide bar
  const int16_t barW = 180;
  const int16_t barH = 8;
  drawSlideBar(tft, (SCREEN_W - barW) / 2, SCREEN_H / 2 - 14,
               barW, barH, CLR_ORANGE, CLR_TRACK);

  // Connection mode + printer info
  PrinterSlot& p = displayedPrinter();
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setTextFont(2);

  const char* modeStr = isCloudMode(p.config.mode) ? "Cloud" : "LAN";
  char infoBuf[40];
  if (isCloudMode(p.config.mode)) {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s", modeStr,
             strlen(p.config.serial) > 0 ? p.config.serial : "no serial!");
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s",  modeStr,
             strlen(p.config.ip) > 0 ? p.config.ip : "no IP!");
  }
  tft.drawString(infoBuf, SCREEN_W / 2, SCREEN_H / 2 + 10);

  // Elapsed time
  if (connectScreenStart > 0) {
    unsigned long elapsed = (millis() - connectScreenStart) / 1000;
    char elBuf[16];
    snprintf(elBuf, sizeof(elBuf), "%lus", elapsed);
    tft.fillRect(SCREEN_W / 2 - 30, SCREEN_H / 2 + 22, 60, 16, CLR_BG);
    tft.drawString(elBuf, SCREEN_W / 2, SCREEN_H / 2 + 30);
  }

  // Diagnostics (only after first attempt)
  const MqttDiag& d = getMqttDiag(rotState.displayIndex);
  if (d.attempts > 0) {
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);

    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt: %u", d.attempts);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 50);

    if (d.lastRc != 0) {
      snprintf(buf, sizeof(buf), "Err: %s", mqttRcToString(d.lastRc));
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 62);
    }
  }
}

// Forward declaration (defined after CYD section)
static void drawWifiSignalIndicator(const BambuState& s, int16_t wifiY);

// ---------------------------------------------------------------------------
//  Screen: Idle (connected, not printing)
// ---------------------------------------------------------------------------
static void drawIdleNoPrinter() {
  if (!forceRedraw) return;

  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("BambuHelper", SCREEN_W / 2, LY_IDLE_NP_TITLE_Y);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("WiFi Connected", SCREEN_W / 2, LY_IDLE_NP_WIFI_Y);

  tft.fillCircle(SCREEN_W / 2, LY_IDLE_NP_DOT_Y, 5, CLR_GREEN);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("No printer configured", SCREEN_W / 2, LY_IDLE_NP_MSG_Y);
  tft.drawString("Open in browser:", SCREEN_W / 2, LY_IDLE_NP_OPEN_Y);

  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, LY_IDLE_NP_IP_Y);
}

// ---------------------------------------------------------------------------
//  Screen: Idle Drying (AMS drying active while printer idle)
//  Shows ONE drying AMS at a time. Rotates between drying units every 60s.
//  Layout: progress bar, header, large temp, time remaining, humidity, ETA.
// ---------------------------------------------------------------------------
static bool wasDrying = false;
static uint8_t dryDisplayIdx = 0;           // which drying unit we're showing
static unsigned long dryRotateMs = 0;       // last rotation timestamp
static const unsigned long DRY_ROTATE_MS = 60000;  // 60s rotation interval

static uint16_t humidityColor(uint8_t level) {
  if (level <= 2) return CLR_GREEN;
  if (level == 3) return CLR_YELLOW;
  if (level == 4) return CLR_ORANGE;
  return CLR_RED;
}

// Find the N-th actively drying unit (or first if idx out of range)
static int8_t findDryingUnit(AmsState& ams, uint8_t idx) {
  uint8_t found = 0;
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++) {
    if (ams.units[i].dryRemainMin > 0) {
      if (found == idx) return i;
      found++;
    }
  }
  // Wrap around: return first drying unit
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++) {
    if (ams.units[i].dryRemainMin > 0) return i;
  }
  return -1;
}

static uint8_t countDryingUnits(AmsState& ams) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < ams.unitCount && i < AMS_MAX_UNITS; i++)
    if (ams.units[i].dryRemainMin > 0) n++;
  return n;
}

static void drawIdleDrying(PrinterSlot& p) {
  BambuState& s = p.state;
  const int16_t cx = SCREEN_W / 2;

  // Count drying units and handle rotation
  uint8_t dryCount = countDryingUnits(s.ams);
  if (dryCount > 1 && millis() - dryRotateMs >= DRY_ROTATE_MS) {
    dryDisplayIdx = (dryDisplayIdx + 1) % dryCount;
    dryRotateMs = millis();
    forceRedraw = true;
    tft.fillScreen(CLR_BG);
    resetGaugeTextCache();
  }
  if (dryCount <= 1) dryDisplayIdx = 0;

  int8_t ui = findDryingUnit(s.ams, dryDisplayIdx);
  if (ui < 0) return;  // no drying unit found (shouldn't happen)
  AmsUnit& u = s.ams.units[ui];

  // Change detection
  static uint16_t prevDryMin = 0;
  static uint8_t  prevHumidity = 0xFF;
  static float    prevTemp = -999;
  static uint8_t  prevHumRaw = 0xFF;

  bool dataChanged = forceRedraw ||
                     u.dryRemainMin != prevDryMin ||
                     u.humidity != prevHumidity ||
                     u.humidityRaw != prevHumRaw ||
                     (int)(u.temp * 10) != (int)(prevTemp * 10);
  prevDryMin = u.dryRemainMin;
  prevHumidity = u.humidity;
  prevHumRaw = u.humidityRaw;
  prevTemp = u.temp;

  // === Progress bar (top, y=0-5) ===
  uint8_t dryProgress = 0;
  if (u.dryTotalMin > 0 && u.dryRemainMin <= u.dryTotalMin)
    dryProgress = 100 - (uint8_t)((uint32_t)u.dryRemainMin * 100 / u.dryTotalMin);
  if (dataChanged) {
    drawLedProgressBar(tft, 0, dryProgress);
  }

  // === Header bar ===
  if (forceRedraw) {
    tft.fillRect(0, LY_HDR_Y, SCREEN_W, LY_HDR_H, CLR_BG);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu";
    tft.drawString(name, LY_HDR_NAME_X, LY_HDR_CY);

    // "DRYING" badge (right, orange)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    const char* badge = "DRYING";
    tft.fillCircle(SCREEN_W - LY_HDR_BADGE_RX - tft.textWidth(badge) - 10, LY_HDR_CY, 4, CLR_ORANGE);
    tft.drawString(badge, SCREEN_W - LY_HDR_BADGE_RX, LY_HDR_CY);

    // Multi-printer dots
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(cx - 5 + di * 10, LY_HDR_DOT_CY, 3, dotClr);
      }
    }
  }

  if (!dataChanged) return;

  // === AMS unit name (below header) ===
  {
    bool isHT = (u.id >= 128);
    uint8_t displayNum = isHT ? (u.id - 128 + 1) : (u.id + 1);
    const char* prefix = isHT ? "AMS HT" : "AMS";
    char unitName[24];
    if (dryCount > 1)
      snprintf(unitName, sizeof(unitName), "%s %d  (%d/%d)", prefix, displayNum, dryDisplayIdx + 1, dryCount);
    else
      snprintf(unitName, sizeof(unitName), "%s %d", prefix, displayNum);

    tft.fillRect(0, 30, SCREEN_W, 20, CLR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    tft.drawString(unitName, cx, 40);
  }

#if defined(DISPLAY_240x320)
  // === 240x320: Centered large temp + remaining + humidity ===
  // Vertically centered between unit name (y~50) and ETA (y=260)
  {
    tft.fillRect(0, 55, SCREEN_W, 75, CLR_BG);

    char tempBuf[14];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f", u.temp);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(7);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    int16_t tempW = tft.textWidth(tempBuf);
    tft.drawString(tempBuf, cx - 10, 100);

    tft.setTextFont(4);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("\xB0""C", cx - 10 + tempW / 2 + 2, 86);
  }

  // === Remaining time ===
  {
    const int16_t timeY = 160;
    tft.fillRect(0, timeY - 14, SCREEN_W, 30, CLR_BG);
    char timeBuf[20];
    uint16_t h = u.dryRemainMin / 60;
    uint16_t m = u.dryRemainMin % 60;
    if (h > 0)
      snprintf(timeBuf, sizeof(timeBuf), "%dh %02dm remaining", h, m);
    else
      snprintf(timeBuf, sizeof(timeBuf), "%dm remaining", m);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(CLR_YELLOW, CLR_BG);
    tft.drawString(timeBuf, cx, timeY);
  }

  // === Humidity ===
  {
    const int16_t humY = 200;
    tft.fillRect(0, humY - 14, SCREEN_W, 30, CLR_BG);
    char humBuf[24];
    snprintf(humBuf, sizeof(humBuf), "Humidity: %d%%", u.humidityRaw);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(humidityColor(u.humidity), CLR_BG);
    tft.drawString(humBuf, cx, humY);
  }
#else
  // === 240x240: Large temperature display (center) ===
  {
    tft.fillRect(0, 55, SCREEN_W, 65, CLR_BG);

    char tempBuf[14];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f", u.temp);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(7);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    int16_t tempW = tft.textWidth(tempBuf);
    tft.drawString(tempBuf, cx - 10, 82);

    tft.setTextFont(4);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("\xB0""C", cx - 10 + tempW / 2 + 2, 68);
  }

  // === Remaining time ===
  {
    tft.fillRect(0, 125, SCREEN_W, 30, CLR_BG);
    char timeBuf[20];
    uint16_t h = u.dryRemainMin / 60;
    uint16_t m = u.dryRemainMin % 60;
    if (h > 0)
      snprintf(timeBuf, sizeof(timeBuf), "%dh %02dm remaining", h, m);
    else
      snprintf(timeBuf, sizeof(timeBuf), "%dm remaining", m);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_YELLOW, CLR_BG);
    tft.drawString(timeBuf, cx, 140);
  }

  // === Humidity ===
  {
    tft.fillRect(0, 158, SCREEN_W, 25, CLR_BG);
    char humBuf[24];
    snprintf(humBuf, sizeof(humBuf), "Humidity: %d%%", u.humidityRaw);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(humidityColor(u.humidity), CLR_BG);
    tft.drawString(humBuf, cx, 170);
  }
#endif

  // === ETA ===
  {
    tft.fillRect(0, LY_ETA_Y, SCREEN_W, LY_ETA_H, CLR_BG);
    tft.setTextDatum(MC_DATUM);

    time_t nowEpoch = time(nullptr);
    struct tm now;
    localtime_r(&nowEpoch, &now);
    bool ntpOk = (now.tm_year > (2020 - 1900));

    char etaBuf[32];
    if (ntpOk && u.dryRemainMin > 0) {
      time_t etaEpoch = nowEpoch + (time_t)u.dryRemainMin * 60;
      struct tm etaTm;
      localtime_r(&etaEpoch, &etaTm);
      int etaH = etaTm.tm_hour;
      const char* ampm = "";
      if (!netSettings.use24h) {
        ampm = etaH < 12 ? "AM" : "PM";
        etaH = etaH % 12;
        if (etaH == 0) etaH = 12;
      }
      if (etaTm.tm_yday != now.tm_yday || etaTm.tm_year != now.tm_year) {
        if (netSettings.use24h)
          snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d.%02d. %02d:%02d",
                   etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min);
        else
          snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d/%02d %d:%02d%s",
                   etaTm.tm_mon + 1, etaTm.tm_mday, etaH, etaTm.tm_min, ampm);
      } else {
        if (netSettings.use24h)
          snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d:%02d", etaH, etaTm.tm_min);
        else
          snprintf(etaBuf, sizeof(etaBuf), "ETA: %d:%02d %s", etaH, etaTm.tm_min, ampm);
      }
    } else if (u.dryRemainMin > 0) {
      uint16_t h = u.dryRemainMin / 60;
      uint16_t m = u.dryRemainMin % 60;
      snprintf(etaBuf, sizeof(etaBuf), "ETA: %dh %02dm", h, m);
    } else {
      snprintf(etaBuf, sizeof(etaBuf), "---");
    }
    tft.setTextFont(4);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.drawString(etaBuf, cx, LY_ETA_TEXT_Y);
  }

  // === Bottom bar — connected indicator ===
  {
    bool connChanged = forceRedraw || (s.connected != prevState.connected);
    if (connChanged) {
      tft.fillRect(0, LY_BOT_Y, SCREEN_W, LY_BOT_H, CLR_BG);
      tft.fillCircle(cx, LY_BOT_CY, 4, s.connected ? CLR_GREEN : CLR_RED);
    }
  }
}

static bool wasNoPrinter = false;

// Forward declarations for 240x320 functions used before their definition
#if defined(DISPLAY_240x320)
static bool isLandscape();
static void drawAmsStrip(const AmsState& ams, int16_t zoneY, int16_t zoneH, int16_t barH);
#endif

static void drawIdle() {
  if (!isAnyPrinterConfigured()) {
    wasNoPrinter = true;
    drawIdleNoPrinter();
    return;
  }

  // Transition from "no printer" to configured — clear stale screen
  if (wasNoPrinter) {
    wasNoPrinter = false;
    tft.fillScreen(dispSettings.bgColor);
    memset(&prevState, 0, sizeof(prevState));
    forceRedraw = true;
  }

  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  // AMS drying active — switch to dedicated drying layout
  // Grace period: stay on drying screen for 5s after anyDrying drops,
  // to avoid flashing back to idle during brief state transitions (PREPARE etc.)
  static unsigned long dryingDropMs = 0;
  if (s.ams.anyDrying) {
    dryingDropMs = 0;
    if (!wasDrying) {
      wasDrying = true;
      tft.fillScreen(dispSettings.bgColor);
      forceRedraw = true;
    }
    drawIdleDrying(p);
    return;
  }
  if (wasDrying) {
    if (dryingDropMs == 0) dryingDropMs = millis();
    if (millis() - dryingDropMs < 5000) {
      drawIdleDrying(p);  // keep showing drying screen during grace
      return;
    }
    wasDrying = false;
    dryingDropMs = 0;
    tft.fillScreen(dispSettings.bgColor);
    memset(&prevState, 0, sizeof(prevState));
    forceRedraw = true;
  }

  // Effective screen dimensions — idle uses full screen (no AMS sidebar)
#if defined(DISPLAY_240x320)
  const int16_t scrW = (int16_t)tft.width();
  const int16_t scrH = (int16_t)tft.height();
#else
  const int16_t scrW = SCREEN_W;
  const int16_t scrH = SCREEN_H;
#endif
  const int16_t cx = scrW / 2;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  gaugesAnimating = animating;
  bool stateChanged = forceRedraw ||
                      (s.gcodeStateId != prevState.gcodeStateId) ||
                      (strcmp(s.gcodeState, prevState.gcodeState) != 0);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);
  bool connChanged = forceRedraw || (s.connected != prevState.connected);
  bool wifiChanged = forceRedraw || (s.wifiSignal != prevState.wifiSignal);

  tft.setTextDatum(MC_DATUM);

  // Printer name (only on forceRedraw — name doesn't change)
  if (forceRedraw) {
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setTextFont(4);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, cx, LY_IDLE_NAME_Y);
  }

  // Status badge — only redraw when state changes
  if (stateChanged) {
    tft.setTextFont(2);
    uint16_t stateColor = CLR_TEXT_DIM;
    const char* stateStr = s.gcodeState;
    if (s.gcodeStateId == GCODE_IDLE) {
      stateColor = CLR_GREEN;
      stateStr = "Ready";
    } else if (s.gcodeStateId == GCODE_FAILED) {
      stateColor = CLR_RED;
      stateStr = "ERROR";
    } else if (s.gcodeStateId == GCODE_UNKNOWN) {
      stateStr = "Waiting...";
    }
    tft.fillRect(0, LY_IDLE_STATE_Y, scrW, LY_IDLE_STATE_H, CLR_BG);
    tft.setTextColor(stateColor, CLR_BG);
    tft.drawString(stateStr, cx, LY_IDLE_STATE_TY);
  }

  // Connected indicator
  if (connChanged) {
    tft.fillCircle(cx, LY_IDLE_DOT_Y, 5, s.connected ? CLR_GREEN : CLR_RED);
  }

  // "Press to refresh" hint for cloud printers stuck in UNKNOWN state
  {
    static unsigned long unknownSinceMs = 0;
    static bool hintShown = false;
    bool isUnknown = (s.gcodeStateId == GCODE_UNKNOWN);
    if (isUnknown && unknownSinceMs == 0) unknownSinceMs = millis();
    if (!isUnknown) unknownSinceMs = 0;
    bool showHint = isUnknown && unknownSinceMs > 0 &&
                    millis() - unknownSinceMs > 60000 &&
                    buttonType != BTN_DISABLED &&
                    isCloudMode(p.config.mode) && s.connected;
    if (stateChanged || showHint != hintShown) {
      const int16_t hintY = LY_IDLE_DOT_Y + 15;
      tft.fillRect(0, hintY - 6, scrW, 14, CLR_BG);
      if (showHint) {
        tft.setTextFont(1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(CLR_TEXT_DARK, CLR_BG);
        tft.drawString("Press to refresh", cx, hintY);
      }
      hintShown = showHint;
    }
  }

  // Nozzle temp gauge
  if (tempChanged) {
    drawTempGauge(tft, cx - LY_IDLE_G_OFFSET, LY_IDLE_GAUGE_Y, LY_IDLE_GAUGE_R,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    // Bed temp gauge
    drawTempGauge(tft, cx + LY_IDLE_G_OFFSET, LY_IDLE_GAUGE_Y, LY_IDLE_GAUGE_R,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // AMS strip on idle (portrait 240x320 only)
#if defined(DISPLAY_240x320)
  if (s.ams.present && s.ams.unitCount > 0 && !isLandscape()) {
    static uint8_t  prevIdleAmsCount = 0;
    static uint8_t  prevIdleAmsActive = 255;
    static uint16_t prevIdleAmsColors[AMS_MAX_TRAYS] = {0};
    static bool     prevIdleAmsPresent[AMS_MAX_TRAYS] = {false};
    static int8_t   prevIdleAmsRemain[AMS_MAX_TRAYS];

    bool amsChanged = forceRedraw ||
                      (s.ams.unitCount != prevIdleAmsCount) ||
                      (s.ams.activeTray != prevIdleAmsActive);
    if (!amsChanged) {
      for (uint8_t i = 0; i < s.ams.unitCount * AMS_TRAYS_PER_UNIT && !amsChanged; i++) {
        amsChanged = (s.ams.trays[i].present != prevIdleAmsPresent[i]) ||
                     (s.ams.trays[i].colorRgb565 != prevIdleAmsColors[i]) ||
                     (s.ams.trays[i].remain != prevIdleAmsRemain[i]);
      }
    }
    if (amsChanged) {
      prevIdleAmsCount = s.ams.unitCount;
      prevIdleAmsActive = s.ams.activeTray;
      for (uint8_t i = 0; i < AMS_MAX_TRAYS; i++) {
        prevIdleAmsPresent[i] = s.ams.trays[i].present;
        prevIdleAmsColors[i]  = s.ams.trays[i].colorRgb565;
        prevIdleAmsRemain[i]  = s.ams.trays[i].remain;
      }
      drawAmsStrip(s.ams, LY_IDLE_AMS_Y, LY_IDLE_AMS_H, LY_IDLE_AMS_BAR_H);
    }
  }
#endif

  // Bottom status bar: Filament/WiFi | Power | Door
  static bool     idlePrevTasmotaOnline = false;
  static float    idlePrevWatts        = -2.0f;

  bool idleTasmotaOnline = tasmotaIsActiveForSlot(rotState.displayIndex);
  float idleCurWatts = tasmotaGetWatts();

  int16_t botCY = scrH - 9;
  bool bottomChanged = wifiChanged ||
                       (s.ams.activeTray != prevState.ams.activeTray) ||
                       (s.doorOpen != prevState.doorOpen) ||
                       (s.doorSensorPresent != prevState.doorSensorPresent) ||
                       (tasmotaSettings.enabled && (idleTasmotaOnline != idlePrevTasmotaOnline ||
                                                    idleCurWatts != idlePrevWatts));
  idlePrevTasmotaOnline  = idleTasmotaOnline;
  idlePrevWatts          = idleCurWatts;

  if (bottomChanged) {
    tft.fillRect(0, scrH - 18, scrW, 18, CLR_BG);
    tft.setTextFont(2);

    // Left: filament circle (if AMS active) or WiFi signal
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      tft.drawCircle(10, botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10, botCY, 4, t.colorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(t.type, 19, botCY);
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      tft.drawCircle(10, botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10, botCY, 4, s.ams.vtColorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(s.ams.vtType, 19, botCY);
    } else {
      drawWifiSignalIndicator(s, botCY);
    }

    // Center: power watts (if Tasmota online)
    // Ready screen has no layer count, so always show power (no alternation)
    bool showPower = idleTasmotaOnline;
    if (showPower) {
      drawIcon16(tft, cx - 20, botCY - 8, icon_lightning, CLR_YELLOW);
      char wBuf[8];
      snprintf(wBuf, sizeof(wBuf), "%.0fW", tasmotaGetWatts());
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(wBuf, cx - 2, botCY);
    }

    // Right: door status (if sensor present)
    if (s.doorSensorPresent) {
      uint16_t clr = s.doorOpen ? CLR_ORANGE : CLR_GREEN;
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(clr, CLR_BG);
      tft.drawString("Door", scrW - 20, botCY);
      drawIcon16(tft, scrW - 18, botCY - 8,
                 s.doorOpen ? icon_unlock : icon_lock, clr);
    }
  }
}

// ---------------------------------------------------------------------------
//  AMS tray visualization (CYD only)
//  Portrait: horizontal strip between gauges and ETA (y=190-246)
//  Landscape: vertical strip on right side (x=244-316)
// ---------------------------------------------------------------------------
#if defined(DISPLAY_240x320)

static bool isLandscape() {
  return (dispSettings.rotation == 1 || dispSettings.rotation == 3);
}

static uint8_t  prevAmsUnitCount = 0;
static uint8_t  prevAmsActive    = 255;
static uint16_t prevAmsTrayColors[AMS_MAX_TRAYS] = {0};
static bool     prevAmsTrayPresent[AMS_MAX_TRAYS] = {false};
static int8_t   prevAmsTrayRemain[AMS_MAX_TRAYS];  // init in drawAmsZone

// Helper: draw a single AMS tray bar with optional partial fill for remain%.
// remain 0-99: color fills bottom portion, CLR_TRACK fills the rest.
// remain 100 or -1 (unknown): full color as before.
static void drawAmsTrayBar(int16_t x, int16_t y, int16_t w, int16_t h,
                           const AmsTray& tray, bool isActive) {
  if (tray.present) {
    int16_t border = isActive ? 2 : 1;
    uint16_t borderClr = isActive ? TFT_WHITE : CLR_TEXT_DARK;

    // Outer border
    if (isActive)
      tft.fillRect(x, y, w, h, borderClr);
    else
      tft.drawRect(x, y, w, h, borderClr);

    // Inner fill with optional partial remain%
    int16_t ix = x + border, iy = y + border;
    int16_t iw = w - 2 * border, ih = h - 2 * border;
    bool partialFill = (tray.remain >= 0 && tray.remain < 100);

    if (partialFill) {
      int16_t fillH = (int16_t)((int32_t)ih * tray.remain / 100);
      int16_t emptyH = ih - fillH;
      if (emptyH > 0) tft.fillRect(ix, iy, iw, emptyH, CLR_TRACK);
      if (fillH > 0)  tft.fillRect(ix, iy + emptyH, iw, fillH, tray.colorRgb565);
    } else {
      tft.fillRect(ix, iy, iw, ih, tray.colorRgb565);
    }

    // Active slot marker triangle
    if (isActive) {
      tft.fillTriangle(x, y, x + w / 2, y + 8, x + w, y, CLR_BG);
      tft.fillTriangle(x + 2, y + 2, x + w / 2, y + 6, x + w - 2, y + 2, TFT_RED);
    }
  } else {
    // Empty slot: outline + diagonal cross to distinguish from black filament
    tft.drawRect(x, y, w, h, CLR_TEXT_DARK);
    tft.drawLine(x, y, x + w - 1, y + h - 1, CLR_TEXT_DARK);
    tft.drawLine(x + w - 1, y, x, y + h - 1, CLR_TEXT_DARK);
  }
}

// Portrait AMS strip: horizontal row of tray bars, usable from printing/idle/finished.
// Draws at (0, zoneY) full width, clears zoneH pixels, bars are barH tall.
// All groups get uniform width (based on AMS_TRAYS_PER_UNIT slots) so labels
// stay evenly spaced. Units with fewer trays (e.g. AMS HT = 1) center their
// bars within the full-width group.
static void drawAmsStrip(const AmsState& ams,
                         int16_t zoneY, int16_t zoneH, int16_t barH) {
  uint8_t units = ams.unitCount;
  tft.fillRect(0, zoneY, LY_W, zoneH, CLR_BG);
  if (units == 0 || units > AMS_MAX_UNITS) return;

  const int16_t usableW = LY_W - 2 * LY_AMS_MARGIN;

  // Uniform group width: every group sized for AMS_TRAYS_PER_UNIT bars
  int16_t barW = (usableW - (units - 1) * LY_AMS_GROUP_GAP
                  - units * (AMS_TRAYS_PER_UNIT - 1) * LY_AMS_BAR_GAP)
                 / (units * AMS_TRAYS_PER_UNIT);
  if (barW > LY_AMS_BAR_MAX_W) barW = LY_AMS_BAR_MAX_W;
  if (barW < 4) barW = 4;

  int16_t groupW = barW * AMS_TRAYS_PER_UNIT + (AMS_TRAYS_PER_UNIT - 1) * LY_AMS_BAR_GAP;
  int16_t totalW = groupW * units + (units - 1) * LY_AMS_GROUP_GAP;
  int16_t startX = (LY_W - totalW) / 2;

  int16_t barY = zoneY + (zoneH - barH - LY_AMS_LABEL_OFFY - 8) / 2;
  int16_t labelY = barY + barH + LY_AMS_LABEL_OFFY;

  for (uint8_t u = 0; u < units; u++) {
    int16_t groupX = startX + u * (groupW + LY_AMS_GROUP_GAP);

    uint8_t tc = ams.units[u].trayCount;
    if (tc == 0) tc = AMS_TRAYS_PER_UNIT;

    // Center actual bars within the uniform group slot
    int16_t barsW = tc * barW + (tc - 1) * LY_AMS_BAR_GAP;
    int16_t barsX = groupX + (groupW - barsW) / 2;

    for (uint8_t t = 0; t < tc; t++) {
      uint8_t trayIdx = u * AMS_TRAYS_PER_UNIT + t;
      int16_t bx = barsX + t * (barW + LY_AMS_BAR_GAP);
      drawAmsTrayBar(bx, barY, barW, barH,
                     ams.trays[trayIdx], trayIdx == ams.activeTray);
    }

    char label[6];
    snprintf(label, sizeof(label), "AMS %c", 'A' + u);
    tft.setTextDatum(TC_DATUM);
    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(label, groupX + groupW / 2, labelY + 2);
  }
}

static void drawAmsZone(const BambuState& s, bool force) {
  // --- Change detection ---
  bool changed = force;
  if (!changed) {
    changed = (s.ams.unitCount != prevAmsUnitCount) ||
              (s.ams.activeTray != prevAmsActive);
    if (!changed) {
      for (uint8_t i = 0; i < s.ams.unitCount * AMS_TRAYS_PER_UNIT && !changed; i++) {
        changed = (s.ams.trays[i].present != prevAmsTrayPresent[i]) ||
                  (s.ams.trays[i].colorRgb565 != prevAmsTrayColors[i]) ||
                  (s.ams.trays[i].remain != prevAmsTrayRemain[i]);
      }
    }
  }
  if (!changed) return;

  // Save state for next comparison
  prevAmsUnitCount = s.ams.unitCount;
  prevAmsActive    = s.ams.activeTray;
  for (uint8_t i = 0; i < AMS_MAX_TRAYS; i++) {
    prevAmsTrayPresent[i] = s.ams.trays[i].present;
    prevAmsTrayColors[i]  = s.ams.trays[i].colorRgb565;
    prevAmsTrayRemain[i]  = s.ams.trays[i].remain;
  }

  uint8_t units = s.ams.unitCount;
  bool landscape = isLandscape();

  if (landscape) {
    // =====================================================================
    //  LANDSCAPE: vertical strip on right side (x=244, 72px wide)
    //  AMS groups stacked vertically, each group has 4 VERTICAL bars
    //  side-by-side (same orientation as portrait / physical AMS)
    // =====================================================================
    tft.fillRect(LY_LAND_AMS_X - 4, LY_LAND_AMS_TOP, LY_LAND_AMS_W + 8,
                 LY_LAND_AMS_BOT - LY_LAND_AMS_TOP, CLR_BG);

    if (units == 0 || units > AMS_MAX_UNITS) return;

    const int16_t totalH = LY_LAND_AMS_BOT - LY_LAND_AMS_TOP;  // ~208px
    const int16_t groupGap = 6;
    const int16_t labelH = 12;  // space for AMS letter label below bars
    const int16_t barGap = 2;   // gap between bars

    // Find max tray count across units for bar width sizing
    uint8_t maxTC = 0;
    for (uint8_t u = 0; u < units; u++) {
      uint8_t tc = s.ams.units[u].trayCount;
      if (tc == 0) tc = AMS_TRAYS_PER_UNIT;
      if (tc > maxTC) maxTC = tc;
    }

    int16_t barW = (LY_LAND_AMS_W - (maxTC - 1) * barGap) / maxTC;
    if (barW > 16) barW = 16;
    if (barW < 4) barW = 4;

    // Calculate group height: bar height + label
    int16_t groupH = (totalH - (units - 1) * groupGap) / units;
    int16_t barH = groupH - labelH;
    if (barH > 50) barH = 50;
    if (barH < 10) barH = 10;

    // Vertical centering
    int16_t actualGroupH = barH + labelH;
    int16_t totalUsed = actualGroupH * units + (units - 1) * groupGap;
    int16_t startY = LY_LAND_AMS_TOP + (totalH - totalUsed) / 2;

    for (uint8_t u = 0; u < units; u++) {
      uint8_t tc = s.ams.units[u].trayCount;
      if (tc == 0) tc = AMS_TRAYS_PER_UNIT;

      int16_t actualGroupW = barW * tc + (tc - 1) * barGap;
      int16_t barsX = LY_LAND_AMS_X + (LY_LAND_AMS_W - actualGroupW) / 2;
      int16_t gy = startY + u * (actualGroupH + groupGap);

      for (uint8_t t = 0; t < tc; t++) {
        uint8_t trayIdx = u * AMS_TRAYS_PER_UNIT + t;
        int16_t bx = barsX + t * (barW + barGap);
        drawAmsTrayBar(bx, gy, barW, barH,
                       s.ams.trays[trayIdx], trayIdx == s.ams.activeTray);
      }

      // AMS label below bars
      char label[6];
      snprintf(label, sizeof(label), "AMS %c", 'A' + u);
      tft.setTextDatum(TC_DATUM);
      bool sm = dispSettings.smallLabels;
      tft.setTextFont(sm ? 1 : 2);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(label, LY_LAND_AMS_X + LY_LAND_AMS_W / 2, gy + barH + 2);
    }

  } else {
    drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H);
  }
}

#endif // DISPLAY_240x320

// ---------------------------------------------------------------------------
//  Helper: draw WiFi signal indicator in bottom-left corner
// ---------------------------------------------------------------------------
static void drawWifiSignalIndicator(const BambuState& s, int16_t wifiY = LY_WIFI_Y) {
  drawIcon16(tft, LY_WIFI_X, wifiY - 8, icon_wifi, CLR_TEXT_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  char wifiBuf[12];
  snprintf(wifiBuf, sizeof(wifiBuf), "%ddBm", s.wifiSignal);
  tft.drawString(wifiBuf, LY_WIFI_X + 18, wifiY);
}

// ---------------------------------------------------------------------------
//  Screen: Printing (main dashboard)
//  Layout: LED bar | header | 2x3 gauge grid | info line
// ---------------------------------------------------------------------------
static void drawPrinting() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  gaugesAnimating = animating;
  bool progChanged = forceRedraw || (s.progress != prevState.progress);
  bool etaChanged = forceRedraw ||
                     (s.remainingMinutes != prevState.remainingMinutes);
  bool stateChanged = forceRedraw ||
                      (s.gcodeStateId != prevState.gcodeStateId) ||
                      (strcmp(s.gcodeState, prevState.gcodeState) != 0);

  // 2x3 gauge grid constants (from layout profile)
  const int16_t gR = LY_GAUGE_R;
  const int16_t gT = LY_GAUGE_T;

  // Effective Y positions — landscape on CYD uses 240x240-style positions
#if defined(DISPLAY_240x320)
  const bool land = isLandscape();
  const int16_t eff_etaY     = land ? LY_LAND_ETA_Y     : LY_ETA_Y;
  const int16_t eff_etaH     = land ? LY_LAND_ETA_H     : LY_ETA_H;
  const int16_t eff_etaTextY = land ? LY_LAND_ETA_TEXT_Y : LY_ETA_TEXT_Y;
  const int16_t eff_botY     = land ? LY_LAND_BOT_Y     : LY_BOT_Y;
  const int16_t eff_botH     = land ? LY_LAND_BOT_H     : LY_BOT_H;
  const int16_t eff_botCY    = land ? LY_LAND_BOT_CY    : LY_BOT_CY;
#else
  const int16_t eff_etaY     = LY_ETA_Y;
  const int16_t eff_etaH     = LY_ETA_H;
  const int16_t eff_etaTextY = LY_ETA_TEXT_Y;
  const int16_t eff_botY     = LY_BOT_Y;
  const int16_t eff_botH     = LY_BOT_H;
  const int16_t eff_botCY    = LY_BOT_CY;
#endif

  // === CYD: clear unused zone on screen transitions ===
#if defined(DISPLAY_240x320)
  if (forceRedraw) {
    int16_t scrW = (int16_t)tft.width();
    int16_t scrH = (int16_t)tft.height();
    // Clear right edge if canvas wider than 240 (rotation 1/3)
    if (scrW > 240)
      tft.fillRect(240, 0, scrW - 240, scrH, CLR_BG);
    // Clear below content area if canvas taller than used
    int16_t usedBottom = eff_botY + eff_botH;
    if (usedBottom < scrH)
      tft.fillRect(0, usedBottom, scrW, scrH - usedBottom, CLR_BG);
  }
#endif

  // === H2-style LED progress bar (y=0-5) ===
  if (progChanged) {
    drawLedProgressBar(tft, 0, s.progress);
  }

  // === Header bar (y=7-25) ===
  if (forceRedraw || stateChanged) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, LY_HDR_Y, SCREEN_W, LY_HDR_H, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, LY_HDR_NAME_X, LY_HDR_CY);

    // State badge (right)
    uint16_t badgeColor = CLR_TEXT_DIM;
    if (s.gcodeStateId == GCODE_RUNNING) badgeColor = CLR_GREEN;
    else if (s.gcodeStateId == GCODE_PAUSE) badgeColor = CLR_YELLOW;
    else if (s.gcodeStateId == GCODE_FAILED) badgeColor = CLR_RED;
    else if (s.gcodeStateId == GCODE_PREPARE) badgeColor = CLR_BLUE;

    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(badgeColor, hdrBg);
    tft.setTextFont(2);
    tft.fillCircle(SCREEN_W - LY_HDR_BADGE_RX - tft.textWidth(s.gcodeState) - 10, LY_HDR_CY, 4, badgeColor);
    tft.drawString(s.gcodeState, SCREEN_W - LY_HDR_BADGE_RX, LY_HDR_CY);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(SCREEN_W / 2 - 5 + di * 10, LY_HDR_DOT_CY, 3, dotClr);
      }
    }
  }

  // === Configurable 2x3 gauge grid ===
  {
    static const int16_t slotX[GAUGE_SLOT_COUNT] = { LY_COL1, LY_COL2, LY_COL3, LY_COL1, LY_COL2, LY_COL3 };
    static const int16_t slotY[GAUGE_SLOT_COUNT] = { LY_ROW1, LY_ROW1, LY_ROW1, LY_ROW2, LY_ROW2, LY_ROW2 };
    static uint8_t prevSlotTypes[GAUGE_SLOT_COUNT] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    for (uint8_t si = 0; si < GAUGE_SLOT_COUNT; si++) {
      uint8_t gt = p.config.gaugeSlots[si];
      if (gt >= GAUGE_TYPE_COUNT) gt = GAUGE_EMPTY;

      bool typeChanged = (gt != prevSlotTypes[si]);
      if (typeChanged) {
        // Slot type changed (or first draw) - clear area and reset cache
        tft.fillCircle(slotX[si], slotY[si], gR + 2, dispSettings.bgColor);
        // Clear label area below gauge
        bool sm = dispSettings.smallLabels;
        tft.fillRect(slotX[si] - gR, slotY[si] + gR + (sm ? 1 : -3),
                     gR * 2, sm ? 12 : 16, dispSettings.bgColor);
        prevSlotTypes[si] = gt;
      }

      // Per-type change detection
      bool needDraw = forceRedraw || typeChanged;
      if (!needDraw) {
        switch (gt) {
          case GAUGE_PROGRESS:    needDraw = (s.progress != prevState.progress) || (s.remainingMinutes != prevState.remainingMinutes); break;
          case GAUGE_NOZZLE:      needDraw = animating || s.nozzleTemp != prevState.nozzleTemp || s.nozzleTarget != prevState.nozzleTarget; break;
          case GAUGE_BED:         needDraw = animating || s.bedTemp != prevState.bedTemp || s.bedTarget != prevState.bedTarget; break;
          case GAUGE_PART_FAN:    needDraw = animating || s.coolingFanPct != prevState.coolingFanPct; break;
          case GAUGE_AUX_FAN:     needDraw = animating || s.auxFanPct != prevState.auxFanPct; break;
          case GAUGE_CHAMBER_FAN: needDraw = animating || s.chamberFanPct != prevState.chamberFanPct; break;
          case GAUGE_CHAMBER_TEMP:needDraw = animating || s.chamberTemp != prevState.chamberTemp; break;
          case GAUGE_HEATBREAK:   needDraw = animating || s.heatbreakFanPct != prevState.heatbreakFanPct; break;
          case GAUGE_CLOCK:       needDraw = true; break;  // text cache handles actual redraw
          case GAUGE_LAYER:       needDraw = s.layerNum != prevState.layerNum || s.totalLayers != prevState.totalLayers; break;
          default:
            // AMS humidity / temperature gauges — index derived from enum value
            if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
              uint8_t ui = gt - GAUGE_AMS_HUM_1;
              const AmsUnit &cu = s.ams.units[ui], &pu = prevState.ams.units[ui];
              needDraw = cu.humidityRaw != pu.humidityRaw || cu.humidity != pu.humidity || cu.present != pu.present;
            } else if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
              uint8_t ui = gt - GAUGE_AMS_TEMP_1;
              const AmsUnit &cu = s.ams.units[ui], &pu = prevState.ams.units[ui];
              needDraw = cu.temp != pu.temp || cu.present != pu.present;
            }
            break;
        }
      }
      if (!needDraw) continue;

      int16_t cx = slotX[si], cy = slotY[si];
      bool fr = forceRedraw || typeChanged;

      switch (gt) {
        case GAUGE_PROGRESS:
          drawProgressArc(tft, cx, cy, gR, gT, s.progress, prevState.progress, s.remainingMinutes, fr);
          break;
        case GAUGE_NOZZLE:
          drawTempGauge(tft, cx, cy, gR, s.nozzleTemp, s.nozzleTarget, 300.0f,
                        dispSettings.nozzle.arc, nozzleLabel(s), nullptr, fr,
                        &dispSettings.nozzle, smoothNozzleTemp);
          break;
        case GAUGE_BED:
          drawTempGauge(tft, cx, cy, gR, s.bedTemp, s.bedTarget, 120.0f,
                        dispSettings.bed.arc, "Bed", nullptr, fr,
                        &dispSettings.bed, smoothBedTemp);
          break;
        case GAUGE_PART_FAN:
          drawFanGauge(tft, cx, cy, gR, s.coolingFanPct, dispSettings.partFan.arc, "Part", fr,
                       &dispSettings.partFan, smoothPartFan);
          break;
        case GAUGE_AUX_FAN:
          drawFanGauge(tft, cx, cy, gR, s.auxFanPct, dispSettings.auxFan.arc, "Aux", fr,
                       &dispSettings.auxFan, smoothAuxFan);
          break;
        case GAUGE_CHAMBER_FAN:
          drawFanGauge(tft, cx, cy, gR, s.chamberFanPct, dispSettings.chamberFan.arc, "Chamber", fr,
                       &dispSettings.chamberFan, smoothChamberFan);
          break;
        case GAUGE_CHAMBER_TEMP:
          drawTempGauge(tft, cx, cy, gR, s.chamberTemp, 0.0f, 60.0f,
                        dispSettings.chamberTemp.arc, "Chamber", nullptr, fr,
                        &dispSettings.chamberTemp, smoothChamberTemp);
          break;
        case GAUGE_HEATBREAK:
          drawFanGauge(tft, cx, cy, gR, s.heatbreakFanPct, dispSettings.heatbreak.arc, "HBreak", fr,
                       &dispSettings.heatbreak, smoothHeatbreakFan);
          break;
        case GAUGE_CLOCK:
          drawClockWidget(tft, cx, cy, gR, gT, fr);
          break;
        case GAUGE_LAYER:
          drawLayerGauge(tft, cx, cy, gR, gT, s.layerNum, s.totalLayers, fr);
          break;
        case GAUGE_EMPTY:
          if (fr) tft.fillCircle(cx, cy, gR + 2, dispSettings.bgColor);
          break;
        default: {
          // AMS humidity / temperature gauges — index derived from enum value
          static const char* amsLabel[AMS_MAX_UNITS] = { "AMS 1", "AMS 2", "AMS 3", "AMS 4" };
          if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
            uint8_t ui = gt - GAUGE_AMS_HUM_1;
            const AmsUnit& u = s.ams.units[ui];
            drawHumidityGauge(tft, cx, cy, gR, u.humidityRaw, u.humidity, u.present, amsLabel[ui], fr);
          } else if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
            uint8_t ui = gt - GAUGE_AMS_TEMP_1;
            const AmsUnit& u = s.ams.units[ui];
            drawTempGauge(tft, cx, cy, gR, u.present ? u.temp : 0, 0, 60.0f,
                          dispSettings.chamberTemp.arc, amsLabel[ui], nullptr, fr, &dispSettings.chamberTemp);
          } else {
            if (fr) tft.fillCircle(cx, cy, gR + 2, dispSettings.bgColor);
          }
        } break;
      }
    }
  }

  // === AMS zone (CYD: portrait + landscape) ===
#if defined(DISPLAY_240x320)
  if (s.ams.present && s.ams.unitCount > 0) {
    drawAmsZone(s, forceRedraw);
  }
#endif

  // === Info line — ETA finish time or PAUSE/ERROR alert ===
  if (etaChanged || stateChanged) {
    tft.fillRect(0, eff_etaY, SCREEN_W, eff_etaH, CLR_BG);
    tft.setTextDatum(MC_DATUM);

    if (s.gcodeStateId == GCODE_PAUSE) {
      tft.setTextFont(4);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString("PAUSED", SCREEN_W / 2, eff_etaTextY);
    } else if (s.gcodeStateId == GCODE_FAILED) {
      tft.setTextFont(4);
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString("ERROR!", SCREEN_W / 2, eff_etaTextY);
    } else if (s.remainingMinutes > 0) {
      // Use time() directly - avoids getLocalTime() race condition with timeout 0.
      // Once NTP syncs the RTC keeps running; ntpSynced latches true forever.
      static bool ntpSynced = false;
      time_t nowEpoch = time(nullptr);
      struct tm now;
      localtime_r(&nowEpoch, &now);
      if (now.tm_year > (2020 - 1900)) ntpSynced = true;

      if (!dispSettings.showTimeRemaining && ntpSynced) {
        // Calculate ETA: current time + remaining minutes
        time_t etaEpoch = nowEpoch + (time_t)s.remainingMinutes * 60;
        struct tm etaTm;
        localtime_r(&etaEpoch, &etaTm);

        char etaBuf[32];
        int etaH = etaTm.tm_hour;
        const char* ampm = "";
        if (!netSettings.use24h) {
          ampm = etaH < 12 ? "AM" : "PM";
          etaH = etaH % 12;
          if (etaH == 0) etaH = 12;
        }
        if (etaTm.tm_yday != now.tm_yday || etaTm.tm_year != now.tm_year) {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d.%02d. %02d:%02d",
                     etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d/%02d %d:%02d%s",
                     etaTm.tm_mon + 1, etaTm.tm_mday, etaH, etaTm.tm_min, ampm);
        } else {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d:%02d", etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d:%02d %s", etaH, etaTm.tm_min, ampm);
        }
        tft.setTextFont(4);
        tft.setTextColor(CLR_GREEN, CLR_BG);
        tft.drawString(etaBuf, SCREEN_W / 2, eff_etaTextY);
      } else {
        // NTP not synced yet OR user requested remaining time - show remaining time only
        char remBuf[24];
        uint16_t h = s.remainingMinutes / 60;
        uint16_t m = s.remainingMinutes % 60;
        snprintf(remBuf, sizeof(remBuf), "Remaining: %dh %02dm", h, m);
        tft.setTextFont(4);
        tft.setTextColor(CLR_TEXT, CLR_BG);
        tft.drawString(remBuf, SCREEN_W / 2, eff_etaTextY);
      }
    } else {
      tft.setTextFont(4);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("ETA: ---", SCREEN_W / 2, eff_etaTextY);
    }
  }

  // === Bottom status bar — Filament/WiFi | Layer (or Power) | Speed ===
  // Tasmota alternation state (persists across redraws)
  static bool     altShowPower    = false;
  static uint32_t altFlipMs       = 0;
  static bool     prevAltShowPower = false;
  static bool     prevTasmotaOnline = false;
  static float    prevWatts        = -2.0f;

  if (tasmotaSettings.enabled && tasmotaSettings.displayMode == 0) {
    if (millis() - altFlipMs > 4000) {
      altShowPower = !altShowPower;
      altFlipMs    = millis();
    }
  } else {
    altShowPower = false;
    altFlipMs    = 0;
  }
  bool tasmotaOnline = tasmotaIsActiveForSlot(rotState.displayIndex);
  float curWatts = tasmotaGetWatts();

  bool showingWifi = !(s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present)
                  && !(s.ams.vtPresent && s.ams.activeTray == 254);
  bool bottomChanged = forceRedraw ||
                       (s.speedLevel != prevState.speedLevel) ||
                       (s.doorOpen != prevState.doorOpen) ||
                       (s.doorSensorPresent != prevState.doorSensorPresent) ||
                       (s.layerNum != prevState.layerNum) ||
                       (s.totalLayers != prevState.totalLayers) ||
                       (s.ams.activeTray != prevState.ams.activeTray) ||
                       (showingWifi && s.wifiSignal != prevState.wifiSignal) ||
                       (tasmotaSettings.enabled && (altShowPower != prevAltShowPower || tasmotaOnline != prevTasmotaOnline ||
                                                    curWatts != prevWatts));
  prevAltShowPower  = altShowPower;
  prevTasmotaOnline = tasmotaOnline;
  prevWatts         = curWatts;

  if (bottomChanged) {
    tft.fillRect(0, eff_botY, SCREEN_W, eff_botH, CLR_BG);
    tft.setTextFont(2);

    // Left: filament indicator (if AMS active) or WiFi signal
    // Dual nozzle (H2C/H2D): activeTray set from extruder.info[].snow per-nozzle
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      if (t.present) {
        tft.drawCircle(10, eff_botCY, 5, CLR_TEXT_DARK);
        tft.fillCircle(10, eff_botCY, 4, t.colorRgb565);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        tft.drawString(t.type, 19, eff_botCY);
      } else {
        drawWifiSignalIndicator(s, eff_botCY);
      }
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      tft.drawCircle(10, eff_botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10, eff_botCY, 4, s.ams.vtColorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(s.ams.vtType, 19, eff_botCY);
    } else {
      drawWifiSignalIndicator(s, eff_botCY);
    }

    // Center: power (if Tasmota active) or layer count
    bool showPowerNow = tasmotaSettings.enabled && tasmotaOnline &&
                        (tasmotaSettings.displayMode == 1 || altShowPower);
    if (showPowerNow) {
      drawIcon16(tft, SCREEN_W / 2 - 20, eff_botCY - 8, icon_lightning, CLR_YELLOW);
      char wBuf[8];
      snprintf(wBuf, sizeof(wBuf), "%.0fW", tasmotaGetWatts());
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(wBuf, SCREEN_W / 2 - 2, eff_botCY);
    } else {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      char layerBuf[20];
      snprintf(layerBuf, sizeof(layerBuf), "L%d/%d", s.layerNum, s.totalLayers);
      tft.drawString(layerBuf, SCREEN_W / 2, eff_botCY);
    }

    // Right: door status (if sensor present) or speed mode
    if (s.doorSensorPresent) {
      uint16_t clr = s.doorOpen ? CLR_ORANGE : CLR_GREEN;
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(clr, CLR_BG);
      tft.drawString("Door", SCREEN_W - 20, eff_botCY);
      drawIcon16(tft, SCREEN_W - 18, eff_botCY - 8,
                 s.doorOpen ? icon_unlock : icon_lock, clr);
    } else {
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(speedLevelColor(s.speedLevel), CLR_BG);
      tft.drawString(speedLevelName(s.speedLevel), SCREEN_W - 4, eff_botCY);
    }
  }
}

// ---------------------------------------------------------------------------
//  Screen: Finished (same layout as printing, but with 2 gauges + status)
// ---------------------------------------------------------------------------
static void drawFinished() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;
  static bool  prevFinTasmotaOnline = false;
  static float prevFinWatts = -2.0f;
  static float prevFinKwh = -2.0f;

  // Effective screen dimensions — finished uses full screen (no AMS sidebar)
#if defined(DISPLAY_240x320)
  const bool land = isLandscape();
  const int16_t scrW = (int16_t)tft.width();
  const int16_t eff_finBotY  = land ? LY_LAND_FIN_BOT_Y  : LY_FIN_BOT_Y;
  const int16_t eff_finBotH  = land ? LY_LAND_FIN_BOT_H  : LY_FIN_BOT_H;
  const int16_t eff_finWifiY = land ? LY_LAND_FIN_WIFI_Y  : LY_FIN_WIFI_Y;
#else
  const int16_t scrW = SCREEN_W;
  const int16_t eff_finBotY  = LY_FIN_BOT_Y;
  const int16_t eff_finBotH  = LY_FIN_BOT_H;
  const int16_t eff_finWifiY = LY_FIN_WIFI_Y;
#endif
  const int16_t cx = scrW / 2;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);

  const int16_t gR = LY_FIN_GAUGE_R;
  const int16_t gaugeLeft  = LY_FIN_GL;
  const int16_t gaugeRight = LY_FIN_GR;
  const int16_t gaugeY = LY_FIN_GY;

  // === H2-style LED progress bar at 100% (y=0-5) ===
  if (forceRedraw) {
    drawLedProgressBar(tft, 0, 100);
  }

  // === Header bar (y=7-25) — same as printing screen ===
  if (forceRedraw) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, LY_HDR_Y, scrW, LY_HDR_H, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Printer";
    tft.drawString(name, LY_HDR_NAME_X, LY_HDR_CY);

    // FINISH badge (right)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_GREEN, hdrBg);
    tft.setTextFont(2);
    tft.fillCircle(scrW - LY_HDR_BADGE_RX - tft.textWidth("FINISH") - 10, LY_HDR_CY, 4, CLR_GREEN);
    tft.drawString("FINISH", scrW - LY_HDR_BADGE_RX, LY_HDR_CY);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(cx - 5 + di * 10, LY_HDR_DOT_CY, 3, dotClr);
      }
    }
  }

  // === Row 1: Nozzle | Bed (two gauges centered) ===
  if (tempChanged) {
    drawTempGauge(tft, gaugeLeft, gaugeY, gR,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    drawTempGauge(tft, gaugeRight, gaugeY, gR,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // === "Print Complete!" status ===
  if (forceRedraw) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setTextFont(4);
    tft.drawString("Print Complete!", cx, LY_FIN_TEXT_Y);
  }

  // === File name ===
  if (forceRedraw) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    if (s.subtaskName[0] != '\0') {
      char truncName[26];
      strncpy(truncName, s.subtaskName, 25);
      truncName[25] = '\0';
      tft.drawString(truncName, cx, LY_FIN_FILE_Y);
    }
  }

  // === kWh used during print (between filename and bottom bar) ===
  bool tasmotaActiveHere = tasmotaIsActiveForSlot(rotState.displayIndex);
  float finishKwh = tasmotaActiveHere ? tasmotaGetPrintKwhUsed() : -1.0f;
  bool kwhChanged = (tasmotaActiveHere && tasmotaKwhChanged()) ||
                    (tasmotaActiveHere != prevFinTasmotaOnline) ||
                    (finishKwh != prevFinKwh);
  if (forceRedraw || kwhChanged) {
#if defined(DISPLAY_240x320)
    int16_t kwhY = land ? (LY_FIN_FILE_Y + eff_finBotY) / 2 : LY_FIN_KWH_Y;
#else
    int16_t kwhY = (LY_FIN_FILE_Y + eff_finBotY) / 2;
#endif
    tft.fillRect(0, kwhY - 9, scrW, 18, CLR_BG);
    if (tasmotaActiveHere && finishKwh >= 0.0f) {
      drawIcon16(tft, cx - 32, kwhY - 8, icon_lightning, CLR_YELLOW);
      char kwhBuf[16];
      snprintf(kwhBuf, sizeof(kwhBuf), "%.3f kWh", finishKwh);
      tft.setTextDatum(ML_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(kwhBuf, cx - 14, kwhY);
    }
  }
  prevFinKwh = finishKwh;

  // === AMS strip (portrait 240x320 only) ===
#if defined(DISPLAY_240x320)
  if (!land && s.ams.present && s.ams.unitCount > 0) {
    static uint8_t  prevFinAmsCount = 0;
    static uint8_t  prevFinAmsActive = 255;
    static uint16_t prevFinAmsColors[AMS_MAX_TRAYS] = {0};
    static bool     prevFinAmsPresent[AMS_MAX_TRAYS] = {false};
    static int8_t   prevFinAmsRemain[AMS_MAX_TRAYS];

    bool amsChanged = forceRedraw ||
                      (s.ams.unitCount != prevFinAmsCount) ||
                      (s.ams.activeTray != prevFinAmsActive);
    if (!amsChanged) {
      for (uint8_t i = 0; i < s.ams.unitCount * AMS_TRAYS_PER_UNIT && !amsChanged; i++) {
        amsChanged = (s.ams.trays[i].present != prevFinAmsPresent[i]) ||
                     (s.ams.trays[i].colorRgb565 != prevFinAmsColors[i]) ||
                     (s.ams.trays[i].remain != prevFinAmsRemain[i]);
      }
    }
    if (amsChanged) {
      prevFinAmsCount = s.ams.unitCount;
      prevFinAmsActive = s.ams.activeTray;
      for (uint8_t i = 0; i < AMS_MAX_TRAYS; i++) {
        prevFinAmsPresent[i] = s.ams.trays[i].present;
        prevFinAmsColors[i]  = s.ams.trays[i].colorRgb565;
        prevFinAmsRemain[i]  = s.ams.trays[i].remain;
      }
      drawAmsStrip(s.ams, LY_FIN_AMS_Y, LY_FIN_AMS_H, LY_FIN_AMS_BAR_H);
    }
  }
#endif

  // === Bottom status bar ===
  bool waitingForDoor = dpSettings.doorAckEnabled && s.doorSensorPresent && !s.doorAcknowledged;
  float finCurWatts = tasmotaGetWatts();
  bool finBottomChanged = forceRedraw ||
                          (waitingForDoor != prevWaitingForDoor) ||
                          (s.doorSensorPresent && s.doorOpen != prevState.doorOpen) ||
                          (tasmotaActiveHere != prevFinTasmotaOnline) ||
                          (finCurWatts != prevFinWatts);
  if (finBottomChanged) {
    prevWaitingForDoor = waitingForDoor;
    tft.fillRect(0, eff_finBotY, scrW, eff_finBotH, CLR_BG);
    tft.setTextFont(1);
    if (waitingForDoor) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_ORANGE, CLR_BG);
      tft.drawString("Open door to dismiss", cx, eff_finWifiY);
    } else {
      drawIcon16(tft, 4, eff_finWifiY - 8, icon_wifi, CLR_TEXT_DIM);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      char wifiBuf[12];
      snprintf(wifiBuf, sizeof(wifiBuf), "%ddBm", s.wifiSignal);
      tft.drawString(wifiBuf, 22, eff_finWifiY);

      if (tasmotaActiveHere) {
        drawIcon16(tft, cx - 20, eff_finWifiY - 8, icon_lightning, CLR_YELLOW);
        char wBuf[8];
        snprintf(wBuf, sizeof(wBuf), "%.0fW", finCurWatts);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        tft.drawString(wBuf, cx - 2, eff_finWifiY);
      }
    }
    // Door status (right) — always show when sensor present
    if (s.doorSensorPresent) {
      uint16_t clr = s.doorOpen ? CLR_ORANGE : CLR_GREEN;
      tft.setTextDatum(MR_DATUM);
      tft.setTextFont(1);
      tft.setTextColor(clr, CLR_BG);
      tft.drawString("Door", scrW - 20, eff_finWifiY);
      drawIcon16(tft, scrW - 18, eff_finWifiY - 8,
                 s.doorOpen ? icon_unlock : icon_lock, clr);
    }
  }
  prevFinTasmotaOnline = tasmotaActiveHere;
  prevFinWatts = finCurWatts;
}

// ---------------------------------------------------------------------------
//  Night mode — scheduled brightness dimming
// ---------------------------------------------------------------------------
static unsigned long lastNightCheck = 0;
// lastAppliedBrightness declared near setBacklight() above

static bool isNightHour() {
  struct tm now;
  time_t t = time(nullptr);
  localtime_r(&t, &now);
  if (now.tm_year < (2020 - 1900)) return false;  // NTP not synced yet

  uint8_t h = now.tm_hour;
  uint8_t s = dpSettings.nightStartHour;
  uint8_t e = dpSettings.nightEndHour;

  if (s == e) return false;  // same hour = disabled
  if (s < e) return (h >= s && h < e);     // e.g. 01:00-07:00
  return (h >= s || h < e);                // e.g. 22:00-07:00 (wraps midnight)
}

uint8_t getEffectiveBrightness() {
  if (currentScreen == SCREEN_CLOCK) {
    // During night hours, use the dimmer of the two
    if (dpSettings.nightModeEnabled && isNightHour()) {
      return min(dpSettings.screensaverBrightness, dpSettings.nightBrightness);
    }
    return dpSettings.screensaverBrightness;
  }
  if (dpSettings.nightModeEnabled && isNightHour()) {
    return dpSettings.nightBrightness;
  }
  return brightness;
}

void checkNightMode() {
  // Check once per minute
  unsigned long now = millis();
  if (now - lastNightCheck < 60000) return;
  lastNightCheck = now;

  // Don't interfere with screen off
  if (currentScreen == SCREEN_OFF) return;

  uint8_t target = getEffectiveBrightness();
  if (target != lastAppliedBrightness) {
    setBacklight(target);
    lastAppliedBrightness = target;
  }
}

// ---------------------------------------------------------------------------
//  Main update (called from loop)
// ---------------------------------------------------------------------------
void updateDisplay() {
  // Shimmer runs at its own cadence (~40fps), independent of display refresh
  if (currentScreen == SCREEN_PRINTING) {
    BambuState& sh = displayedPrinter().state;
    tickProgressShimmer(tft, 0, sh.progress, sh.printing);
  }
  if (currentScreen == SCREEN_IDLE && isPrinterConfigured(rotState.displayIndex)) {
    BambuState& sh = displayedPrinter().state;
    if (sh.ams.anyDrying) {
      uint8_t dp = 0;
      AmsUnit* du = nullptr;
      for (uint8_t i = 0; i < sh.ams.unitCount; i++) {
        if (sh.ams.units[i].dryRemainMin > 0) { du = &sh.ams.units[i]; break; }
      }
      if (du && du->dryTotalMin > 0 && du->dryRemainMin <= du->dryTotalMin)
        dp = 100 - (uint8_t)((uint32_t)du->dryRemainMin * 100 / du->dryTotalMin);
      tickProgressShimmer(tft, 0, dp, true);
    }
  }
  // Pong clock runs at ~50fps, independent of display refresh
  if (currentScreen == SCREEN_CLOCK && dispSettings.pongClock) {
    tickPongClock();
  }

  unsigned long now = millis();
  unsigned long interval = gaugesAnimating ? GAUGE_ANIM_MS : DISPLAY_UPDATE_MS;
  if (now - lastDisplayUpdate < interval) return;
  lastDisplayUpdate = now;

  // Detect screen change
  if (currentScreen != prevScreen) {
    // Restore backlight when leaving SCREEN_OFF or SCREEN_CLOCK
    if ((prevScreen == SCREEN_OFF || prevScreen == SCREEN_CLOCK) &&
        currentScreen != SCREEN_OFF && currentScreen != SCREEN_CLOCK) {
      setBacklight(getEffectiveBrightness());
    }
    // Reset text size in case Pong clock left it scaled up
    tft.setTextSize(1);
    tft.fillScreen(currentScreen == SCREEN_OFF ? TFT_BLACK : dispSettings.bgColor);
    forceRedraw = true;
    if (currentScreen == SCREEN_CONNECTING_WIFI || currentScreen == SCREEN_CONNECTING_MQTT) {
      connectScreenStart = millis();
    }
    if (currentScreen == SCREEN_CLOCK) {
      if (dispSettings.pongClock) resetPongClock();
      else resetClock();
      setBacklight(getEffectiveBrightness());  // dim for screensaver
    }
    prevScreen = currentScreen;
  }

  switch (currentScreen) {
    case SCREEN_SPLASH:
      // Splash shown in initDisplay(), auto-advance handled by main.cpp
      break;

    case SCREEN_AP_MODE:
      if (forceRedraw) drawAPMode();
      break;

    case SCREEN_CONNECTING_WIFI:
      drawConnectingWiFi();
      break;

    case SCREEN_WIFI_CONNECTED:
      drawWiFiConnected();
      break;

    case SCREEN_CONNECTING_MQTT:
      drawConnectingMQTT();
      break;

    case SCREEN_OTA_UPDATE:
      drawOtaUpdate();
      break;

    case SCREEN_IDLE:
      drawIdle();
      break;

    case SCREEN_PRINTING:
      drawPrinting();
      break;

    case SCREEN_FINISHED:
      drawFinished();
      break;

    case SCREEN_CLOCK:
      if (!dispSettings.pongClock) drawClock();
      // Pong clock is ticked before the throttle (above)
      break;

    case SCREEN_OFF:
      if (forceRedraw) {
        tft.fillScreen(TFT_BLACK);
        setBacklight(0);
        triggerDisplayTransition();  // clear gauge cache so wake shows fresh data
      }
      break;
  }

  // Save state for next smart-redraw comparison
  memcpy(&prevState, &displayedPrinter().state, sizeof(BambuState));
  forceRedraw = false;
}
