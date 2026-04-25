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
#include <new>   // placement new for CYD panel variant selection

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
// Two hardware variants exist:
//   - V2 (default): Panel_ILI9341_2 + color inversion — matches the TFT_eSPI
//     ILI9341_2_DRIVER + TFT_INVERSION_ON used on `main`.
//   - Classic: plain Panel_ILI9341, no color inversion — for units that show
//     mirrored/rotated image on V2.
// Selected at runtime from DisplaySettings.cydPanelClassic (persisted in
// Preferences).
template <class PanelT, bool InvertColors, uint8_t RotationOffset>
class LGFX_CYD_Impl : public lgfx::LGFX_Device {
  PanelT          _panel;
  lgfx::Bus_SPI   _bus;
public:
  LGFX_CYD_Impl() {
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
      cfg.offset_rotation = RotationOffset;
      cfg.invert        = InvertColors;
      cfg.rgb_order     = false;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
using LGFX_CYD_V2      = LGFX_CYD_Impl<lgfx::Panel_ILI9341_2, true,  6>;
using LGFX_CYD_Classic = LGFX_CYD_Impl<lgfx::Panel_ILI9341,   false, 2>;
// One of these is placement-new'd into _tft_storage in initDisplay() based on
// dispSettings.cydPanelClassic. Alignment covers both; size covers the larger.
union LGFX_CYD_Storage {
  LGFX_CYD_V2      v2;
  LGFX_CYD_Classic classic;
  LGFX_CYD_Storage() : v2() {}   // default-construct V2 for static-init safety
  ~LGFX_CYD_Storage() {}
};
static LGFX_CYD_Storage   _tft_storage;
// _tft_instance is a reference to the base LGFX_Device for the currently
// constructed variant. Defaults to V2; rebound via placement-new in
// initDisplay() if the user selected Classic.
static lgfx::LGFX_Device& _tft_instance = _tft_storage.v2;

#elif defined(BOARD_IS_WS200)
// --- Waveshare ESP32-S3-Touch-LCD-2 (2.0" ST7789 240x320) --------------------
class LGFX_WS200 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS200() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 39;
      cfg.pin_mosi   = 38;
      cfg.pin_miso   = 40;
      cfg.pin_dc     = 42;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 45;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_WS200 _tft_instance;

#elif defined(BOARD_IS_WS154)
// --- Waveshare ESP32-S3-Touch-LCD-1.54 (1.54" ST7789 240x240) ---------------
class LGFX_WS154 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS154() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 38;
      cfg.pin_mosi   = 39;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 45;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 21;
      cfg.pin_rst  = 40;
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
static LGFX_WS154 _tft_instance;

#elif defined(BOARD_IS_C3)
// --- ESP32-C3 Super Mini + ST7789 240x240 ------------------------------------
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
static LGFX_C3 _tft_instance;

#else
  #error "No board variant defined. Add BOARD_IS_S3, DISPLAY_CYD, BOARD_IS_C3, BOARD_IS_WS200 or BOARD_IS_WS154 to build_flags."
#endif

// Global pointer + reference — accessed via `tft` throughout the codebase.
// For CYD, _tft_instance is backed by a union (see _tft_storage) that is
// populated with either the V2 or Classic panel in initDisplay(), so method
// calls via this reference/pointer dispatch to whichever variant was chosen.
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

#if defined(DISPLAY_CYD)
static void applyCydPanelInversion() {
  _tft_instance.invertDisplay(dispSettings.invertColors);
}
#endif

// ---------------------------------------------------------------------------
//  Active-canvas helpers (rotation-aware; needed before drawing)
// ---------------------------------------------------------------------------
#if defined(DISPLAY_240x320)
// Forward declared here so non-CYD-specific drawers can call them.
static bool isLandscape() {
  return (dispSettings.rotation == 1 || dispSettings.rotation == 3);
}
static int16_t uiW() { return (int16_t)tft.width(); }
static int16_t uiH() { return (int16_t)tft.height(); }
// In landscape with 0..2 AMS units the bottom bar spans the full 320px and
// the AMS column ends higher (BOT_SHORT). With 3-4 AMS units the bottom bar
// is limited to 240px so AMS can extend further down (BOT_FULL).
static bool landBottomBarFullWidth(uint8_t units) {
  return units <= 2;
}
#else
static inline bool isLandscape() { return false; }
static inline int16_t uiW() { return SCREEN_W; }
static inline int16_t uiH() { return SCREEN_H; }
static inline bool landBottomBarFullWidth(uint8_t) { return true; }
#endif

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initDisplay() {
  Serial.println("Display: pre-init delay...");
  delay(500);
#if defined(DISPLAY_CYD)
  // Pick CYD panel variant based on loaded settings. Default static-init
  // already constructed V2; swap to Classic if user selected it.
  if (dispSettings.cydPanelClassic) {
    _tft_storage.v2.~LGFX_CYD_V2();
    new (&_tft_storage.classic) LGFX_CYD_Classic();
    Serial.println("Display: CYD panel variant = Classic (Panel_ILI9341)");
  } else {
    Serial.println("Display: CYD panel variant = V2 (Panel_ILI9341_2)");
  }
  // _tft_instance reference + tft_ptr already point at the same storage.
#endif
  Serial.println("Display: calling _tft_instance.init()...");
  _tft_instance.init();  // LovyanGFX configures SPI from the board class above
#if defined(DISPLAY_CYD)
  applyCydPanelInversion();
#elif defined(BOARD_IS_S3) || defined(BOARD_IS_C3) || defined(BOARD_IS_WS200) || defined(BOARD_IS_WS154)
  _tft_instance.invertDisplay(true);  // ST7789 requires color inversion
#endif
  Serial.println("Display: tft.init() done");
#if defined(DISPLAY_240x320)
  // Clear entire GRAM at rotation 0 first (guarantees all 240x320 pixels
  // are addressed). Without this, rotations 1/3 leave 80px of uninitialized
  // VRAM visible as garbage noise on the extra screen edge.
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
  tft.setRotation(dispSettings.rotation);
#if defined(DISPLAY_CYD)
  applyCydPanelInversion();
#elif defined(DISPLAY_240x320)
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

  // Splash screen — center on actual canvas (rotation-aware for 240x320)
  {
    const int16_t sw = uiW();
    const int16_t sh = uiH();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setTextFont(4);
    tft.drawString("BambuHelper", sw / 2, sh / 2 - 20);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString("Printer Monitor", sw / 2, sh / 2 + 10);
    tft.setTextFont(1);
    tft.drawString(FW_VERSION, sw / 2, sh / 2 + 30);
  }
}

void applyDisplaySettings() {
#if defined(DISPLAY_240x320)
  // Pre-clear entire GRAM at rotation 0 to prevent garbage on edges
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
  tft.setRotation(dispSettings.rotation);
#if defined(DISPLAY_CYD)
  applyCydPanelInversion();
#elif defined(DISPLAY_240x320)
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
  const int16_t cx = uiW() / 2;
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Setup", cx, LY_AP_TITLE_Y);

  // Instructions
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connect to WiFi:", cx, LY_AP_SSID_LBL_Y);

  // AP SSID
  tft.setTextColor(CLR_CYAN, CLR_BG);
  tft.setTextFont(4);
  char ssid[32];
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_PREFIX, mac);
  tft.drawString(ssid, cx, LY_AP_SSID_Y);

  // Password
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Password:", cx, LY_AP_PASS_LBL_Y);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(WIFI_AP_PASSWORD, cx, LY_AP_PASS_Y);

  // IP
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Then open:", cx, LY_AP_OPEN_Y);
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("192.168.4.1", cx, LY_AP_IP_Y);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting WiFi
// ---------------------------------------------------------------------------
static void drawConnectingWiFi() {
  const int16_t sw = uiW();
  const int16_t sh = uiH();
  const int16_t cx = sw / 2;
  const int16_t cy = sh / 2;
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to WiFi", cx, cy - 20);

  int16_t tw = tft.textWidth("Connecting to WiFi");
  drawAnimDots(tft, cx + tw / 2, cy - 26, CLR_TEXT);

  // Slide bar
  const int16_t barW = 180;
  const int16_t barH = 8;
  drawSlideBar(tft, (sw - barW) / 2, cy + 4,
               barW, barH, CLR_BLUE, CLR_TRACK);
}

// ---------------------------------------------------------------------------
//  Screen: WiFi Connected (show IP)
// ---------------------------------------------------------------------------
static void drawWiFiConnected() {
  if (!forceRedraw) return;

  const int16_t sw = uiW();
  const int16_t midX = sw / 2;
  const int16_t midY = uiH() / 2;
  tft.setTextDatum(MC_DATUM);

  // Checkmark circle with tick
  int cx = midX;
  int cy = midY - 40;
  tft.fillCircle(cx, cy, 25, CLR_GREEN);
  // Draw thick tick mark (3px wide)
  for (int i = -1; i <= 1; i++) {
    tft.drawLine(cx - 12, cy + i,     cx - 4, cy + 8 + i, CLR_BG);  // short leg
    tft.drawLine(cx - 4,  cy + 8 + i, cx + 12, cy - 6 + i, CLR_BG); // long leg
  }

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Connected", midX, midY + 10);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString(WiFi.localIP().toString().c_str(), midX, midY + 40);
}

// ---------------------------------------------------------------------------
//  Screen: OTA firmware update in progress
// ---------------------------------------------------------------------------
#include "web_server.h"
static void drawOtaUpdate() {
  const int16_t sw = uiW();
  const int16_t cx = sw / 2;
  const int16_t cy = uiH() / 2;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT, CLR_BG);

  // Title
  tft.setTextFont(4);
  tft.drawString("Updating", cx, cy - 60);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("BambuHelper firmware", cx, cy - 36);

  // Progress bar
  int pct = getOtaAutoProgress();
  const int16_t barX = 20, barY = cy - 10;
  const int16_t barW = sw - 40, barH = 14;
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
  tft.drawString(pctBuf, cx, cy + 14);

  // Status
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString(getOtaAutoStatus(), cx, cy + 34);

  // Warning
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.drawString("Do not power off", cx, cy + 58);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting MQTT
// ---------------------------------------------------------------------------
static void drawConnectingMQTT() {
  const int16_t sw = uiW();
  const int16_t cx = sw / 2;
  const int16_t cy = uiH() / 2;
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to Printer", cx, cy - 40);

  int16_t tw = tft.textWidth("Connecting to Printer");
  drawAnimDots(tft, cx + tw / 2, cy - 46, CLR_TEXT);
  tft.setTextDatum(MC_DATUM);

  // Slide bar
  const int16_t barW = 180;
  const int16_t barH = 8;
  drawSlideBar(tft, (sw - barW) / 2, cy - 14,
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
  tft.drawString(infoBuf, cx, cy + 10);

  // Elapsed time
  if (connectScreenStart > 0) {
    unsigned long elapsed = (millis() - connectScreenStart) / 1000;
    char elBuf[16];
    snprintf(elBuf, sizeof(elBuf), "%lus", elapsed);
    tft.fillRect(cx - 30, cy + 22, 60, 16, CLR_BG);
    tft.drawString(elBuf, cx, cy + 30);
  }

  // Diagnostics (only after first attempt)
  const MqttDiag& d = getMqttDiag(rotState.displayIndex);
  if (d.attempts > 0) {
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);

    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt: %u", d.attempts);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, cx, cy + 50);

    if (d.lastRc != 0) {
      snprintf(buf, sizeof(buf), "Err: %s", mqttRcToString(d.lastRc));
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString(buf, cx, cy + 62);
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

  const int16_t cx = uiW() / 2;
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("BambuHelper", cx, LY_IDLE_NP_TITLE_Y);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("WiFi Connected", cx, LY_IDLE_NP_WIFI_Y);

  tft.fillCircle(cx, LY_IDLE_NP_DOT_Y, 5, CLR_GREEN);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("No printer configured", cx, LY_IDLE_NP_MSG_Y);
  tft.drawString("Open in browser:", cx, LY_IDLE_NP_OPEN_Y);

  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString(WiFi.localIP().toString().c_str(), cx, LY_IDLE_NP_IP_Y);
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

static void drawCelsiusUnit(int16_t x, int16_t y, uint16_t color) {
  tft.setTextFont(4);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(color, CLR_BG);
  tft.drawString("C", x + 12, y);
  tft.drawCircle(x + 4, y - 8, 3, color);
  tft.drawCircle(x + 4, y - 8, 2, color);
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
  const bool land = isLandscape();
  const int16_t scrW = uiW();
  const int16_t cx = scrW / 2;

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

  // Change detection: keep fields independent so temperature/humidity updates
  // do not erase the whole drying screen.
  static int8_t   prevDryUnitIndex = -1;
  static uint8_t  prevDryCount = 0xFF;
  static uint16_t prevDryMin = 0xFFFF;
  static uint8_t  prevHumidity = 0xFF;
  static uint8_t  prevHumRaw = 0xFF;
  static int16_t  prevTempShown = -32768;
  static uint8_t  prevDryProgress = 0xFF;

  int16_t tempShown = (int16_t)((u.temp >= 0.0f) ? (u.temp + 0.5f) : (u.temp - 0.5f));
  bool unitChanged = forceRedraw ||
                     ui != prevDryUnitIndex ||
                     dryCount != prevDryCount;
  bool tempChanged = unitChanged || tempShown != prevTempShown;
  bool remainChanged = unitChanged || u.dryRemainMin != prevDryMin;
  bool humidityChanged = unitChanged ||
                         u.humidity != prevHumidity ||
                         u.humidityRaw != prevHumRaw;

  // === Progress bar (top, y=0-5) ===
  uint8_t dryProgress = 0;
  if (u.dryTotalMin > 0 && u.dryRemainMin <= u.dryTotalMin)
    dryProgress = 100 - (uint8_t)((uint32_t)u.dryRemainMin * 100 / u.dryTotalMin);
  bool progressChanged = unitChanged || dryProgress != prevDryProgress;
  if (progressChanged) {
    drawLedProgressBar(tft, 0, dryProgress);
  }

  // === Header bar ===
  if (forceRedraw) {
    tft.fillRect(0, LY_HDR_Y, scrW, LY_HDR_H, CLR_BG);

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
    tft.fillCircle(scrW - LY_HDR_BADGE_RX - tft.textWidth(badge) - 10, LY_HDR_CY, 4, CLR_ORANGE);
    tft.drawString(badge, scrW - LY_HDR_BADGE_RX, LY_HDR_CY);

    // Multi-printer dots
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(cx - 5 + di * 10, LY_HDR_DOT_CY, 3, dotClr);
      }
    }
  }

  // === AMS unit name (below header) ===
  if (unitChanged) {
    bool isHT = (u.id >= 128);
    uint8_t displayNum = isHT ? (u.id - 128 + 1) : (u.id + 1);
    const char* prefix = isHT ? "AMS HT" : "AMS";
    char unitName[24];
    if (dryCount > 1)
      snprintf(unitName, sizeof(unitName), "%s %d  (%d/%d)", prefix, displayNum, dryDisplayIdx + 1, dryCount);
    else
      snprintf(unitName, sizeof(unitName), "%s %d", prefix, displayNum);

    tft.fillRect(0, 30, scrW, 20, CLR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    tft.drawString(unitName, cx, 40);
  }

#if defined(DISPLAY_240x320)
  if (land) {
    // === 320x240 landscape: temp left, drying facts right, ETA above bottom ===
    if (unitChanged) {
      tft.fillRect(0, 55, scrW, LY_LAND_ETA_Y - 55, CLR_BG);
    }

    if (tempChanged) {
      if (!unitChanged) tft.fillRect(0, 70, 160, 72, CLR_BG);
      char tempBuf[14];
      snprintf(tempBuf, sizeof(tempBuf), "%d", tempShown);
      const int16_t tempCx = 88;
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(7);
      tft.setTextColor(CLR_ORANGE, CLR_BG);
      int16_t tempW = tft.textWidth(tempBuf);
      tft.drawString(tempBuf, tempCx - 10, 112);

      drawCelsiusUnit(tempCx - 10 + tempW / 2 + 2, 98, CLR_ORANGE);
    }

    const int16_t infoCx = 238;
    if (remainChanged) {
      if (!unitChanged) tft.fillRect(160, 58, scrW - 160, 54, CLR_BG);
      char timeBuf[16];
      uint16_t h = u.dryRemainMin / 60;
      uint16_t m = u.dryRemainMin % 60;
      if (h > 0)
        snprintf(timeBuf, sizeof(timeBuf), "%dh %02dm", h, m);
      else
        snprintf(timeBuf, sizeof(timeBuf), "%dm", m);

      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Remaining", infoCx, 72);
      tft.setTextFont(4);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString(timeBuf, infoCx, 96);
    }

    if (humidityChanged) {
      if (!unitChanged) tft.fillRect(160, 114, scrW - 160, 54, CLR_BG);
      char humBuf[8];
      snprintf(humBuf, sizeof(humBuf), "%d%%", u.humidityRaw);

      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Humidity", infoCx, 128);
      tft.setTextFont(4);
      tft.setTextColor(humidityColor(u.humidity), CLR_BG);
      tft.drawString(humBuf, infoCx, 152);
    }
  } else {
    // === 240x320 portrait: Centered large temp + remaining + humidity ===
    // Vertically centered between unit name (y~50) and ETA (y=260)
    if (tempChanged) {
      tft.fillRect(0, 55, SCREEN_W, 75, CLR_BG);

      char tempBuf[14];
      snprintf(tempBuf, sizeof(tempBuf), "%d", tempShown);
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(7);
      tft.setTextColor(CLR_ORANGE, CLR_BG);
      int16_t tempW = tft.textWidth(tempBuf);
      tft.drawString(tempBuf, cx - 10, 100);

      drawCelsiusUnit(cx - 10 + tempW / 2 + 2, 86, CLR_ORANGE);
    }

    // === Remaining time ===
    if (remainChanged) {
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
    if (humidityChanged) {
      const int16_t humY = 200;
      tft.fillRect(0, humY - 14, SCREEN_W, 30, CLR_BG);
      char humBuf[24];
      snprintf(humBuf, sizeof(humBuf), "Humidity: %d%%", u.humidityRaw);

      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(4);
      tft.setTextColor(humidityColor(u.humidity), CLR_BG);
      tft.drawString(humBuf, cx, humY);
    }
  }
#else
  // === 240x240: Large temperature display (center) ===
  if (tempChanged) {
    tft.fillRect(0, 55, SCREEN_W, 65, CLR_BG);

    char tempBuf[14];
    snprintf(tempBuf, sizeof(tempBuf), "%d", tempShown);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(7);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    int16_t tempW = tft.textWidth(tempBuf);
    tft.drawString(tempBuf, cx - 10, 82);

    drawCelsiusUnit(cx - 10 + tempW / 2 + 2, 68, CLR_ORANGE);
  }

  // === Remaining time ===
  if (remainChanged) {
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
  if (humidityChanged) {
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
  if (remainChanged) {
#if defined(DISPLAY_240x320)
    const int16_t etaY = land ? LY_LAND_ETA_Y : LY_ETA_Y;
    const int16_t etaH = land ? LY_LAND_ETA_H : LY_ETA_H;
    const int16_t etaTextY = land ? LY_LAND_ETA_TEXT_Y : LY_ETA_TEXT_Y;
#else
    const int16_t etaY = LY_ETA_Y;
    const int16_t etaH = LY_ETA_H;
    const int16_t etaTextY = LY_ETA_TEXT_Y;
#endif
    tft.fillRect(0, etaY, scrW, etaH, CLR_BG);
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
    tft.drawString(etaBuf, cx, etaTextY);
  }

  // === Bottom bar — connected indicator ===
  {
#if defined(DISPLAY_240x320)
    const int16_t botY = land ? LY_LAND_BOT_Y : LY_BOT_Y;
    const int16_t botH = land ? LY_LAND_BOT_H : LY_BOT_H;
    const int16_t botCY = land ? LY_LAND_BOT_CY : LY_BOT_CY;
#else
    const int16_t botY = LY_BOT_Y;
    const int16_t botH = LY_BOT_H;
    const int16_t botCY = LY_BOT_CY;
#endif
    bool connChanged = forceRedraw || (s.connected != prevState.connected);
    if (connChanged) {
      tft.fillRect(0, botY, scrW, botH, CLR_BG);
      tft.fillCircle(cx, botCY, 4, s.connected ? CLR_GREEN : CLR_RED);
    }
  }

  prevDryUnitIndex = ui;
  prevDryCount = dryCount;
  prevDryMin = u.dryRemainMin;
  prevHumidity = u.humidity;
  prevHumRaw = u.humidityRaw;
  prevTempShown = tempShown;
  prevDryProgress = dryProgress;
}

static bool wasNoPrinter = false;

// Forward declarations for 240x320 functions used before their definition
#if defined(DISPLAY_240x320)
static void drawAmsStrip(const AmsState& ams, int16_t zoneY, int16_t zoneH, int16_t barH,
                         int16_t barMaxW = LY_AMS_BAR_MAX_W,
                         bool showFilamentTypes = false);
static bool useEnhancedPortraitAms(const AmsState& ams);
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
    static char     prevIdleAmsTypes[AMS_MAX_TRAYS][16] = {{0}};

    bool enhanced = useEnhancedPortraitAms(s.ams);
    bool amsChanged = forceRedraw ||
                      (s.ams.unitCount != prevIdleAmsCount) ||
                      (s.ams.activeTray != prevIdleAmsActive);
    if (!amsChanged) {
      for (uint8_t i = 0; i < s.ams.unitCount * AMS_TRAYS_PER_UNIT && !amsChanged; i++) {
        amsChanged = (s.ams.trays[i].present != prevIdleAmsPresent[i]) ||
                     (s.ams.trays[i].colorRgb565 != prevIdleAmsColors[i]) ||
                     (s.ams.trays[i].remain != prevIdleAmsRemain[i]);
        if (!amsChanged && enhanced) {
          amsChanged = strncmp(s.ams.trays[i].type, prevIdleAmsTypes[i], 16) != 0;
        }
      }
    }

    if (amsChanged) {
      prevIdleAmsCount = s.ams.unitCount;
      prevIdleAmsActive = s.ams.activeTray;
      for (uint8_t i = 0; i < AMS_MAX_TRAYS; i++) {
        prevIdleAmsPresent[i] = s.ams.trays[i].present;
        prevIdleAmsColors[i]  = s.ams.trays[i].colorRgb565;
        prevIdleAmsRemain[i]  = s.ams.trays[i].remain;
        strncpy(prevIdleAmsTypes[i], s.ams.trays[i].type, 15);
        prevIdleAmsTypes[i][15] = '\0';
      }
      if (enhanced) {
        drawAmsStrip(s.ams, LY_IDLE_AMS_Y, LY_IDLE_AMS_H, LY_IDLE_AMS_BAR_H,
                     LY_AMS_BAR_MAX_W_EXTRAS, /*showFilamentTypes=*/true);
      } else {
        drawAmsStrip(s.ams, LY_IDLE_AMS_Y, LY_IDLE_AMS_H, LY_IDLE_AMS_BAR_H);
      }
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

static uint8_t  prevAmsUnitCount = 0;
static uint8_t  prevAmsActive    = 255;
static uint16_t prevAmsTrayColors[AMS_MAX_TRAYS] = {0};
static bool     prevAmsTrayPresent[AMS_MAX_TRAYS] = {false};
static int8_t   prevAmsTrayRemain[AMS_MAX_TRAYS];  // init in drawAmsZone
static char     prevAmsTrayTypes[AMS_MAX_TRAYS][16] = {{0}};

// Extract a short display label from a filament type string.
// Takes the first space-delimited token, caps at maxChars, strips trailing
// separators (-,_). Examples:
//   "PLA Basic"  -> "PLA"
//   "PETG HF"    -> "PETG"
//   "PA-CF"      -> "PA-CF" (or "PA-C" at maxChars=4)
//   "PAHT-CF"    -> "PAHT"
static void shortFilamentType(const char* src, char* dst, size_t dstSize,
                              size_t maxChars) {
  size_t cap = (dstSize > 0) ? dstSize - 1 : 0;
  if (cap > maxChars) cap = maxChars;
  size_t i = 0;
  while (src[i] && src[i] != ' ' && i < cap) {
    dst[i] = src[i];
    i++;
  }
  while (i > 0 && (dst[i - 1] == '-' || dst[i - 1] == '_')) i--;
  dst[i] = '\0';
}

// Blend two RGB565 colors. alpha=0 -> a, alpha=255 -> b. Used to derive a
// subtle "white-shifted" highlight color from the tray filament color.
static inline uint16_t blendRgb565(uint16_t a, uint16_t b, uint8_t alpha) {
  uint16_t rA = (a >> 11) & 0x1F, rB = (b >> 11) & 0x1F;
  uint16_t gA = (a >>  5) & 0x3F, gB = (b >>  5) & 0x3F;
  uint16_t bA =  a        & 0x1F, bB =  b        & 0x1F;
  uint16_t r = (rA * (255 - alpha) + rB * alpha) / 255;
  uint16_t g = (gA * (255 - alpha) + gB * alpha) / 255;
  uint16_t bl= (bA * (255 - alpha) + bB * alpha) / 255;
  return (r << 11) | (g << 5) | bl;
}

// Rounded-tech portrait AMS tray. Differs from the legacy sharp-rect bar:
//   - outer rounded shell
//   - bottom-up remain% fill, empty portion uses track color
//   - subtle highlight at the top of the filled area (filament color blended
//     toward white) for a crisp, industrial - not glossy - feel
//   - active tray: 2px white border + small centered red rounded notch on top
//   - inactive tray: 1px dim border
//   - empty tray: rounded outline + diagonal cross
// Portrait path only; the landscape strip still uses the legacy flat bar.
static void drawAmsTrayBarRounded(int16_t x, int16_t y, int16_t w, int16_t h,
                                  const AmsTray& tray, bool isActive) {
  int16_t radius = (w >= 14 && h >= 14) ? 4 : (w >= 8 && h >= 8 ? 3 : 2);

  if (!tray.present) {
    tft.drawRoundRect(x, y, w, h, radius, CLR_TEXT_DARK);
    // Diagonal cross, inset so it does not clip the corner radius
    int16_t inset = radius;
    tft.drawLine(x + inset, y + inset, x + w - 1 - inset, y + h - 1 - inset, CLR_TEXT_DARK);
    tft.drawLine(x + w - 1 - inset, y + inset, x + inset, y + h - 1 - inset, CLR_TEXT_DARK);
    return;
  }

  // Fill empty portion with track color (outer shell), then overlay the
  // filled portion in the filament color.
  tft.fillRoundRect(x, y, w, h, radius, CLR_TRACK);

  bool partial = (tray.remain >= 0 && tray.remain < 100);
  int16_t innerH = h - 2;               // 1px insets for border + fill area
  if (innerH < 0) innerH = 0;
  int16_t fillH = partial ? ((int32_t)innerH * tray.remain / 100) : innerH;
  if (fillH < 0) fillH = 0;
  int16_t fillW = w - 2;
  if (fillW < 0) fillW = 0;

  if (fillH > 0 && fillW > 0) {
    int16_t fx = x + 1;
    int16_t fy = y + 1 + (innerH - fillH);
    // Full-width color bar; lets border strokes sit over it cleanly
    tft.fillRect(fx, fy, fillW, fillH, tray.colorRgb565);
    // Re-round the bottom corners (overpaint with track where the rounded
    // outer shell already gave us the right curve).
    if (radius >= 2) {
      int16_t bottomY = y + h - radius;
      // Only re-round if the fill reaches the bottom (it always does since
      // we paint the whole bottom-up slab)
      if (fy + fillH >= y + h - 1) {
        // Redraw the rounded-rect border later; for now ensure corner pixels
        // match the shell by re-applying fillRoundRect is expensive, so we
        // just rely on the outer border stroke to mask corner artifacts.
      }
    }
    // Subtle highlight at the top of the filled area, derived from the tray
    // color blended ~30% toward white. Crisp, not glossy.
    if (fillH >= 2 && fillW >= 3) {
      uint16_t hi = blendRgb565(tray.colorRgb565, TFT_WHITE, 80);
      tft.drawFastHLine(fx + 1, fy, fillW - 2, hi);
    }
  }

  // Border on top (last) so fill cannot overpaint it
  if (isActive) {
    tft.drawRoundRect(x,     y,     w,     h,     radius,                         TFT_WHITE);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, radius > 1 ? radius - 1 : 1,    TFT_WHITE);
  } else {
    tft.drawRoundRect(x, y, w, h, radius, CLR_TEXT_DARK);
  }

  // Active notch: small centered rounded-red indicator on top edge
  if (isActive) {
    int16_t nw = w / 3;
    if (nw < 6) nw = (w >= 6 ? 6 : w - 2);
    if (nw > w - 6) nw = w - 6;
    if (nw < 3) nw = 3;
    int16_t nh = 3;
    int16_t nx = x + (w - nw) / 2;
    int16_t ny = y - 1;
    // Clear the two pixels above the tray so the notch reads cleanly, then
    // draw the notch sitting half inside the border.
    tft.fillRect(nx, ny, nw, nh, CLR_BG);
    tft.fillRoundRect(nx, ny, nw, nh, 1, CLR_RED);
  }
}

// Legacy sharp-rect AMS tray bar (landscape strip only).
// remain 0-99: color fills bottom portion, CLR_TRACK fills the rest.
// remain 100 or -1 (unknown): full color.
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
                         int16_t zoneY, int16_t zoneH, int16_t barH,
                         int16_t barMaxW,
                         bool showFilamentTypes) {
  uint8_t units = ams.unitCount;
  // Font 2 is 16px tall but our AMS labels sit near the bottom edge of the
  // nominal zone, so their descender rows fall outside zoneH. Clear a few
  // extra rows so toggling between enhanced/default layouts doesn't leave
  // residue pixels below the new layout.
  tft.fillRect(0, zoneY, LY_W, zoneH + 7, CLR_BG);
  if (units == 0 || units > AMS_MAX_UNITS) return;

  const int16_t usableW = LY_W - 2 * LY_AMS_MARGIN;

  // For 1-AMS enhanced view there's ~114px of horizontal slack, so widen the
  // inter-bar gap to avoid a cramped look. 2-AMS extras already fills the row,
  // so stay at the default.
  int16_t barGap = (showFilamentTypes && units == 1) ? 6 : LY_AMS_BAR_GAP;

  // Uniform group width: every group sized for AMS_TRAYS_PER_UNIT bars
  int16_t barW = (usableW - (units - 1) * LY_AMS_GROUP_GAP
                  - units * (AMS_TRAYS_PER_UNIT - 1) * barGap)
                 / (units * AMS_TRAYS_PER_UNIT);
  if (barW > barMaxW) barW = barMaxW;
  if (barW < 4) barW = 4;

  int16_t groupW = barW * AMS_TRAYS_PER_UNIT + (AMS_TRAYS_PER_UNIT - 1) * barGap;
  int16_t totalW = groupW * units + (units - 1) * LY_AMS_GROUP_GAP;
  int16_t startX = (LY_W - totalW) / 2;

  // Layout paths:
  //   normal:   bars centered in zone with AMS label below (offset LY_AMS_LABEL_OFFY)
  //   extras:   bars anchored at zoneY; filament-type row (font1) 3px below
  //             bars; AMS label (font2, same as default) a couple pixels lower
  //             so it reads as "label" rather than "caption".
  int16_t barY, typeY, labelY;
  if (showFilamentTypes) {
    barY   = zoneY;
    typeY  = barY + barH + 3;   // 2px lower than before so names breathe off the bar
    labelY = typeY + 11;        // 8px font1 type row + 3px gap before font2 label
  } else {
    barY   = zoneY + (zoneH - barH - LY_AMS_LABEL_OFFY - 8) / 2;
    labelY = barY + barH + LY_AMS_LABEL_OFFY;
    typeY  = 0;  // unused
  }

  for (uint8_t u = 0; u < units; u++) {
    int16_t groupX = startX + u * (groupW + LY_AMS_GROUP_GAP);

    uint8_t tc = ams.units[u].trayCount;
    if (tc == 0) tc = AMS_TRAYS_PER_UNIT;

    // Center actual bars within the uniform group slot
    int16_t barsW = tc * barW + (tc - 1) * barGap;
    int16_t barsX = groupX + (groupW - barsW) / 2;

    for (uint8_t t = 0; t < tc; t++) {
      uint8_t trayIdx = u * AMS_TRAYS_PER_UNIT + t;
      int16_t bx = barsX + t * (barW + barGap);
      drawAmsTrayBarRounded(bx, barY, barW, barH,
                            ams.trays[trayIdx], trayIdx == ams.activeTray);

      if (showFilamentTypes) {
        const AmsTray& tray = ams.trays[trayIdx];
        char typeBuf[6];
        if (tray.present && tray.type[0]) {
          // Cap at 4 chars to guarantee fit under the 26/25px bars
          shortFilamentType(tray.type, typeBuf, sizeof(typeBuf), 4);
        } else {
          typeBuf[0] = '\0';
        }
        tft.setTextDatum(TC_DATUM);
        tft.setTextFont(1);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        if (typeBuf[0]) {
          tft.drawString(typeBuf, bx + barW / 2, typeY);
        }
      }
    }

    char label[6];
    snprintf(label, sizeof(label), "AMS %c", 'A' + u);
    tft.setTextDatum(TC_DATUM);
    bool sm = dispSettings.smallLabels;
    tft.setTextFont(sm ? 1 : 2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(label, groupX + groupW / 2, labelY + (showFilamentTypes ? 0 : 2));
  }
}

// ---------------------------------------------------------------------------
//  Enhanced portrait AMS layout
//    - enabled when per-printer extras flag is on AND 1 or 2 AMS units
//    - draws wider tray bars (rectangular, not square) and a filament-type
//      label under each tray. For 3+ AMS the bars are too narrow for text
//      to fit cleanly, so we fall back to the default compact layout.
// ---------------------------------------------------------------------------
// Enhanced portrait AMS is used for 1-2 AMS units (wider bars + filament-type
// labels). With 3+ units there isn't enough horizontal room for readable
// filament labels, so we fall back to the compact default layout.
static bool useEnhancedPortraitAms(const AmsState& ams) {
  return ams.unitCount >= 1 && ams.unitCount <= 2;
}

static void drawAmsZone(const BambuState& s, bool force) {
  // --- Change detection ---
  bool landscape = isLandscape();
  bool enhanced = !landscape && useEnhancedPortraitAms(s.ams);

  // In landscape the right column also hosts the gcode-state badge — track
  // it so the badge refreshes on state transition even without a global
  // forceRedraw.
  static uint8_t prevAmsGcodeStateId = 0xFF;
  static char    prevAmsGcodeStateText[16] = "";
  bool badgeChanged = landscape && (prevAmsGcodeStateId != s.gcodeStateId ||
                                    strncmp(prevAmsGcodeStateText, s.gcodeState, 15) != 0);

  bool amsChanged = force || badgeChanged;
  if (!amsChanged) {
    amsChanged = (s.ams.unitCount != prevAmsUnitCount) ||
                 (s.ams.activeTray != prevAmsActive);
    if (!amsChanged) {
      for (uint8_t i = 0; i < s.ams.unitCount * AMS_TRAYS_PER_UNIT && !amsChanged; i++) {
        amsChanged = (s.ams.trays[i].present != prevAmsTrayPresent[i]) ||
                     (s.ams.trays[i].colorRgb565 != prevAmsTrayColors[i]) ||
                     (s.ams.trays[i].remain != prevAmsTrayRemain[i]);
        if (!amsChanged && enhanced) {
          amsChanged = strncmp(s.ams.trays[i].type, prevAmsTrayTypes[i], 16) != 0;
        }
      }
    }
  }

  if (!amsChanged) return;

  prevAmsGcodeStateId = s.gcodeStateId;
  strncpy(prevAmsGcodeStateText, s.gcodeState, 15);
  prevAmsGcodeStateText[15] = '\0';

  // Save state for next comparison (AMS trays)
  prevAmsUnitCount = s.ams.unitCount;
  prevAmsActive    = s.ams.activeTray;
  for (uint8_t i = 0; i < AMS_MAX_TRAYS; i++) {
    prevAmsTrayPresent[i] = s.ams.trays[i].present;
    prevAmsTrayColors[i]  = s.ams.trays[i].colorRgb565;
    prevAmsTrayRemain[i]  = s.ams.trays[i].remain;
    strncpy(prevAmsTrayTypes[i], s.ams.trays[i].type, 15);
    prevAmsTrayTypes[i][15] = '\0';
  }

  uint8_t units = s.ams.unitCount;

  if (landscape) {
    // =====================================================================
    //  LANDSCAPE: right column = status badge (top) + AMS strip (below)
    //  AMS groups stacked vertically, each group has VERTICAL bars
    //  side-by-side (same orientation as portrait / physical AMS).
    // =====================================================================
    // 0-2 AMS leaves room for the bottom bar to extend full 320px so the
    // AMS column ends higher (BOT_SHORT). 3-4 AMS keeps the bottom bar at
    // 240px and lets AMS run all the way down (BOT_FULL).
    const int16_t amsBot = landBottomBarFullWidth(units)
                           ? LY_LAND_AMS_BOT_SHORT
                           : LY_LAND_AMS_BOT_FULL;

    // --- Status badge (always present in landscape) ---
    // Drawn before clearing AMS area so its 4px outline-clear doesn't bleed
    // into the badge row.
    {
      uint16_t badgeColor = CLR_TEXT_DIM;
      if (s.gcodeStateId == GCODE_RUNNING)      badgeColor = CLR_GREEN;
      else if (s.gcodeStateId == GCODE_PAUSE)   badgeColor = CLR_YELLOW;
      else if (s.gcodeStateId == GCODE_FAILED)  badgeColor = CLR_RED;
      else if (s.gcodeStateId == GCODE_PREPARE) badgeColor = CLR_BLUE;

      const int16_t bx = LY_LAND_AMS_X - 4;
      const int16_t bw = LY_LAND_AMS_W + 8;
      tft.fillRect(bx, LY_LAND_BADGE_Y, bw, LY_LAND_BADGE_H, dispSettings.bgColor);

      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.setTextColor(badgeColor, dispSettings.bgColor);
      const int16_t cx = LY_LAND_AMS_X + LY_LAND_AMS_W / 2;
      // Dot + label centered in the right column.
      int16_t tw = tft.textWidth(s.gcodeState);
      tft.fillCircle(cx - tw / 2 - 6, LY_LAND_BADGE_CY, 3, badgeColor);
      tft.drawString(s.gcodeState, cx + 4, LY_LAND_BADGE_CY);
    }

    // --- AMS bars area ---
    tft.fillRect(LY_LAND_AMS_X - 4, LY_LAND_AMS_TOP, LY_LAND_AMS_W + 8,
                 amsBot - LY_LAND_AMS_TOP, CLR_BG);

    if (units == 0 || units > AMS_MAX_UNITS) return;

    const int16_t totalH = amsBot - LY_LAND_AMS_TOP;
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
        // Use the same rounded helper as the portrait refresh — rounded
        // shell, remain-fill, white outline + red notch on the active tray.
        drawAmsTrayBarRounded(bx, gy, barW, barH,
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

  } else if (enhanced) {
    // Portrait enhanced layout: wider tray bars + filament-type labels
    drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H,
                 LY_AMS_BAR_MAX_W_EXTRAS, /*showFilamentTypes=*/true);
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

  // Track AMS unit-count transitions that change layout zones (badge moves
  // between header and right column at units 0↔1; bottom bar width flips at
  // units 2↔3 in landscape).
#if defined(DISPLAY_240x320)
  static uint8_t prevPrintingUnits = 0xFF;
  bool unitsZoneChanged = (prevPrintingUnits == 0xFF) ||
                          ((prevPrintingUnits >= 1) != (s.ams.unitCount >= 1)) ||
                          ((prevPrintingUnits <= 2) != (s.ams.unitCount <= 2));
  prevPrintingUnits = s.ams.unitCount;
  if (unitsZoneChanged) {
    stateChanged = true;   // forces header repaint with new badge layout
    etaChanged   = true;   // forces ETA clear at new width
  }
#else
  const bool unitsZoneChanged = false;
#endif

  // 2x3 gauge grid constants (from layout profile)
  const int16_t gR = LY_GAUGE_R;
  const int16_t gT = LY_GAUGE_T;

  // Effective Y positions — landscape on CYD uses 240x240-style positions
#if defined(DISPLAY_240x320)
  const bool land = isLandscape();
  const uint8_t units = s.ams.unitCount;
  // Right column (badge + AMS) only used in landscape with at least one AMS.
  const bool landAmsCol = land && units >= 1;
  // Bottom bar / ETA span the full 320 only when right column doesn't need
  // to extend down to the screen edge (0..2 AMS, or no AMS).
  const bool botFull = land && landBottomBarFullWidth(units);
  const int16_t eff_etaY     = land ? LY_LAND_ETA_Y     : LY_ETA_Y;
  const int16_t eff_etaH     = land ? LY_LAND_ETA_H     : LY_ETA_H;
  const int16_t eff_etaTextY = land ? LY_LAND_ETA_TEXT_Y : LY_ETA_TEXT_Y;
  const int16_t eff_botY     = land ? LY_LAND_BOT_Y     : LY_BOT_Y;
  const int16_t eff_botH     = land ? LY_LAND_BOT_H     : LY_BOT_H;
  const int16_t eff_botCY    = land ? LY_LAND_BOT_CY    : LY_BOT_CY;
  // Width of the horizontal strip used for header / ETA / bottom bar.
  // Header is always full canvas; ETA/bottom shrink to 240 when AMS column
  // needs the full vertical extent (3-4 AMS).
  const int16_t hdrW   = land ? uiW() : SCREEN_W;
  const int16_t etaW   = (land && !botFull) ? LY_LAND_GAUGE_W : (land ? uiW() : SCREEN_W);
  const int16_t botW   = (land && !botFull) ? LY_LAND_GAUGE_W : (land ? uiW() : SCREEN_W);
#else
  const bool landAmsCol = false;
  const int16_t eff_etaY     = LY_ETA_Y;
  const int16_t eff_etaH     = LY_ETA_H;
  const int16_t eff_etaTextY = LY_ETA_TEXT_Y;
  const int16_t eff_botY     = LY_BOT_Y;
  const int16_t eff_botH     = LY_BOT_H;
  const int16_t eff_botCY    = LY_BOT_CY;
  const int16_t hdrW = SCREEN_W;
  const int16_t etaW = SCREEN_W;
  const int16_t botW = SCREEN_W;
#endif

  // === CYD: clear unused zone on screen transitions ===
#if defined(DISPLAY_240x320)
  {
    int16_t scrW = (int16_t)tft.width();
    int16_t scrH = (int16_t)tft.height();
    if (forceRedraw) {
      // In portrait 240x320 the canvas is 240 wide so this is a no-op. In
      // landscape (320 wide) we wipe the area drawAmsZone is responsible for
      // before AMS rendering takes over.
      if (scrW > LY_LAND_GAUGE_W) {
        tft.fillRect(LY_LAND_GAUGE_W, 0, scrW - LY_LAND_GAUGE_W, scrH, CLR_BG);
      }
      // Clear below content area if canvas taller than used
      int16_t usedBottom = eff_botY + eff_botH;
      if (usedBottom < scrH)
        tft.fillRect(0, usedBottom, scrW, scrH - usedBottom, CLR_BG);
    } else if (land && unitsZoneChanged && s.ams.unitCount == 0) {
      // 1+ AMS → 0 AMS in landscape: drawAmsZone will not be called this
      // frame (gated on unitCount > 0), so wipe the right column ourselves
      // so the old badge + AMS bars don't linger.
      tft.fillRect(LY_LAND_GAUGE_W, LY_HDR_Y + LY_HDR_H,
                   scrW - LY_LAND_GAUGE_W,
                   eff_botY - (LY_HDR_Y + LY_HDR_H), CLR_BG);
    }
  }
#endif

  // === H2-style LED progress bar (y=0-5) ===
  if (progChanged) {
    drawLedProgressBar(tft, 0, s.progress);
  }

  // === Header bar (y=7-25) ===
  // In landscape with AMS column the badge is rendered separately by
  // drawAmsZone in the right column, so the header carries only printer
  // name + multi-printer dots.
  if (forceRedraw || stateChanged) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, LY_HDR_Y, hdrW, LY_HDR_H, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, LY_HDR_NAME_X, LY_HDR_CY);

    // State badge (right) — only when right column not used for it
    if (!landAmsCol) {
      uint16_t badgeColor = CLR_TEXT_DIM;
      if (s.gcodeStateId == GCODE_RUNNING) badgeColor = CLR_GREEN;
      else if (s.gcodeStateId == GCODE_PAUSE) badgeColor = CLR_YELLOW;
      else if (s.gcodeStateId == GCODE_FAILED) badgeColor = CLR_RED;
      else if (s.gcodeStateId == GCODE_PREPARE) badgeColor = CLR_BLUE;

      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(badgeColor, hdrBg);
      tft.setTextFont(2);
      tft.fillCircle(hdrW - LY_HDR_BADGE_RX - tft.textWidth(s.gcodeState) - 10, LY_HDR_CY, 4, badgeColor);
      tft.drawString(s.gcodeState, hdrW - LY_HDR_BADGE_RX, LY_HDR_CY);
    }

    // Printer indicator dots (multi-printer) — centered on header strip
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(hdrW / 2 - 5 + di * 10, LY_HDR_DOT_CY, 3, dotClr);
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
    const int16_t etaCx = etaW / 2;
    tft.fillRect(0, eff_etaY, etaW, eff_etaH, CLR_BG);
    tft.setTextDatum(MC_DATUM);

    if (s.gcodeStateId == GCODE_PAUSE) {
      tft.setTextFont(4);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString("PAUSED", etaCx, eff_etaTextY);
    } else if (s.gcodeStateId == GCODE_FAILED) {
      tft.setTextFont(4);
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString("ERROR!", etaCx, eff_etaTextY);
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
        tft.drawString(etaBuf, etaCx, eff_etaTextY);
      } else {
        // NTP not synced yet OR user requested remaining time - show remaining time only
        char remBuf[24];
        uint16_t h = s.remainingMinutes / 60;
        uint16_t m = s.remainingMinutes % 60;
        snprintf(remBuf, sizeof(remBuf), "Remaining: %dh %02dm", h, m);
        tft.setTextFont(4);
        tft.setTextColor(CLR_TEXT, CLR_BG);
        tft.drawString(remBuf, etaCx, eff_etaTextY);
      }
    } else {
      tft.setTextFont(4);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("ETA: ---", etaCx, eff_etaTextY);
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
  bool bottomChanged = forceRedraw || unitsZoneChanged ||
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
    const int16_t botCx = botW / 2;
    // When AMS column doesn't extend down to the bar, wipe the right edge
    // too so a previous wider bar (e.g. after AMS unit count dropped) doesn't
    // leak pixels under the AMS zone.
#if defined(DISPLAY_240x320)
    if (botW < uiW()) {
      // The AMS zone clears x=240..324 from y=AMS_TOP..AMS_BOT_FULL — but the
      // 4px gap below AMS_BOT_FULL inside the bottom-bar Y range is ours.
      tft.fillRect(botW, eff_botY, uiW() - botW, eff_botH, CLR_BG);
    }
#endif
    tft.fillRect(0, eff_botY, botW, eff_botH, CLR_BG);
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
      drawIcon16(tft, botCx - 20, eff_botCY - 8, icon_lightning, CLR_YELLOW);
      char wBuf[8];
      snprintf(wBuf, sizeof(wBuf), "%.0fW", tasmotaGetWatts());
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(wBuf, botCx - 2, eff_botCY);
    } else {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      char layerBuf[20];
      snprintf(layerBuf, sizeof(layerBuf), "L%d/%d", s.layerNum, s.totalLayers);
      tft.drawString(layerBuf, botCx, eff_botCY);
    }

    // Right: door status (if sensor present) or speed mode
    if (s.doorSensorPresent) {
      uint16_t clr = s.doorOpen ? CLR_ORANGE : CLR_GREEN;
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(clr, CLR_BG);
      tft.drawString("Door", botW - 20, eff_botCY);
      drawIcon16(tft, botW - 18, eff_botCY - 8,
                 s.doorOpen ? icon_unlock : icon_lock, clr);
    } else {
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(speedLevelColor(s.speedLevel), CLR_BG);
      tft.drawString(speedLevelName(s.speedLevel), botW - 4, eff_botCY);
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
  // Landscape gauge / text positions are tighter to fit a 320x240 canvas.
  const int16_t gR        = LY_FIN_GAUGE_R;
  const int16_t gaugeLeft  = land ? LY_LAND_FIN_GL    : LY_FIN_GL;
  const int16_t gaugeRight = land ? LY_LAND_FIN_GR    : LY_FIN_GR;
  const int16_t gaugeY     = land ? LY_LAND_FIN_GY    : LY_FIN_GY;
  const int16_t finTextY   = land ? LY_LAND_FIN_TEXT_Y : LY_FIN_TEXT_Y;
  const int16_t finFileY   = land ? LY_LAND_FIN_FILE_Y : LY_FIN_FILE_Y;
  const int16_t finKwhY    = land ? LY_LAND_FIN_KWH_Y  : LY_FIN_KWH_Y;
#else
  const int16_t scrW = SCREEN_W;
  const int16_t eff_finBotY  = LY_FIN_BOT_Y;
  const int16_t eff_finBotH  = LY_FIN_BOT_H;
  const int16_t eff_finWifiY = LY_FIN_WIFI_Y;
  const int16_t gR        = LY_FIN_GAUGE_R;
  const int16_t gaugeLeft  = LY_FIN_GL;
  const int16_t gaugeRight = LY_FIN_GR;
  const int16_t gaugeY     = LY_FIN_GY;
  const int16_t finTextY   = LY_FIN_TEXT_Y;
  const int16_t finFileY   = LY_FIN_FILE_Y;
  // 240x240 layout has no dedicated KWH Y — derive it midway between file
  // and bottom bar so the clear band sits between them.
  const int16_t finKwhY    = (LY_FIN_FILE_Y + eff_finBotY) / 2;
#endif
  const int16_t cx = scrW / 2;

  bool animating = tickGaugeSmooth(s, forceRedraw);
  bool tempChanged = forceRedraw || animating ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);

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
    tft.drawString("Print Complete!", cx, finTextY);
  }

  // === File name ===
  if (forceRedraw) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    if (s.subtaskName[0] != '\0') {
      // Trim to canvas width (font 2 ~9px/char nominal). 25 chars suited 240
      // portrait but landscape (320) can fit more — adapt to actual width by
      // shrinking until the rendered string fits in `scrW - 16`.
      char truncName[64];
      strncpy(truncName, s.subtaskName, sizeof(truncName) - 1);
      truncName[sizeof(truncName) - 1] = '\0';
      const int16_t maxW = scrW - 16;
      while (tft.textWidth(truncName) > maxW) {
        size_t n = strlen(truncName);
        if (n <= 1) break;
        truncName[n - 1] = '\0';
      }
      tft.drawString(truncName, cx, finFileY);
    }
  }

  // === kWh used during print (between filename and bottom bar) ===
  // Issue #72: in landscape the previous formula (LY_FIN_FILE_Y +
  // eff_finBotY)/2 produced a clear band that overlapped the file name.
  // Now we use the explicit landscape KWH Y which sits below the file.
  bool tasmotaActiveHere = tasmotaIsActiveForSlot(rotState.displayIndex);
  float finishKwh = tasmotaActiveHere ? tasmotaGetPrintKwhUsed() : -1.0f;
  bool kwhChanged = (tasmotaActiveHere && tasmotaKwhChanged()) ||
                    (tasmotaActiveHere != prevFinTasmotaOnline) ||
                    (finishKwh != prevFinKwh);
  if (forceRedraw || kwhChanged) {
    const int16_t kwhY = finKwhY;
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
      // Finished screen zone is too short (45px, barH=26) to fit filament-type
      // labels comfortably. For a single AMS we still shrink the bar cap so it
      // renders as a rectangle rather than near-square.
      int16_t finCap = (s.ams.unitCount == 1) ? 20 : LY_AMS_BAR_MAX_W;
      drawAmsStrip(s.ams, LY_FIN_AMS_Y, LY_FIN_AMS_H, LY_FIN_AMS_BAR_H, finCap);
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
