#include "display_ui.h"
#include "display_gauges.h"
#include "display_split.h"
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
#include "fonts.h"
#include "battery.h"
#include <WiFi.h>
#include <time.h>
#if defined(BOARD_IS_SENSECAP) || defined(BOARD_IS_WS350)
#include <Wire.h>     // SenseCAP: PCA9535PW IO expander / ws_lcd_350: TCA9554 LCD reset
#endif
#include <new>   // placement new for CYD panel variant selection

// LovyanGFX board-specific configurations - the 12 LGFX device classes and the
// per-board `_tft_instance` - live in their own header to keep this file
// focused on UI logic. Included exactly once, here. See src/lgfx_boards.h.
#include "lgfx_boards.h"

// Global pointer + reference — accessed via `tft` throughout the codebase.
// For CYD, _tft_instance is backed by a union (see _tft_storage) that is
// populated with either the V2 or Classic panel in initDisplay(), so method
// calls via this reference/pointer dispatch to whichever variant was chosen.
lgfx::LovyanGFX* tft_ptr = &_tft_instance;
// `tft` is now a macro in display_ui.h — `#define tft (*tft_ptr)` — so
// every call site re-dereferences the pointer and picks up runtime
// retargeting to the JC3248W535 PSRAM sprite.

// Direct panel pointer for JC3248W535 sprite escape-hatch; nullptr on all
// other boards so the extern declaration in display_ui.h is always satisfied.
#if defined(BOARD_IS_JC3248W535)
lgfx::Panel_AXS15231B_AGFX* g_axs_panel = _tft_instance.panelAXS();

// Full-frame PSRAM sprite. All BambuHelper draws are redirected here in
// initDisplay() (via tft_ptr), then flushed to the panel once per loop()
// tick via flushFrame(). The AXS15231B in QSPI mode cannot address
// arbitrary Y per draw (see lgfx_panel_axs15231b_agfx.hpp), so a
// framebuffer-and-single-raster-flush is the only reliable render path.
static lgfx::LGFX_Sprite _frame_sprite(&_tft_instance);

// Dirty flag: start true so the very first flushFrame() pushes the cleared
// sprite + splash. Redraw sites call markFrameDirty() to request another
// push. A keepalive in flushFrame() also forces one push every
// FRAME_KEEPALIVE_MS as a safety net against missed dirty marks.
static bool g_frame_dirty = true;
static unsigned long g_last_flush_ms = 0;
static const unsigned long FRAME_KEEPALIVE_MS = 500;
#else
lgfx::Panel_AXS15231B_AGFX* g_axs_panel = nullptr;
#endif

void markFrameDirty() {
#if defined(BOARD_IS_JC3248W535)
  g_frame_dirty = true;
#endif
}

void flushFrame() {
#if defined(BOARD_IS_JC3248W535)
  if (!g_axs_panel || !_frame_sprite.getBuffer()) return;
  unsigned long now = millis();
  bool keepalive_due = (now - g_last_flush_ms) >= FRAME_KEEPALIVE_MS;
  if (!g_frame_dirty && !keepalive_due) return;
  g_axs_panel->pushRawPixels(
    static_cast<uint16_t*>(_frame_sprite.getBuffer()),
    320u * 480u);
  g_frame_dirty = false;
  g_last_flush_ms = now;
#endif
}

// Pass-through hook for any future board-level rotation constraints. All four
// rotations are supported on every current board; JC3248W535 applies rotation
// at the sprite level (panel MADCTL stays at 0) while CYD/ws_lcd_200 use real
// hardware MADCTL via LovyanGFX setRotation().
static uint8_t sanitizeRotation(uint8_t r) {
  return r;
}

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

// Battery indicator cache: forces a bottom-bar redraw when the icon's visible
// state, percentage, or critical-blink phase changes. Without this, hot-plug
// or web-UI toggle wouldn't refresh the bar until the next forced redraw.
static bool    prevBatShown          = false;
static uint8_t prevBatPercent        = 0;
static bool    prevBatCriticalBlink  = false;
static inline void resetBatteryRedrawCache() {
  prevBatShown          = false;
  prevBatPercent        = 0;
  prevBatCriticalBlink  = false;
}
static bool    batteryStateChanged() {
  bool shown = dispSettings.showBatteryIndicator && Battery::isPresent();
  uint8_t pct = Battery::percent();
  bool blink = Battery::isCritical() ? ((millis() / 500) & 1) != 0 : false;
  bool changed = (shown != prevBatShown) ||
                 (shown && pct != prevBatPercent) ||
                 (shown && Battery::isCritical() && blink != prevBatCriticalBlink);
  prevBatShown          = shown;
  prevBatPercent        = pct;
  prevBatCriticalBlink  = blink;
  return changed;
}

// ---------------------------------------------------------------------------
//  Smooth gauge interpolation - values lerp toward MQTT actuals each frame
// ---------------------------------------------------------------------------
static float smoothNozzleTemp   = 0;
static float smoothNozzleTempN[2] = {0, 0};  // dual-nozzle fixed L/R gauges
static float smoothBedTemp      = 0;
static float smoothPartFan     = 0;
static float smoothAuxFan      = 0;
static float smoothAuxRightFan = 0;
static float smoothChamberFan  = 0;
static float smoothExhaustFan  = 0;
static float smoothChamberTemp = 0;
static float smoothHeatbreakFan= 0;
static bool  smoothInited      = false;

static bool gaugesAnimating = false;       // true while arcs are interpolating
static const unsigned long GAUGE_ANIM_MS = 80; // ~12 Hz during animation

// Per-frame easing factor (at 12Hz). Index by dispSettings.gaugeSmoothing:
// 0=Off(instant), 1=Slow(~2s), 2=Normal(~1s, default), 3=Fast(~0.4s).
static const float SMOOTH_ALPHAS[4] = { 1.0f, 0.05f, 0.09f, 0.18f };
static const float SNAP_THRESH  = 0.5f;   // snap when within 0.5 of target

static void smoothLerp(float& cur, float target) {
  uint8_t mode = dispSettings.gaugeSmoothing <= 3 ? dispSettings.gaugeSmoothing : 2;
  float diff = target - cur;
  if (fabsf(diff) < SNAP_THRESH) cur = target;
  else cur += diff * SMOOTH_ALPHAS[mode];
}

// Returns true if any gauge is still animating
static bool tickGaugeSmooth(const BambuState& s, bool snap) {
  if (snap || !smoothInited) {
    smoothNozzleTemp   = s.nozzleTemp;
    smoothNozzleTempN[0] = s.nozzleTempN[0];
    smoothNozzleTempN[1] = s.nozzleTempN[1];
    smoothBedTemp      = s.bedTemp;
    smoothPartFan      = s.coolingFanPct;
    smoothAuxFan       = s.auxFanPct;
    smoothAuxRightFan  = s.auxFanRightPct;
    smoothChamberFan   = s.chamberFanPct;
    smoothExhaustFan   = s.exhaustFanPct;
    smoothChamberTemp  = s.chamberTemp;
    smoothHeatbreakFan = s.heatbreakFanPct;
    smoothInited = true;
    return false;
  }
  smoothLerp(smoothNozzleTemp,   s.nozzleTemp);
  smoothLerp(smoothNozzleTempN[0], s.nozzleTempN[0]);
  smoothLerp(smoothNozzleTempN[1], s.nozzleTempN[1]);
  smoothLerp(smoothBedTemp,      s.bedTemp);
  smoothLerp(smoothPartFan,      (float)s.coolingFanPct);
  smoothLerp(smoothAuxFan,       (float)s.auxFanPct);
  smoothLerp(smoothAuxRightFan,  (float)s.auxFanRightPct);
  smoothLerp(smoothChamberFan,   (float)s.chamberFanPct);
  smoothLerp(smoothExhaustFan,   (float)s.exhaustFanPct);
  smoothLerp(smoothChamberTemp,  s.chamberTemp);
  smoothLerp(smoothHeatbreakFan, (float)s.heatbreakFanPct);

  const float ANIM_EPS = 0.01f;
  return (fabsf(smoothNozzleTemp   - s.nozzleTemp)              > ANIM_EPS) ||
         (fabsf(smoothNozzleTempN[0] - s.nozzleTempN[0])        > ANIM_EPS) ||
         (fabsf(smoothNozzleTempN[1] - s.nozzleTempN[1])        > ANIM_EPS) ||
         (fabsf(smoothBedTemp      - s.bedTemp)                 > ANIM_EPS) ||
         (fabsf(smoothPartFan      - (float)s.coolingFanPct)    > ANIM_EPS) ||
         (fabsf(smoothAuxFan       - (float)s.auxFanPct)        > ANIM_EPS) ||
         (fabsf(smoothAuxRightFan  - (float)s.auxFanRightPct)   > ANIM_EPS) ||
         (fabsf(smoothChamberFan   - (float)s.chamberFanPct)    > ANIM_EPS) ||
         (fabsf(smoothExhaustFan   - (float)s.exhaustFanPct)    > ANIM_EPS) ||
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
#if defined(LAYOUT_HAS_LANDSCAPE)
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

// Split (dual-printer) screen is only offered on portrait profiles that define
// LAYOUT_HAS_SPLIT, and only while not in a landscape rotation. Compiled-out
// boards (e.g. 480x480 SenseCAP) return false so the feature stays inert.
bool displaySupportsSplit() {
#if defined(LAYOUT_HAS_SPLIT)
  return !isLandscape();
#else
  return false;
#endif
}

bool isDisplayForceRedraw() { return forceRedraw; }

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initDisplay() {

#if defined(BOARD_IS_WS350)
  // The ST7796 reset line is not on a GPIO - it is driven by the TCA9554 I2C
  // IO expander (@0x20, expander P1). Bring up the shared I2C bus (also used
  // by the FT6336 touch controller) and pulse reset before tft.init(), exactly
  // as the Waveshare demo's lcd_reset() does. The panel class sets pin_rst=-1
  // so LovyanGFX never tries to toggle a (nonexistent) reset GPIO itself.
  {
    const uint8_t  TCA_ADDR    = 0x20;
    const uint8_t  TCA_RST_BIT = 1;       // expander P1 = LCD reset
    const uint8_t  TCA_REG_OUT = 0x01;    // TCA9554 Output Port register
    const uint8_t  TCA_REG_CFG = 0x03;    // TCA9554 Configuration register (1=in, 0=out)
    Wire.begin(8 /*SDA*/, 7 /*SCL*/);
    Wire.setClock(400000);
    auto tcaWrite = [&](uint8_t reg, uint8_t val) {
      Wire.beginTransmission(TCA_ADDR);
      Wire.write(reg);
      Wire.write(val);
      Wire.endTransmission();
    };
    // Drive only P1 as an output; leave every other expander pin as an input
    // (high-Z), matching the demo which only touches P1.
    tcaWrite(TCA_REG_CFG, (uint8_t)~(1 << TCA_RST_BIT));
    tcaWrite(TCA_REG_OUT, (1 << TCA_RST_BIT));   // high
    delay(10);
    tcaWrite(TCA_REG_OUT, 0);                    // low
    delay(10);
    tcaWrite(TCA_REG_OUT, (1 << TCA_RST_BIT));   // high
    delay(200);                                  // ST7796 settle after reset
  }
#endif
#if defined(BOARD_IS_SENSECAP)
  // Initialize PCA9535PW I2C IO expander before display init.
  // The SenseCAP Indicator routes display CS and RESET through this expander
  // since they can't be connected directly to ESP32-S3 GPIOs.
  Wire.begin(PCA9535_I2C_SDA, PCA9535_I2C_SCL, 400000);

  // Configure expander pins: P04 (DISP_CS), P05 (DISP_RST), P07 (TOUCH_RST) as outputs
  // P06 (TOUCH_INT) stays as input. Write 0xBF to config register (bit 6 = 1 = input)
  // PCA9535 register map: 0x06=Configuration Port 0, 0x02=Output Port 0
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x06);  // Configuration register (port 0)
  Wire.write(0x40);  // P06=input, rest=output
  Wire.endTransmission();

  // Start with CS HIGH (deselected), RST HIGH (not in reset), TOUCH_RST HIGH
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x02);  // Output register (port 0)
  Wire.write((1 << PCA9535_PIN_DISP_CS) | (1 << PCA9535_PIN_DISP_RST) | (1 << PCA9535_PIN_TOUCH_RST));
  Wire.endTransmission();
  delay(10);

  // Hardware reset: pull RST LOW for 10ms then HIGH
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x02);  // Output register (port 0)
  Wire.write((1 << PCA9535_PIN_DISP_CS) | (1 << PCA9535_PIN_TOUCH_RST));  // RST LOW
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x02);  // Output register (port 0)
  Wire.write((1 << PCA9535_PIN_DISP_CS) | (1 << PCA9535_PIN_DISP_RST) | (1 << PCA9535_PIN_TOUCH_RST));  // RST HIGH
  Wire.endTransmission();
  delay(120);  // ST7701S needs time after reset

  // Pull CS LOW for SPI init commands. LovyanGFX uses IO_EXPANDER-aware GPIO
  // for pin_cs=(4|IO_EXPANDER) when USE_ARDUINO_HAL_GPIO is defined.
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x02);  // Output register (port 0)
  Wire.write((0 << PCA9535_PIN_DISP_CS) | (1 << PCA9535_PIN_DISP_RST) | (1 << PCA9535_PIN_TOUCH_RST));  // CS LOW
  Wire.endTransmission();
  delay(1);
#else
  Serial.println("Display: pre-init delay...");
  delay(500);
#endif
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
#elif defined(USE_ST7789_INVERT)
  _tft_instance.invertDisplay(true);  // ST7789 panels need color inversion
#elif defined(BOARD_IS_SENSECAP)
  // ST7701S IPS inversion already handled by default Panel_ST7701 init (0x21 command).
  // Release SPI CS HIGH now that init commands are done
  Wire.beginTransmission(PCA9535_ADDR);
  Wire.write(0x02);  // Output register (port 0)
  Wire.write((1 << PCA9535_PIN_DISP_CS) | (1 << PCA9535_PIN_DISP_RST) | (1 << PCA9535_PIN_TOUCH_RST));  // CS HIGH
  Wire.endTransmission();
#endif
  Serial.println("Display: tft.init() done");
#if defined(DISPLAY_240x320)
  // Clear entire GRAM at rotation 0 first (guarantees all 240x320 pixels
  // are addressed). Without this, rotations 1/3 leave 80px of uninitialized
  // VRAM visible as garbage noise on the extra screen edge.
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
#if defined(BOARD_IS_JC3248W535)
  // Panel MADCTL stays at 0 forever — RASET-skip + LSB-first byte-order
  // invariants in pushRawPixels depend on native orientation. User-facing
  // rotation is applied to the PSRAM sprite after tft_ptr is redirected.
  tft.setRotation(0);
#else
  tft.setRotation(dispSettings.rotation);
#endif
#if defined(DISPLAY_CYD)
  applyCydPanelInversion();
#elif defined(DISPLAY_240x320)
  if (dispSettings.invertColors) _tft_instance.invertDisplay(false);
#endif
  Serial.println("Display: setRotation done");
  tft.fillScreen(CLR_BG);
  Serial.println("Display: fillScreen done");

#if defined(BOARD_IS_JC3248W535)
  // Allocate 320x480x16bpp PSRAM sprite (300 KB) and redirect tft_ptr so all
  // subsequent draws (splash, UI, refreshes) render into the sprite buffer.
  // Panel cannot address arbitrary Y in QSPI mode — instead we flush the
  // whole sprite to the panel once per loop tick via flushFrame().
  _frame_sprite.setPsram(true);
  _frame_sprite.setColorDepth(16);
  if (_frame_sprite.createSprite(320, 480)) {
    _frame_sprite.setTextDatum(MC_DATUM);  // match the tft defaults used below
    tft_ptr = &_frame_sprite;
    Serial.printf("Display: frame sprite 320x480 allocated in PSRAM, free=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    tft.setRotation(sanitizeRotation(dispSettings.rotation));
    tft.fillScreen(CLR_BG);
    flushFrame();  // push cleared sprite so panel shows CLR_BG during splash
  } else {
    Serial.println("Display: frame sprite alloc FAILED — will draw direct to panel (expect artifacts)");
  }
#endif


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
  resetBatteryRedrawCache();

  // Splash screen — center on actual canvas (rotation-aware for 240x320)
  {
    const int16_t sw = uiW();
    const int16_t sh = uiH();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    setFont(tft, FONT_LARGE);
    tft.drawString("BambuHelper", sw / 2, sh / 2 - 20);
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString("Printer Monitor", sw / 2, sh / 2 + 10);
    setFont(tft, FONT_SMALL);
    tft.drawString(FW_VERSION, sw / 2, sh / 2 + 30);
  }
}

// Repaint helper: the clock and the pong screensaver each keep a private
// digit/frame cache and deliberately ignore forceRedraw. So any site that
// fillScreen()s the panel while the clock is on screen must also drop that
// cache, or the clock paints blank until its next digit/colon roll. Centralized
// here so every screen-clear path stays correct without copy-pasting the
// SCREEN_CLOCK block (and so a future clear site can't forget it).
static void resetActiveClockCache() {
  if (currentScreen != SCREEN_CLOCK) return;
  if (dispSettings.pongClock) resetPongClock();
  else resetClock();
}

void applyDisplaySettings() {
#if defined(DISPLAY_240x320)
  // Pre-clear entire GRAM at rotation 0 to prevent garbage on edges
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
#endif
#if defined(BOARD_IS_JC3248W535)
  // Sprite path: panel MADCTL stays at 0, but the 320x480 PSRAM sprite has
  // stale pixels at the "extra" edge when flipping between portrait and
  // landscape. Wipe the whole sprite at the current rotation before applying
  // the new one so no garbage survives the flip.
  tft.fillScreen(TFT_BLACK);
  markFrameDirty();
#endif
  tft.setRotation(sanitizeRotation(dispSettings.rotation));
#if defined(DISPLAY_CYD)
  applyCydPanelInversion();
#elif defined(DISPLAY_240x320)
  _tft_instance.invertDisplay(dispSettings.invertColors ? false : true);
#endif
  tft.fillScreen(dispSettings.bgColor);
  markFrameDirty();
  forceRedraw = true;
  lastDisplayUpdate = 0;  // bypass throttle so redraw is immediate after fillScreen
  // Reset clock/pong so they redraw fully after fillScreen cleared everything
  resetActiveClockCache();
}

void triggerDisplayTransition() {
  // Clear previous state so everything redraws for the new printer
  memset(&prevState, 0, sizeof(prevState));
  resetBatteryRedrawCache();
  smoothInited = false;  // snap gauges to new printer's values
  resetGaugeTextCache();
  tft.fillScreen(dispSettings.bgColor);
  markFrameDirty();
  forceRedraw = true;
  // If the clock is on screen (e.g. a non-displayed printer hit its FINISH edge
  // while idle), the fillScreen above wiped it; reset its private cache so it
  // repaints whole instead of leaving a single stale digit on a blank screen.
  resetActiveClockCache();
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
  // Called only when forceRedraw is set by the switch gate; any paint below
  // is a real change.
  markFrameDirty();
  const int16_t cx = uiW() / 2;
  tft.setTextDatum(MC_DATUM);

#if defined(LAYOUT_HAS_LANDSCAPE)
  const bool apLand   = isLandscape();
  const int16_t apTitleY    = apLand ? LY_LAND_AP_TITLE_Y    : LY_AP_TITLE_Y;
  const int16_t apSsidLblY  = apLand ? LY_LAND_AP_SSID_LBL_Y : LY_AP_SSID_LBL_Y;
  const int16_t apSsidY     = apLand ? LY_LAND_AP_SSID_Y     : LY_AP_SSID_Y;
  const int16_t apPassLblY  = apLand ? LY_LAND_AP_PASS_LBL_Y : LY_AP_PASS_LBL_Y;
  const int16_t apPassY     = apLand ? LY_LAND_AP_PASS_Y     : LY_AP_PASS_Y;
  const int16_t apOpenY     = apLand ? LY_LAND_AP_OPEN_Y     : LY_AP_OPEN_Y;
  const int16_t apIpY       = apLand ? LY_LAND_AP_IP_Y       : LY_AP_IP_Y;
#else
  const int16_t apTitleY    = LY_AP_TITLE_Y;
  const int16_t apSsidLblY  = LY_AP_SSID_LBL_Y;
  const int16_t apSsidY     = LY_AP_SSID_Y;
  const int16_t apPassLblY  = LY_AP_PASS_LBL_Y;
  const int16_t apPassY     = LY_AP_PASS_Y;
  const int16_t apOpenY     = LY_AP_OPEN_Y;
  const int16_t apIpY       = LY_AP_IP_Y;
#endif

  // Title
  tft.setTextColor(CLR_GREEN, CLR_BG);
  setFont(tft, FONT_LARGE);
  tft.drawString("WiFi Setup", cx, apTitleY);

  // Instructions
  setFont(tft, FONT_BODY);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connect to WiFi:", cx, apSsidLblY);

  // AP SSID
  tft.setTextColor(CLR_CYAN, CLR_BG);
  setFont(tft, FONT_LARGE);
  char ssid[32];
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_PREFIX, mac);
  tft.drawString(ssid, cx, apSsidY);

  // Password
  setFont(tft, FONT_BODY);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Password:", cx, apPassLblY);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(WIFI_AP_PASSWORD, cx, apPassY);

  // IP
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Then open:", cx, apOpenY);
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  setFont(tft, FONT_LARGE);
  tft.drawString("192.168.4.1", cx, apIpY);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting WiFi
// ---------------------------------------------------------------------------
static void drawConnectingWiFi() {
  // Always animates (dots + slide bar) — mark dirty every frame.
  markFrameDirty();
  const int16_t sw = uiW();
  const int16_t sh = uiH();
  const int16_t cx = sw / 2;
  const int16_t cy = sh / 2;
  tft.setTextDatum(MC_DATUM);

  // Title
  setFont(tft, FONT_BODY);
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
  markFrameDirty();

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
  setFont(tft, FONT_LARGE);
  tft.drawString("WiFi Connected", midX, midY + 10);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  setFont(tft, FONT_BODY);
  tft.drawString(WiFi.localIP().toString().c_str(), midX, midY + 40);
}

// ---------------------------------------------------------------------------
//  Screen: OTA firmware update in progress
// ---------------------------------------------------------------------------
#include "web_server.h"
static void drawOtaUpdate() {
  // Progress updates every frame during OTA — mark dirty every frame.
  markFrameDirty();
  const int16_t sw = uiW();
  const int16_t cx = sw / 2;
  const int16_t cy = uiH() / 2;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT, CLR_BG);

  // Title
  setFont(tft, FONT_LARGE);
  tft.drawString("Updating", cx, cy - 60);
  setFont(tft, FONT_BODY);
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
  setFont(tft, FONT_BODY);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(pctBuf, cx, cy + 14);

  // Status
  setFont(tft, FONT_BODY);
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
  // Always animates (dots + slide bar + elapsed counter) — mark dirty every frame.
  markFrameDirty();
  const int16_t sw = uiW();
  const int16_t cx = sw / 2;
  const int16_t cy = uiH() / 2;
  tft.setTextDatum(MC_DATUM);

  // Title
  setFont(tft, FONT_BODY);
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
  setFont(tft, FONT_BODY);

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
    setFont(tft, FONT_SMALL);
    tft.setTextDatum(MC_DATUM);

    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt: %u", d.attempts);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, cx, cy + 50);

    if (d.lastRc != 0) {
      bool cloudAuthErr = isCloudMode(p.config.mode) &&
                          (d.lastRc == 4 || d.lastRc == 5);
      tft.setTextColor(CLR_RED, CLR_BG);
      if (cloudAuthErr) {
        // Cloud token rejected - server-side TTL is 90 days. May also be
        // invalidated earlier if the user does "log out everywhere" or
        // changes their password. The cookie in the browser may still look
        // valid; the server is the source of truth.
        tft.drawString("Token rejected", cx, cy + 62);
        tft.drawString("Re-paste in web setup", cx, cy + 74);
      } else {
        snprintf(buf, sizeof(buf), "Err: %s", mqttRcToString(d.lastRc));
        tft.drawString(buf, cx, cy + 62);
      }
    }
  }
}

// Forward declaration (defined after CYD section)
static void drawWifiSignalIndicator(const BambuState& s, int16_t wifiY);
static int16_t drawBatteryPrefix(int16_t y);

// ---------------------------------------------------------------------------
//  Screen: Idle (connected, not printing)
// ---------------------------------------------------------------------------
static void drawIdleNoPrinter() {
  if (!forceRedraw) return;
  markFrameDirty();

  const int16_t cx = uiW() / 2;
  tft.setTextDatum(MC_DATUM);

#if defined(LAYOUT_HAS_LANDSCAPE)
  const bool npLand = isLandscape();
  const int16_t npTitleY = npLand ? LY_LAND_IDLE_NP_TITLE_Y : LY_IDLE_NP_TITLE_Y;
  const int16_t npWifiY  = npLand ? LY_LAND_IDLE_NP_WIFI_Y  : LY_IDLE_NP_WIFI_Y;
  const int16_t npDotY   = npLand ? LY_LAND_IDLE_NP_DOT_Y   : LY_IDLE_NP_DOT_Y;
  const int16_t npMsgY   = npLand ? LY_LAND_IDLE_NP_MSG_Y   : LY_IDLE_NP_MSG_Y;
  const int16_t npOpenY  = npLand ? LY_LAND_IDLE_NP_OPEN_Y  : LY_IDLE_NP_OPEN_Y;
  const int16_t npIpY    = npLand ? LY_LAND_IDLE_NP_IP_Y    : LY_IDLE_NP_IP_Y;
#else
  const int16_t npTitleY = LY_IDLE_NP_TITLE_Y;
  const int16_t npWifiY  = LY_IDLE_NP_WIFI_Y;
  const int16_t npDotY   = LY_IDLE_NP_DOT_Y;
  const int16_t npMsgY   = LY_IDLE_NP_MSG_Y;
  const int16_t npOpenY  = LY_IDLE_NP_OPEN_Y;
  const int16_t npIpY    = LY_IDLE_NP_IP_Y;
#endif

  tft.setTextColor(CLR_GREEN, CLR_BG);
  setFont(tft, FONT_LARGE);
  tft.drawString("BambuHelper", cx, npTitleY);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  setFont(tft, FONT_BODY);
  tft.drawString("WiFi Connected", cx, npWifiY);

  tft.fillCircle(cx, npDotY, 5, CLR_GREEN);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  setFont(tft, FONT_BODY);
  tft.drawString("No printer configured", cx, npMsgY);
  tft.drawString("Open in browser:", cx, npOpenY);

  tft.setTextColor(CLR_ORANGE, CLR_BG);
  setFont(tft, FONT_LARGE);
  tft.drawString(WiFi.localIP().toString().c_str(), cx, npIpY);
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

// Draw a string left-aligned, hard-truncating at character boundary if it
// doesn't fit maxW. No ellipsis.
// Assumes font and text color are already configured by the caller.
static void drawStringClipped(const char* s, int16_t x, int16_t y, int16_t maxW) {
  if (!s || !*s) return;
  if (maxW <= 0) return;
  if (tft.textWidth(s) <= maxW) {
    tft.drawString(s, x, y);
    return;
  }
  char buf[40];
  size_t n = strlen(s);
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = '\0';
  while (n > 0 && tft.textWidth(buf) > maxW) {
    buf[--n] = '\0';
  }
  if (n > 0) tft.drawString(buf, x, y);
}

static void drawCelsiusUnit(int16_t x, int16_t y, uint16_t color) {
  setFont(tft, FONT_LARGE);
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
    markFrameDirty();
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
    markFrameDirty();
    drawLedProgressBar(tft, 0, dryProgress);
  }

  // === Header bar ===
#if defined(LAYOUT_HAS_LANDSCAPE)
  const int16_t dryHdrY    = land ? LY_LAND_HDR_Y     : LY_HDR_Y;
  const int16_t dryHdrH    = land ? LY_LAND_HDR_H     : LY_HDR_H;
  const int16_t dryHdrCY   = land ? LY_LAND_HDR_CY    : LY_HDR_CY;
  const int16_t dryHdrDotCY = land ? LY_LAND_HDR_DOT_CY : LY_HDR_DOT_CY;
#else
  const int16_t dryHdrY    = LY_HDR_Y;
  const int16_t dryHdrH    = LY_HDR_H;
  const int16_t dryHdrCY   = LY_HDR_CY;
  const int16_t dryHdrDotCY = LY_HDR_DOT_CY;
#endif
  if (forceRedraw) {
    markFrameDirty();
    tft.fillRect(0, dryHdrY, scrW, dryHdrH, CLR_BG);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu";
    tft.drawString(name, LY_HDR_NAME_X, dryHdrCY);

    // "DRYING" badge (right, orange)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    const char* badge = "DRYING";
    tft.fillCircle(scrW - LY_HDR_BADGE_RX - tft.textWidth(badge) - 10, dryHdrCY, 4, CLR_ORANGE);
    tft.drawString(badge, scrW - LY_HDR_BADGE_RX, dryHdrCY);

    // Multi-printer dots
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(cx - 5 + di * 10, dryHdrDotCY, 3, dotClr);
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
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_ORANGE, CLR_BG);
    tft.drawString(unitName, cx, 40);
  }

#if defined(LAYOUT_HAS_LANDSCAPE)
  if (land) {
    // === Landscape: temp left, drying facts right, ETA above bottom ===
    // X positions are scrW-proportional so CYD/ws_lcd_200 (320 wide) stays
    // pixel-identical (tempCx=88, infoCx=238) while JC3248W535 (480 wide)
    // gets a balanced layout (tempCx=128, infoCx=358).
    const int16_t halfW = scrW / 2;
    const int16_t tempCx = scrW / 4 + 8;
    const int16_t infoCx = scrW * 3 / 4 - 2;

    if (unitChanged) {
      tft.fillRect(0, 55, scrW, LY_LAND_ETA_Y - 55, CLR_BG);
    }

    if (tempChanged) {
      if (!unitChanged) tft.fillRect(0, 70, halfW, 72, CLR_BG);
      char tempBuf[14];
      snprintf(tempBuf, sizeof(tempBuf), "%d", tempShown);
      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_7SEG);
      tft.setTextColor(CLR_ORANGE, CLR_BG);
      int16_t tempW = tft.textWidth(tempBuf);
      tft.drawString(tempBuf, tempCx - 10, 112);

      drawCelsiusUnit(tempCx - 10 + tempW / 2 + 2, 98, CLR_ORANGE);
    }

    if (remainChanged) {
      if (!unitChanged) tft.fillRect(halfW, 58, scrW - halfW, 54, CLR_BG);
      char timeBuf[16];
      uint16_t h = u.dryRemainMin / 60;
      uint16_t m = u.dryRemainMin % 60;
      if (h > 0)
        snprintf(timeBuf, sizeof(timeBuf), "%dh %02dm", h, m);
      else
        snprintf(timeBuf, sizeof(timeBuf), "%dm", m);

      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Remaining", infoCx, 72);
      setFont(tft, FONT_LARGE);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString(timeBuf, infoCx, 96);
    }

    if (humidityChanged) {
      if (!unitChanged) tft.fillRect(halfW, 114, scrW - halfW, 54, CLR_BG);
      char humBuf[8];
      snprintf(humBuf, sizeof(humBuf), "%d%%", u.humidityRaw);

      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("Humidity", infoCx, 128);
      setFont(tft, FONT_LARGE);
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
      setFont(tft, FONT_7SEG);
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
      setFont(tft, FONT_LARGE);
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
      setFont(tft, FONT_LARGE);
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
    setFont(tft, FONT_7SEG);
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
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_YELLOW, CLR_BG);
    tft.drawString(timeBuf, cx, 140);
  }

  // === Humidity ===
  if (humidityChanged) {
    tft.fillRect(0, 158, SCREEN_W, 25, CLR_BG);
    char humBuf[24];
    snprintf(humBuf, sizeof(humBuf), "Humidity: %d%%", u.humidityRaw);

    tft.setTextDatum(MC_DATUM);
    setFont(tft, FONT_BODY);
    tft.setTextColor(humidityColor(u.humidity), CLR_BG);
    tft.drawString(humBuf, cx, 170);
  }
#endif

  // === ETA ===
  if (remainChanged) {
    markFrameDirty();
#if defined(LAYOUT_HAS_LANDSCAPE)
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
    setFont(tft, FONT_LARGE);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.drawString(etaBuf, cx, etaTextY);
  }

  // === Bottom bar — connected indicator ===
  {
#if defined(LAYOUT_HAS_LANDSCAPE)
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
      markFrameDirty();
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

// Forward declarations for AMS-strip functions. Available on all builds that
// have a 240px-wide AMS layout (240x320 + 240x240). Excluded on the 480x480
// SenseCAP build whose layout has no LY_AMS_* constants.
#if !defined(DISPLAY_480x480)
static void drawAmsStrip(const AmsState& ams, int16_t zoneY, int16_t zoneH, int16_t barH,
                         int16_t barMaxW = LY_AMS_BAR_MAX_W,
                         bool showFilamentTypes = false);
static bool useEnhancedPortraitAms(const AmsState& ams);
#endif
#if defined(LAYOUT_HAS_AMS_STRIP)
static void drawAmsZone(const BambuState& s, bool force);
#endif

// Helper macro for the 240x240-only AMS-view feature (replaces gauge row 2
// with an AMS strip). The HTML row, gauge gating, and dispatch are gated by
// this macro so layouts with a permanent AMS strip (LAYOUT_HAS_AMS_STRIP)
// and 480x480 (no LY_AMS_*) skip the new code path.
#if !defined(LAYOUT_HAS_AMS_STRIP) && !defined(DISPLAY_480x480)
  #define LAYOUT_240x240_AMS_VIEW 1
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
    markFrameDirty();
    memset(&prevState, 0, sizeof(prevState));
    resetBatteryRedrawCache();
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
      markFrameDirty();
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
    markFrameDirty();
    memset(&prevState, 0, sizeof(prevState));
    resetBatteryRedrawCache();
    forceRedraw = true;
  }

  // Effective screen dimensions. In landscape, always reserve the right
  // column for the AMS sidebar even when no AMS is present yet — otherwise
  // cx flips between full-width centre and gauge-area centre the moment
  // MQTT data arrives, leaving ghost gauges at the previous position.
  // Matches the printing screen, which keeps the sidebar geometry fixed.
#if defined(LAYOUT_HAS_AMS_STRIP)
  const bool idleLandAmsSidebar = isLandscape();
  const int16_t fullW = (int16_t)tft.width();
  const int16_t scrW = idleLandAmsSidebar ? (int16_t)LY_LAND_GAUGE_W : fullW;
  const int16_t scrH = (int16_t)tft.height();
#else
  const int16_t scrW = SCREEN_W;
  const int16_t scrH = SCREEN_H;
#endif
  const int16_t cx = scrW / 2;

  // Landscape Y-coordinate selector (boards without LAYOUT_HAS_LANDSCAPE pin
  // these to portrait).
#if defined(LAYOUT_HAS_LANDSCAPE)
  const bool idleLand           = isLandscape();
  const int16_t lyIdleNameY     = idleLand ? LY_LAND_IDLE_NAME_Y    : LY_IDLE_NAME_Y;
  const int16_t lyIdleStateY    = idleLand ? LY_LAND_IDLE_STATE_Y   : LY_IDLE_STATE_Y;
  const int16_t lyIdleStateH    = idleLand ? LY_LAND_IDLE_STATE_H   : LY_IDLE_STATE_H;
  const int16_t lyIdleStateTy   = idleLand ? LY_LAND_IDLE_STATE_TY  : LY_IDLE_STATE_TY;
  const int16_t lyIdleDotY      = idleLand ? LY_LAND_IDLE_DOT_Y     : LY_IDLE_DOT_Y;
  const int16_t lyIdleGaugeR    = idleLand ? LY_LAND_IDLE_GAUGE_R   : LY_IDLE_GAUGE_R;
  const int16_t lyIdleGaugeY    = idleLand ? LY_LAND_IDLE_GAUGE_Y   : LY_IDLE_GAUGE_Y;
  const int16_t lyIdleGOffset   = idleLand ? LY_LAND_IDLE_G_OFFSET  : LY_IDLE_G_OFFSET;
#else
  const int16_t lyIdleNameY     = LY_IDLE_NAME_Y;
  const int16_t lyIdleStateY    = LY_IDLE_STATE_Y;
  const int16_t lyIdleStateH    = LY_IDLE_STATE_H;
  const int16_t lyIdleStateTy   = LY_IDLE_STATE_TY;
  const int16_t lyIdleDotY      = LY_IDLE_DOT_Y;
  const int16_t lyIdleGaugeR    = LY_IDLE_GAUGE_R;
  const int16_t lyIdleGaugeY    = LY_IDLE_GAUGE_Y;
  const int16_t lyIdleGOffset   = LY_IDLE_G_OFFSET;
#endif

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
    setFont(tft, FONT_LARGE);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, cx, lyIdleNameY);
    markFrameDirty();
  }

  // Status badge — only redraw when state changes
  if (stateChanged) {
    markFrameDirty();
    setFont(tft, FONT_BODY);
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
    tft.fillRect(0, lyIdleStateY, scrW, lyIdleStateH, CLR_BG);
    tft.setTextColor(stateColor, CLR_BG);
    tft.drawString(stateStr, cx, lyIdleStateTy);
  }

  // Connected indicator
  if (connChanged) {
    tft.fillCircle(cx, lyIdleDotY, 5, s.connected ? CLR_GREEN : CLR_RED);
    markFrameDirty();
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
      markFrameDirty();
      const int16_t hintY = lyIdleDotY + 15;
      tft.fillRect(0, hintY - 6, scrW, 14, CLR_BG);
      if (showHint) {
        setFont(tft, FONT_SMALL);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(CLR_TEXT_DARK, CLR_BG);
        tft.drawString("Press to refresh", cx, hintY);
      }
      hintShown = showHint;
    }
  }

  // Nozzle temp gauge
  if (tempChanged) {
    markFrameDirty();
    drawTempGauge(tft, cx - lyIdleGOffset, lyIdleGaugeY, lyIdleGaugeR,
                  s.nozzleTemp, s.nozzleTarget, (float)dispSettings.nozzleScaleMax,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    // Bed temp gauge
    drawTempGauge(tft, cx + lyIdleGOffset, lyIdleGaugeY, lyIdleGaugeR,
                  s.bedTemp, s.bedTarget, (float)dispSettings.bedScaleMax,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // AMS on idle (boards with permanent AMS strip)
  //  Portrait: horizontal strip below the gauges.
  //  Landscape: right-column vertical sidebar (drawAmsZone handles it).
#if defined(LAYOUT_HAS_AMS_STRIP)
  if (s.ams.present && s.ams.unitCount > 0 && isLandscape()) {
    // Reuse the printing screen's landscape sidebar renderer. Its internal
    // caches gate the redraw, so this is a no-op when nothing changed.
    drawAmsZone(s, forceRedraw);
  } else if (s.ams.present && s.ams.unitCount > 0 && !isLandscape()) {
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
      markFrameDirty();
    }
  }
#endif

  // Bottom status bar: Filament/WiFi | Power | Door
  static bool     idlePrevTasmotaOnline = false;
  static float    idlePrevWatts        = -2.0f;

  bool idleTasmotaOnline = tasmotaIsActiveForSlot(rotState.displayIndex);
  float idleCurWatts = tasmotaGetWattsForSlot(rotState.displayIndex);

  int16_t botCY = scrH - 9;
  bool batChanged = batteryStateChanged();
  bool bottomChanged = batChanged ||
                       wifiChanged ||
                       (s.ams.activeTray != prevState.ams.activeTray) ||
                       (s.doorOpen != prevState.doorOpen) ||
                       (s.doorSensorPresent != prevState.doorSensorPresent) ||
                       (idleTasmotaOnline != idlePrevTasmotaOnline) ||
                       (idleTasmotaOnline && idleCurWatts != idlePrevWatts);
  idlePrevTasmotaOnline  = idleTasmotaOnline;
  idlePrevWatts          = idleCurWatts;

  if (bottomChanged) {
    markFrameDirty();
    tft.fillRect(0, scrH - 18, scrW, 18, CLR_BG);
    setFont(tft, FONT_BODY);

    // Left: filament circle (if AMS active) or WiFi signal
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      int16_t bx = drawBatteryPrefix(botCY);
      tft.drawCircle(10 + bx, botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10 + bx, botCY, 4, t.colorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(t.type, 19 + bx, botCY);
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      int16_t bx = drawBatteryPrefix(botCY);
      tft.drawCircle(10 + bx, botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10 + bx, botCY, 4, s.ams.vtColorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(s.ams.vtType, 19 + bx, botCY);
    } else {
      drawWifiSignalIndicator(s, botCY);
    }

    // Center: power watts (if Tasmota online)
    // Ready screen has no layer count, so always show power (no alternation)
    bool showPower = idleTasmotaOnline;
    if (showPower) {
      drawIcon16(tft, cx - 20, botCY - 8, icon_lightning, CLR_YELLOW);
      char wBuf[8];
      snprintf(wBuf, sizeof(wBuf), "%.0fW", idleCurWatts);
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
//  AMS tray visualization (layouts with permanent AMS strip)
//  Portrait: horizontal strip between gauges and ETA
//  Landscape (CYD only): vertical strip on right side
// ---------------------------------------------------------------------------
#if defined(LAYOUT_HAS_AMS_STRIP)

static uint8_t  prevAmsUnitCount = 0;
static uint8_t  prevAmsActive    = 255;
static uint8_t  prevAmsUnitIds[AMS_MAX_UNITS] = {0};
static uint8_t  prevAmsUnitTrayCounts[AMS_MAX_UNITS] = {0};
static bool     prevAmsUnitPresent[AMS_MAX_UNITS] = {false};
static uint16_t prevAmsTrayColors[AMS_MAX_TRAYS] = {0};
static bool     prevAmsTrayPresent[AMS_MAX_TRAYS] = {false};
static int8_t   prevAmsTrayRemain[AMS_MAX_TRAYS];  // init in drawAmsZone
static char     prevAmsTrayTypes[AMS_MAX_TRAYS][16] = {{0}};

#endif // LAYOUT_HAS_AMS_STRIP (prevAms* caches consumed only by drawAmsZone)

// The stateless AMS helpers below also compile on 240x240 builds, where the
// "AMS view" toggle reuses drawAmsStrip(). Excluded only on 480x480 (SenseCAP)
// because layout_480x480.h does not define LY_AMS_*.
#if !defined(DISPLAY_480x480)

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

  // Self-contained repaint: clear the 2px strip above the bar (where a stale
  // notch from a previously-active tray may live) so callers don't have to
  // wipe the surrounding area to switch the active marker without flicker.
  if (y >= 2) tft.fillRect(x, y - 2, w, 2, CLR_BG);

  if (!tray.present) {
    // Wipe the bar's interior so a previous color from this slot is gone.
    tft.fillRect(x, y, w, h, CLR_BG);
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

// Gauge-slot AMS visualization: one rounded vertical bar per loaded tray (same
// look as the landscape sidebar / portrait strip), no humidity. Bar count tracks
// the unit's actual trayCount, so an AMS HT (single slot) shows one centered bar
// instead of three empty ones. Centered in a square 2R x 2R region; "AMS N"
// label below at the standard gauge-label position.
// Exported (declared in display_ui.h) so the split renderer can reuse it. The
// helper drawAmsTrayBarRounded() it relies on stays private here because
// drawAmsStrip()/drawAmsZone() also use it.
void drawAmsBarsGauge(int16_t cx, int16_t cy, int16_t radius,
                      const AmsState& ams, uint8_t unitIndex,
                      bool forceRedraw) {
  uint16_t bg = dispSettings.bgColor;

  const bool unitPresent = ams.present
                        && unitIndex < AMS_MAX_UNITS
                        && unitIndex < ams.unitCount
                        && ams.units[unitIndex].present;

  // Number of bars = the unit's actual tray count, so an AMS HT (1 slot) draws
  // a single centered bar. Mirrors the standard AMS strip/sidebar (drawAmsZone),
  // which also centers `trayCount` bars. Fall back to the full count while the
  // unit isn't reporting yet (placeholder before AMS data arrives).
  uint8_t bars = unitPresent ? ams.units[unitIndex].trayCount : AMS_TRAYS_PER_UNIT;
  if (bars == 0 || bars > AMS_TRAYS_PER_UNIT) bars = AMS_TRAYS_PER_UNIT;

  // The bar count can shrink at runtime (4 placeholder bars before AMS HT data
  // arrives, then 1). That transition doesn't always come with forceRedraw, so
  // the leftover bars would ghost; track the last-drawn count per unit and wipe
  // the slot whenever it changes as well.
  static uint8_t prevBars[AMS_MAX_UNITS] = { 0, 0, 0, 0 };
  bool clear = forceRedraw || (unitIndex < AMS_MAX_UNITS && prevBars[unitIndex] != bars);
  if (unitIndex < AMS_MAX_UNITS) prevBars[unitIndex] = bars;
  if (clear) {
    // Rect clear (not circle) - bars are top-anchored and reach into the
    // corners of the bounding square, where a circle of radius+2 would miss
    // a few pixels at every corner. Match the slot-type-change clear in
    // the printing-screen slot loop so behaviour stays consistent.
    const int16_t side = radius * 2 + 4;
    tft.fillRect(cx - radius - 2, cy - radius - 2, side, side, bg);
  }

  const int16_t innerSize = radius * 2;
  const int16_t barGap = 2;
  // Bar width is sized for the full 4-slot layout so a 1-tray HT unit shows a
  // single bar the same width as one bar in a 4-tray unit, just centered.
  int16_t barW = (innerSize - (AMS_TRAYS_PER_UNIT - 1) * barGap) / AMS_TRAYS_PER_UNIT;
  if (barW < 4) barW = 4;
  if (barW > 18) barW = 18;
  // Reserve top space for the active-tray notch and bottom space so the slot
  // label ("AMS N") sits below the bars without clipping into them. Bars are
  // top-anchored — the saved height becomes the breathing room above the label.
  const int16_t barTopMargin    = 2;
  const int16_t barLabelMargin  = 14;
  int16_t barH = innerSize - barTopMargin - barLabelMargin;
  if (barH < 10) barH = 10;
  const int16_t totalW = barW * bars + (bars - 1) * barGap;
  const int16_t startX = cx - totalW / 2;
  const int16_t startY = cy - innerSize / 2 + barTopMargin;

  AmsTray absent{};
  absent.present = false;

  for (uint8_t t = 0; t < bars; t++) {
    int16_t bx = startX + t * (barW + barGap);
    uint8_t trayIdx = unitIndex * AMS_TRAYS_PER_UNIT + t;
    bool active = unitPresent && (trayIdx == ams.activeTray);
    const AmsTray& tray = unitPresent ? ams.trays[trayIdx] : absent;
    drawAmsTrayBarRounded(bx, startY, barW, barH, tray, active);
  }

  static const char* amsLabel[AMS_MAX_UNITS] = { "AMS 1", "AMS 2", "AMS 3", "AMS 4" };
  bool sm = dispSettings.smallLabels;
  int16_t labelY = cy + radius + (sm ? 3 : -1);
  tft.setTextDatum(MC_DATUM);
  setFont(tft, sm ? FONT_SMALL : FONT_BODY);
  tft.setTextColor(CLR_TEXT_DIM, bg);
  tft.drawString(amsLabel[unitIndex], cx, labelY);
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

  // A lone AMS unit doesn't need an "AMS A" caption - drop it and grow the
  // bars into the reclaimed band (portrait only; landscape is drawAmsZone).
  const bool singleAms = (units == 1);

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
  int16_t barY, typeY = 0, labelY = 0;
  if (showFilamentTypes) {
    barY = zoneY;
    if (singleAms) {
      // Lone unit: no "AMS A" caption - bars + type row fill the zone.
      barH  = zoneH - 3 - 11;     // 3px gap + ~11px filament-type row
      typeY = barY + barH + 3;
    } else {
      typeY  = barY + barH + 3;   // 2px lower than before so names breathe off the bar
      labelY = typeY + 13;        // 8px font1 type row + 5px gap before font2 label
    }
  } else {
    if (singleAms) {
      // Lone unit: no "AMS A" caption - bars own the whole zone (2px top pad
      // leaves room for the active-tray notch).
      barY = zoneY + 2;
      barH = zoneH - 4;
    } else {
      barY   = zoneY + (zoneH - barH - LY_AMS_LABEL_OFFY - 8) / 2;
      labelY = barY + barH + LY_AMS_LABEL_OFFY;
    }
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
        setFont(tft, FONT_SMALL);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        if (typeBuf[0]) {
          tft.drawString(typeBuf, bx + barW / 2, typeY);
        }
      }
    }

    if (!singleAms) {
      char label[6];
      snprintf(label, sizeof(label), "AMS %c", 'A' + u);
      tft.setTextDatum(TC_DATUM);
      bool sm = dispSettings.smallLabels;
      setFont(tft, sm ? FONT_SMALL : FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(label, groupX + groupW / 2, labelY + (showFilamentTypes ? 0 : 2));
    }
  }
}

// ---------------------------------------------------------------------------
//  Enhanced portrait AMS layout
//    - enabled when per-printer extras flag is on AND 1 or 2 AMS units
//    - draws wider tray bars (rectangular, not square) and a filament-type
//      label under each tray. For 3+ AMS the bars are too narrow for text
//      to fit cleanly, so we fall back to the default compact layout.
// ---------------------------------------------------------------------------
// Enhanced portrait AMS is used when there is enough horizontal room for
// readable filament-type labels under each tray bar. Per-layout bar-width math:
//   240 wide (240x320): 4 trays * 3 AMS leaves ~16px per bar - labels collide
//   320 wide (320x480): 4 trays * 3 AMS leaves ~21px per bar - labels fit
// With 4+ units even 320 wide gets tight (<15px bars), so cap there.
static bool useEnhancedPortraitAms(const AmsState& ams) {
#if defined(DISPLAY_320x480)
  return ams.unitCount >= 1 && ams.unitCount <= 3;
#else
  return ams.unitCount >= 1 && ams.unitCount <= 2;
#endif
}

#else  // DISPLAY_480x480
// 480x480 (SenseCAP) does not implement the AMS bar visualizations (the helpers
// above are excluded). Provide a stub so the unguarded GAUGE_AMS_BARS call site
// in drawPrinting() and the exported declaration still link; AMS-bars simply
// isn't drawn on this profile. (This also fixes a pre-existing link break on
// main where the static drawAmsBarsGauge was referenced but never defined here.)
void drawAmsBarsGauge(int16_t, int16_t, int16_t, const AmsState&, uint8_t, bool) {}
#endif // !DISPLAY_480x480 (stateless AMS helpers)

#if defined(LAYOUT_HAS_AMS_STRIP)

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

  bool unitLayoutChanged = (s.ams.unitCount != prevAmsUnitCount);
  for (uint8_t i = 0; i < AMS_MAX_UNITS && !unitLayoutChanged; i++) {
    unitLayoutChanged = (s.ams.units[i].present != prevAmsUnitPresent[i]) ||
                        (s.ams.units[i].id != prevAmsUnitIds[i]) ||
                        (s.ams.units[i].trayCount != prevAmsUnitTrayCounts[i]);
  }

  bool amsChanged = force || badgeChanged || unitLayoutChanged;
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
  markFrameDirty();

  prevAmsGcodeStateId = s.gcodeStateId;
  strncpy(prevAmsGcodeStateText, s.gcodeState, 15);
  prevAmsGcodeStateText[15] = '\0';

  // Save state for next comparison (AMS trays)
  prevAmsUnitCount = s.ams.unitCount;
  prevAmsActive    = s.ams.activeTray;
  for (uint8_t i = 0; i < AMS_MAX_UNITS; i++) {
    prevAmsUnitPresent[i] = s.ams.units[i].present;
    prevAmsUnitIds[i] = s.ams.units[i].id;
    prevAmsUnitTrayCounts[i] = s.ams.units[i].trayCount;
  }
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

    // --- Status badge (only when right column is active = units >= 1) ---
    // When units == 0 the header takes over the badge (right-aligned), so
    // skip drawing it here to avoid two badges colliding.
    if (units >= 1) {
      uint16_t badgeColor = CLR_TEXT_DIM;
      if (s.gcodeStateId == GCODE_RUNNING)      badgeColor = CLR_GREEN;
      else if (s.gcodeStateId == GCODE_PAUSE)   badgeColor = CLR_YELLOW;
      else if (s.gcodeStateId == GCODE_FAILED)  badgeColor = CLR_RED;
      else if (s.gcodeStateId == GCODE_PREPARE) badgeColor = CLR_BLUE;

      const int16_t bx = LY_LAND_AMS_X - 4;
      const int16_t bw = LY_LAND_AMS_W + 8;
      tft.fillRect(bx, LY_LAND_BADGE_Y, bw, LY_LAND_BADGE_H, dispSettings.bgColor);

      tft.setTextDatum(MC_DATUM);
      setFont(tft, FONT_BODY);
      tft.setTextColor(badgeColor, dispSettings.bgColor);
      const int16_t cx = LY_LAND_AMS_X + LY_LAND_AMS_W / 2;
      // Dot + label centered in the right column.
      int16_t tw = tft.textWidth(s.gcodeState);
      tft.fillCircle(cx - tw / 2 - 6, LY_LAND_BADGE_CY, 3, badgeColor);
      tft.drawString(s.gcodeState, cx + 4, LY_LAND_BADGE_CY);
    }

    // --- AMS bars area ---
    // Only wipe the whole strip when the bar layout itself can have moved
    // (forced redraw or unit count changed). For same-layout updates each
    // tray repaints itself in-place without flicker.
    const bool layoutChanged = force || unitLayoutChanged;
    if (layoutChanged) {
      tft.fillRect(LY_LAND_AMS_X - 4, LY_LAND_AMS_TOP, LY_LAND_AMS_W + 8,
                   amsBot - LY_LAND_AMS_TOP, CLR_BG);
    }

    if (units == 0 || units > AMS_MAX_UNITS) return;

    const int16_t totalH = amsBot - LY_LAND_AMS_TOP;
    const int16_t groupGap = 6;
    const int16_t labelH = 16;  // font 2 label height below bars
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
      setFont(tft, sm ? FONT_SMALL : FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(label, LY_LAND_AMS_X + LY_LAND_AMS_W / 2, gy + barH + 1);
    }

  } else if (enhanced) {
    // Portrait enhanced layout: wider tray bars + filament-type labels.
    if (dispSettings.amsTrayTypes) {
      drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H,
                   LY_AMS_BAR_MAX_W_EXTRAS, /*showFilamentTypes=*/true);
    } else {
      // Labels off (user choice): reclaim the type-row height by growing the
      // bars to fill the zone, leaving room only for the AMS caption beneath.
      // Caption height is read from the active label font so this adapts across
      // layouts (240x320 / 320x480) and the smallLabels toggle. drawAmsStrip's
      // non-types path centers these taller bars and drops the caption below.
      setFont(tft, dispSettings.smallLabels ? FONT_SMALL : FONT_BODY);
      int16_t capH = tft.fontHeight();
      int16_t tallBarH = LY_AMS_H - LY_AMS_LABEL_OFFY - capH - 2;
      if (tallBarH < LY_AMS_BAR_H) tallBarH = LY_AMS_BAR_H;  // never shrink below default
      drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, tallBarH,
                   LY_AMS_BAR_MAX_W_EXTRAS, /*showFilamentTypes=*/false);
    }
  } else {
    drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H);
  }
}

#endif // LAYOUT_HAS_AMS_STRIP

// ---------------------------------------------------------------------------
//  Helper: draw battery icon (vertical, 8x16) at (x, y) with fill from bottom.
//  Footprint is 8 px wide x 16 px tall: 4x2 nub on top, 8x14 body below.
// ---------------------------------------------------------------------------
static void drawBatteryIconOnly(int16_t x, int16_t y, uint8_t pct) {
  uint16_t fg;
  if (pct < 20) fg = CLR_RED;
  else if (pct < 50) fg = CLR_YELLOW;
  else fg = CLR_GREEN;

  bool blank = false;
  if (Battery::isCritical()) {
    blank = ((millis() / 500) & 1) != 0;
  }

  uint16_t outline = blank ? CLR_BG : CLR_TEXT_DIM;
  // Clear footprint
  tft.fillRect(x, y, 8, 16, CLR_BG);
  // Top nub (centered, 4 wide x 2 tall)
  tft.fillRect(x + 2, y, 4, 2, outline);
  // Body outline (8 wide x 14 tall, starts at y+2). Interior is 6x12 at (x+1, y+3).
  tft.drawRect(x, y + 2, 8, 14, outline);

  if (!blank) {
    int16_t levelH = (int16_t)((12 * (uint16_t)pct + 50) / 100);
    if (levelH > 0) {
      tft.fillRect(x + 1, y + 3 + (12 - levelH), 6, levelH, fg);
    }
  }
}

// True when the battery icon should be rendered: hardware presence AND user
// has not disabled the indicator in the web UI.
static inline bool shouldShowBatteryIndicator() {
  return dispSettings.showBatteryIndicator && Battery::isPresent();
}

// ---------------------------------------------------------------------------
//  Helper: draw WiFi signal indicator OR battery indicator (replaces WiFi
//  on Waveshare boards when a battery is detected at boot).
// ---------------------------------------------------------------------------
static void drawWifiSignalIndicator(const BambuState& s, int16_t wifiY = LY_WIFI_Y) {
  if (shouldShowBatteryIndicator()) {
    int16_t iconY = wifiY - LY_BAT_H / 2;
    drawBatteryIconOnly(LY_WIFI_X, iconY, Battery::percent());
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)Battery::percent());
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, LY_WIFI_X + LY_BAT_TEXT_X, wifiY);
    return;
  }
  drawIcon16(tft, LY_WIFI_X, wifiY - 8, icon_wifi, CLR_TEXT_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  char wifiBuf[12];
  snprintf(wifiBuf, sizeof(wifiBuf), "%ddBm", s.wifiSignal);
  tft.drawString(wifiBuf, LY_WIFI_X + 18, wifiY);
}

// ---------------------------------------------------------------------------
//  Helper: draw battery icon as a prefix BEFORE swatch+filament name (on
//  Waveshare boards). Returns x-offset to apply to swatch and text positions.
// ---------------------------------------------------------------------------
static int16_t drawBatteryPrefix(int16_t y) {
  if (!shouldShowBatteryIndicator()) return 0;
  int16_t iconY = y - LY_BAT_H / 2;
  drawBatteryIconOnly(LY_WIFI_X, iconY, Battery::percent());
  return LY_BAT_SHIFT_X;
}

// ---------------------------------------------------------------------------
//  Helper: gauge slot grid descriptor.
//
//  drawPrinting() supports three slot layouts that draw from three INDEPENDENT
//  per-printer arrays so each physical position has its own gauge type:
//    - 2x3 standard  (6 slots, cfg.gaugeSlots[0..5])              every mode
//    - 2x4 landscape (+2 slots, cfg.landscapeExtras[0..1])        landscape8Slots
//    - 3x3 portrait  (+3 slots, cfg.portraitExtras[0..2])         portrait9Slots
//                                                       (LY_PORT9_GAUGE_R only)
//
//  computeSlotGrid resolves which array each visible slot pulls from for the
//  current mode, so the printing-screen body just reads grid.types[si] and
//  grid.x/y[si] without caring about the storage layout. To add a new mode:
//  add a new PrinterConfig extras array, a new branch here, and a web-UI
//  section - the slot loop stays untouched.
// ---------------------------------------------------------------------------
struct SlotGrid {
  int16_t x[GAUGE_SLOT_MAX];
  int16_t y[GAUGE_SLOT_MAX];
  uint8_t types[GAUGE_SLOT_MAX];  // resolved gauge type per visible slot
  int16_t r;                       // per-mode gauge radius
  uint8_t count;                   // 6, 8, or 9 - upper bound for the slot loop
};

static void computeSlotGrid(SlotGrid& g, const PrinterConfig& cfg, bool landscape) {
  const bool eight = landscape && dispSettings.landscape8Slots;
#if defined(LY_PORT9_GAUGE_R)
  const bool nine  = !landscape && dispSettings.portrait9Slots;
#else
  const bool nine  = false;
#endif

  // Zero everything first so unused slots resolve to (0, 0)/EMPTY and skip.
  for (uint8_t i = 0; i < GAUGE_SLOT_MAX; i++) {
    g.x[i] = 0; g.y[i] = 0; g.types[i] = GAUGE_EMPTY;
  }

  // Slots 0-5 always come from the standard array, regardless of mode.
  for (uint8_t i = 0; i < GAUGE_SLOT_COUNT; i++) g.types[i] = cfg.gaugeSlots[i];

  if (eight) {
#if defined(LAYOUT_HAS_LANDSCAPE) && defined(LY_LAND8_COL1)
    const int16_t cs[4] = { LY_LAND8_COL1, LY_LAND8_COL2, LY_LAND8_COL3, LY_LAND8_COL4 };
    const int16_t rs[2] = { LY_LAND_ROW1,  LY_LAND_ROW2 };
    g.r = LY_GAUGE_R; g.count = 8;
    for (uint8_t row = 0; row < 2; row++)
      for (uint8_t col = 0; col < 4; col++) {
        g.x[row * 4 + col] = cs[col];
        g.y[row * 4 + col] = rs[row];
      }
    g.types[6] = cfg.landscapeExtras[0];
    g.types[7] = cfg.landscapeExtras[1];
    return;
#endif
  }
#if defined(LY_PORT9_GAUGE_R)
  if (nine) {
    const int16_t cs[3] = { LY_COL1, LY_COL2, LY_COL3 };
    const int16_t rs[3] = { LY_PORT9_ROW1, LY_PORT9_ROW2, LY_PORT9_ROW3 };
    g.r = LY_PORT9_GAUGE_R; g.count = 9;
    for (uint8_t row = 0; row < 3; row++)
      for (uint8_t col = 0; col < 3; col++) {
        g.x[row * 3 + col] = cs[col];
        g.y[row * 3 + col] = rs[row];
      }
    g.types[6] = cfg.portraitExtras[0];
    g.types[7] = cfg.portraitExtras[1];
    g.types[8] = cfg.portraitExtras[2];
    return;
  }
#endif

  // Default: 2x3, columns + rows pick portrait/landscape variant.
#if defined(LAYOUT_HAS_LANDSCAPE)
  const int16_t c0 = landscape ? LY_LAND_COL1 : LY_COL1;
  const int16_t c1 = landscape ? LY_LAND_COL2 : LY_COL2;
  const int16_t c2 = landscape ? LY_LAND_COL3 : LY_COL3;
  const int16_t r0 = landscape ? LY_LAND_ROW1 : LY_ROW1;
  const int16_t r1 = landscape ? LY_LAND_ROW2 : LY_ROW2;
#else
  const int16_t c0 = LY_COL1, c1 = LY_COL2, c2 = LY_COL3;
  const int16_t r0 = LY_ROW1, r1 = LY_ROW2;
#endif
  g.r = LY_GAUGE_R; g.count = 6;
  g.x[0]=c0; g.x[1]=c1; g.x[2]=c2; g.x[3]=c0; g.x[4]=c1; g.x[5]=c2;
  g.y[0]=r0; g.y[1]=r0; g.y[2]=r0; g.y[3]=r1; g.y[4]=r1; g.y[5]=r1;
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
#if defined(LAYOUT_HAS_AMS_STRIP)
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

  // Gauge grid constants. gR is per-mode (shrunk for 3x3 portrait); gT (arc
  // thickness) stays at the layout default because the arc geometry scales
  // with radius internally.
  SlotGrid grid;
  computeSlotGrid(grid, p.config, isLandscape());
  const int16_t gR = grid.r;
  const int16_t gT = LY_GAUGE_T;

  // Effective Y positions — landscape on CYD uses 240x240-style positions
#if defined(LAYOUT_HAS_AMS_STRIP)
  const bool land = isLandscape();
  const uint8_t units = s.ams.unitCount;
  // 8-slot landscape mode: drops the AMS sidebar in favour of a 2x4 gauge
  // grid spanning the full canvas. Everything that branches on landAmsCol
  // (header clear width, ETA/bot-bar widths, sidebar badge) naturally folds
  // back to the "no right column" path.
  const bool land8 = land && dispSettings.landscape8Slots;
  // Right column (badge + AMS) only used in landscape with at least one AMS
  // and when the 8-slot mode hasn't claimed that strip for gauges.
  const bool landAmsCol = land && units >= 1 && !land8;
  // Bottom bar / ETA span the full 320 only when right column doesn't need
  // to extend down to the screen edge (0..2 AMS, or no AMS).
  const bool botFull = land && (land8 || landBottomBarFullWidth(units));
  const int16_t eff_etaY     = land ? LY_LAND_ETA_Y     : LY_ETA_Y;
  const int16_t eff_etaH     = land ? LY_LAND_ETA_H     : LY_ETA_H;
  const int16_t eff_etaTextY = land ? LY_LAND_ETA_TEXT_Y : LY_ETA_TEXT_Y;
  const int16_t eff_botY     = land ? LY_LAND_BOT_Y     : LY_BOT_Y;
  const int16_t eff_botH     = land ? LY_LAND_BOT_H     : LY_BOT_H;
  const int16_t eff_botCY    = land ? LY_LAND_BOT_CY    : LY_BOT_CY;
  // Width of the horizontal strip used for header / ETA / bottom bar.
  // Header is always full canvas. ETA must shrink to 240 whenever the AMS
  // column exists, otherwise its y=190..220 fillRect would carve into the
  // bottom of the AMS bars (AMS_BOT_SHORT=210 with 0-2 AMS). The bottom bar
  // stays full-width when the AMS column ends high (0-2 AMS) and shrinks to
  // 240 only when AMS extends to AMS_BOT_FULL (3-4 AMS).
  const int16_t hdrW   = land ? uiW() : SCREEN_W;
  const int16_t etaW   = landAmsCol ? LY_LAND_GAUGE_W : (land ? uiW() : SCREEN_W);
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

  // === Permanent AMS strip: clear unused zone on screen transitions ===
#if defined(LAYOUT_HAS_AMS_STRIP)
  {
    int16_t scrW = (int16_t)tft.width();
    int16_t scrH = (int16_t)tft.height();
    if (forceRedraw) {
      markFrameDirty();
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
    }
    // No special wipe for units→0: drawAmsZone is now called unconditionally
    // in landscape and clears the AMS bars area itself while still drawing
    // the badge.
  }
#endif

  // === H2-style LED progress bar (y=0-5) ===
  if (progChanged) {
    markFrameDirty();
    drawLedProgressBar(tft, 0, s.progress);
  }

  // === Header bar ===
  // In landscape with AMS column the badge is rendered separately by
  // drawAmsZone in the right column, so the header carries only printer
  // name + multi-printer dots.
#if defined(LAYOUT_HAS_LANDSCAPE)
  const int16_t hdrY     = land ? LY_LAND_HDR_Y     : LY_HDR_Y;
  const int16_t hdrH     = land ? LY_LAND_HDR_H     : LY_HDR_H;
  const int16_t hdrCY    = land ? LY_LAND_HDR_CY    : LY_HDR_CY;
  const int16_t hdrDotCY = land ? LY_LAND_HDR_DOT_CY : LY_HDR_DOT_CY;
#else
  const int16_t hdrY     = LY_HDR_Y;
  const int16_t hdrH     = LY_HDR_H;
  const int16_t hdrCY    = LY_HDR_CY;
  const int16_t hdrDotCY = LY_HDR_DOT_CY;
#endif
  if (forceRedraw || stateChanged) {
    markFrameDirty();
    uint16_t hdrBg = dispSettings.bgColor;
    // Cap the header clear at the gauge column when the right-side AMS panel
    // is active; otherwise the badge that drawAmsZone parks at y=7..27 in the
    // right column (x=240..320) would be wiped on every header repaint.
#if defined(LAYOUT_HAS_AMS_STRIP)
    const int16_t hdrClearW = landAmsCol ? LY_LAND_GAUGE_W : hdrW;
#else
    const int16_t hdrClearW = hdrW;
#endif
    tft.fillRect(0, hdrY, hdrClearW, hdrH, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, LY_HDR_NAME_X, hdrCY);

    // State badge (right) — only when right column not used for it
    if (!landAmsCol) {
      uint16_t badgeColor = CLR_TEXT_DIM;
      if (s.gcodeStateId == GCODE_RUNNING) badgeColor = CLR_GREEN;
      else if (s.gcodeStateId == GCODE_PAUSE) badgeColor = CLR_YELLOW;
      else if (s.gcodeStateId == GCODE_FAILED) badgeColor = CLR_RED;
      else if (s.gcodeStateId == GCODE_PREPARE) badgeColor = CLR_BLUE;

      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(badgeColor, hdrBg);
      setFont(tft, FONT_BODY);
      tft.fillCircle(hdrW - LY_HDR_BADGE_RX - tft.textWidth(s.gcodeState) - 10, hdrCY, 4, badgeColor);
      tft.drawString(s.gcodeState, hdrW - LY_HDR_BADGE_RX, hdrCY);
    }

    // Printer indicator dots (multi-printer) — centered on the visible
    // header strip (excludes the right column when the AMS panel is active).
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(hdrClearW / 2 - 5 + di * 10, hdrDotCY, 3, dotClr);
      }
    }
  }

  // === AMS-view toggle (240x240 only): swap gauge row 2 for AMS strip ===
#if defined(LAYOUT_240x240_AMS_VIEW)
  const bool amsViewActive  = p.config.amsView;
  const bool amsHasContent  = s.ams.present && s.ams.unitCount > 0;
  const bool amsStripVisible = amsViewActive && amsHasContent;
  static bool prevAmsViewActive   = false;
  static bool prevAmsStripVisible = false;
  static bool amsStripDirty       = false;

  // Toggle: wipe both the row-2 gauge band and the AMS band. Start at the
  // higher of the two top edges - LY_AMS_Y - 2 covers the active-tray notch
  // in enhanced AMS, which extends slightly above LY_AMS_Y.
  if (amsViewActive != prevAmsViewActive) {
    const int16_t row2Top = LY_ROW2 - LY_GAUGE_R - 2;
    const int16_t amsTop  = LY_AMS_Y - 2;
    const int16_t y0      = (row2Top < amsTop) ? row2Top : amsTop;
    const int16_t y1      = LY_AMS_Y + LY_AMS_H + 8;
    tft.fillRect(0, y0, LY_W, y1 - y0, dispSettings.bgColor);
    prevAmsViewActive   = amsViewActive;
    prevAmsStripVisible = false;
    amsStripDirty       = true;
  }

  // AMS disappeared while view is on - drawAmsStrip won't be called this
  // frame, so explicitly wipe the band.
  if (amsViewActive && prevAmsStripVisible && !amsStripVisible) {
    tft.fillRect(0, LY_AMS_Y - 2, LY_W, LY_AMS_H + 10, dispSettings.bgColor);
  }
  prevAmsStripVisible = amsStripVisible;
#else
  const bool amsViewActive   = false;
  const bool amsStripVisible = false;
#endif

  // === Configurable gauge grid (6 / 8 / 9 slots, see computeSlotGrid) ===
  {
    // Grid is already populated for this frame at the top of drawPrinting()
    // (gR/gT use grid.r). Read-only here.
    const int16_t* slotX = grid.x;
    const int16_t* slotY = grid.y;
    const uint8_t  slotCount = grid.count;
    static uint8_t prevSlotTypes[GAUGE_SLOT_MAX] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    // Invalidate per-slot redraw cache when rotation flips, so the gauges
    // get a clean redraw at their new (x, y) instead of leaving artefacts
    // at the old positions.
    static uint8_t prevSlotRotation = 0xFF;
    if (prevSlotRotation != dispSettings.rotation) {
      for (uint8_t i = 0; i < GAUGE_SLOT_MAX; i++) prevSlotTypes[i] = 0xFF;
      prevSlotRotation = dispSettings.rotation;
    }
    // Same cache invalidation when slot count changes (6 <-> 8 <-> 9) — the
    // grid shape itself shifts and any frame-cached positions are stale.
    static uint8_t prevSlotCount = 0;
    if (prevSlotCount != slotCount) {
      for (uint8_t i = 0; i < GAUGE_SLOT_MAX; i++) prevSlotTypes[i] = 0xFF;
      prevSlotCount = slotCount;
    }
    // Labels for GAUGE_CHAMBER_FAN (Chamber vs Exhaust) and GAUGE_AUX_FAN (Aux vs
    // L.Aux) depend on s.airductFuncs, which starts at 0 and gets bits OR'd in
    // once the first pushall with device.airduct.parts lands. The slot cache
    // skips redraws when the type hasn't changed, so without this invalidation
    // the initial "Chamber"/"Aux" labels drawn on boot would stick forever
    // even after the bitmask updates. Force a redraw for those slots when the
    // mask changes (typically once per session, on the first pushall).
    static uint32_t prevAirductFuncs = 0;
    if (prevAirductFuncs != s.airductFuncs) {
      for (uint8_t i = 0; i < slotCount; i++) {
        uint8_t gtPrev = grid.types[i];
        if (gtPrev == GAUGE_CHAMBER_FAN || gtPrev == GAUGE_AUX_FAN) prevSlotTypes[i] = 0xFF;
      }
      prevAirductFuncs = s.airductFuncs;
    }

    // Mark any slots beyond the current mode's count as needing a clean
    // redraw if they ever become active again — they aren't drawn this frame.
    for (uint8_t si = slotCount; si < GAUGE_SLOT_MAX; si++) prevSlotTypes[si] = 0xFF;

    for (uint8_t si = 0; si < slotCount; si++) {
      // Skip row-2 slots when AMS view replaces them. Mark prevSlotTypes as
      // invalid so toggling back later forces a clean redraw.
      if (amsViewActive && si >= 3) { prevSlotTypes[si] = 0xFF; continue; }
      uint8_t gt = grid.types[si];
      if (gt >= GAUGE_TYPE_COUNT) gt = GAUGE_EMPTY;

      bool typeChanged = (gt != prevSlotTypes[si]);
      if (typeChanged) {
        // Slot type changed (or first draw) - clear area and reset cache.
        // Use a square fill, not a circle: GAUGE_AMS_BARS draws a rectangular
        // bar block that extends into the corners of the slot bounding box,
        // so a circular clear would leave ghost pixels behind when switching
        // away from it. Spacing math (see computeSlotGrid) leaves >=4px
        // between this square and the next slot's, even on the densest
        // 320x480 9-slot grid.
        const int16_t slotClear = gR * 2 + 4;
        tft.fillRect(slotX[si] - gR - 2, slotY[si] - gR - 2,
                     slotClear, slotClear, dispSettings.bgColor);
        // Label is drawn MC_DATUM at labelY, so its glyphs straddle that line;
        // FONT_BODY (~20px) and FONT_SMALL (~14px) need a generous band to fully
        // erase a longer previous label when shrinking to a shorter one.
        bool sm = dispSettings.smallLabels;
        int16_t labelY = slotY[si] + gR + (sm ? 3 : -1);
        int16_t lh     = sm ? 18 : 24;
        tft.fillRect(slotX[si] - gR - 2, labelY - lh / 2,
                     gR * 2 + 4, lh, dispSettings.bgColor);
        prevSlotTypes[si] = gt;
      }

      // Per-type change detection
      bool needDraw = forceRedraw || typeChanged;
      if (!needDraw) {
        switch (gt) {
          case GAUGE_PROGRESS:    needDraw = (s.progress != prevState.progress) || (s.remainingMinutes != prevState.remainingMinutes); break;
          case GAUGE_NOZZLE:      needDraw = animating || s.nozzleTemp != prevState.nozzleTemp || s.nozzleTarget != prevState.nozzleTarget; break;
          case GAUGE_NOZZLE_RIGHT: needDraw = animating || s.nozzleTempN[0] != prevState.nozzleTempN[0] || s.nozzleTargetN[0] != prevState.nozzleTargetN[0]; break;
          case GAUGE_NOZZLE_LEFT:  needDraw = animating || s.nozzleTempN[1] != prevState.nozzleTempN[1] || s.nozzleTargetN[1] != prevState.nozzleTargetN[1]; break;
          case GAUGE_BED:         needDraw = animating || s.bedTemp != prevState.bedTemp || s.bedTarget != prevState.bedTarget; break;
          case GAUGE_PART_FAN:      needDraw = animating || s.coolingFanPct != prevState.coolingFanPct; break;
          case GAUGE_AUX_FAN:       needDraw = animating || s.auxFanPct != prevState.auxFanPct; break;
          case GAUGE_AUX_FAN_RIGHT: needDraw = animating || s.auxFanRightPct != prevState.auxFanRightPct; break;
          case GAUGE_CHAMBER_FAN:   needDraw = animating || s.chamberFanPct != prevState.chamberFanPct; break;
          case GAUGE_EXHAUST_FAN:   needDraw = animating || s.exhaustFanPct != prevState.exhaustFanPct; break;
          case GAUGE_CHAMBER_TEMP:  needDraw = animating || s.chamberTemp != prevState.chamberTemp; break;
          case GAUGE_HEATBREAK:     needDraw = animating || s.heatbreakFanPct != prevState.heatbreakFanPct; break;
          case GAUGE_CLOCK:       needDraw = true; break;  // text cache handles actual redraw
          case GAUGE_POWER:       needDraw = true; break;  // watts live in tasmota runtime; text cache + incremental arc gate the redraw
          case GAUGE_LAYER:       needDraw = s.layerNum != prevState.layerNum || s.totalLayers != prevState.totalLayers; break;
          default:
            // AMS humidity / temperature / filament gauges — index derived from enum value
            if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
              uint8_t ui = gt - GAUGE_AMS_HUM_1;
              const AmsUnit &cu = s.ams.units[ui], &pu = prevState.ams.units[ui];
              needDraw = cu.humidityRaw != pu.humidityRaw || cu.humidity != pu.humidity || cu.present != pu.present;
            } else if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
              uint8_t ui = gt - GAUGE_AMS_TEMP_1;
              const AmsUnit &cu = s.ams.units[ui], &pu = prevState.ams.units[ui];
              needDraw = cu.temp != pu.temp || cu.present != pu.present;
            } else if ((gt >= GAUGE_AMS_FILAMENT_1 && gt <= GAUGE_AMS_FILAMENT_4)
                    || (gt >= GAUGE_AMS_BARS_1     && gt <= GAUGE_AMS_BARS_4)) {
              uint8_t ui = (gt >= GAUGE_AMS_BARS_1) ? (gt - GAUGE_AMS_BARS_1)
                                                    : (gt - GAUGE_AMS_FILAMENT_1);
              const bool isBars = (gt >= GAUGE_AMS_BARS_1);
              needDraw = s.ams.present != prevState.ams.present
                      || s.ams.unitCount != prevState.ams.unitCount;
              if (!needDraw) {
                const AmsUnit &cu = s.ams.units[ui], &pu = prevState.ams.units[ui];
                // Bars gauge does not show humidity, so skip the humidity diff
                // to avoid redundant redraws.
                if (cu.present != pu.present
                    || (!isBars && cu.humidity != pu.humidity)
                    || cu.trayCount != pu.trayCount) needDraw = true;
              }
              if (!needDraw && isBars && s.ams.activeTray != prevState.ams.activeTray) {
                needDraw = true;
              }
              if (!needDraw) {
                for (int t = 0; t < AMS_TRAYS_PER_UNIT; t++) {
                  int idx = ui * AMS_TRAYS_PER_UNIT + t;
                  const AmsTray &ct = s.ams.trays[idx], &pt = prevState.ams.trays[idx];
                  if (ct.present != pt.present || ct.colorRgb565 != pt.colorRgb565
                      || ct.remain != pt.remain
                      || (!isBars && strcmp(ct.type, pt.type) != 0)) {
                    needDraw = true; break;
                  }
                }
              }
            }
            break;
        }
      }
      if (!needDraw) continue;
      markFrameDirty();

      int16_t cx = slotX[si], cy = slotY[si];
      bool fr = forceRedraw || typeChanged;

      switch (gt) {
        case GAUGE_PROGRESS:
          drawProgressArc(tft, cx, cy, gR, gT, s.progress, prevState.progress, s.remainingMinutes, fr);
          break;
        case GAUGE_NOZZLE:
          drawTempGauge(tft, cx, cy, gR, s.nozzleTemp, s.nozzleTarget, (float)dispSettings.nozzleScaleMax,
                        dispSettings.nozzle.arc, nozzleLabel(s), nullptr, fr,
                        &dispSettings.nozzle, smoothNozzleTemp);
          break;
        case GAUGE_NOZZLE_RIGHT:
          drawTempGauge(tft, cx, cy, gR, s.nozzleTempN[0], s.nozzleTargetN[0], (float)dispSettings.nozzleScaleMax,
                        dispSettings.nozzle.arc, "Nozzle R", nullptr, fr,
                        &dispSettings.nozzle, smoothNozzleTempN[0]);
          break;
        case GAUGE_NOZZLE_LEFT:
          drawTempGauge(tft, cx, cy, gR, s.nozzleTempN[1], s.nozzleTargetN[1], (float)dispSettings.nozzleScaleMax,
                        dispSettings.nozzle.arc, "Nozzle L", nullptr, fr,
                        &dispSettings.nozzle, smoothNozzleTempN[1]);
          break;
        case GAUGE_BED:
          drawTempGauge(tft, cx, cy, gR, s.bedTemp, s.bedTarget, (float)dispSettings.bedScaleMax,
                        dispSettings.bed.arc, "Bed", nullptr, fr,
                        &dispSettings.bed, smoothBedTemp);
          break;
        case GAUGE_PART_FAN:
          drawFanGauge(tft, cx, cy, gR, s.coolingFanPct, dispSettings.partFan.arc, "Part", fr,
                       &dispSettings.partFan, smoothPartFan);
          break;
        case GAUGE_AUX_FAN:
          // Re-label to "L.Aux" only when the printer actually has a right-aux
          // counterpart (func=6). Otherwise it's the only aux fan, so keep "Aux".
          drawFanGauge(tft, cx, cy, gR, s.auxFanPct, dispSettings.auxFan.arc,
                       (s.airductFuncs & (1u << 6)) ? "L.Aux" : "Aux", fr,
                       &dispSettings.auxFan, smoothAuxFan);
          break;
        case GAUGE_AUX_FAN_RIGHT:
          drawFanGauge(tft, cx, cy, gR, s.auxFanRightPct, dispSettings.auxFanRight.arc, "R.Aux", fr,
                       &dispSettings.auxFanRight, smoothAuxRightFan);
          break;
        case GAUGE_CHAMBER_FAN:
          // big_fan2_speed -> chamberFanPct mapping is shared across all models, but on
          // airduct printers that report func=2 (H2C/X2D) the same legacy field actually
          // carries the EXHAUST fan value, not a chamber fan. Relabel the gauge so the
          // displayed name matches reality on those models. X1C/P/A and other non-airduct
          // models keep the "Chamber" label that matches Bambu's own UI terminology.
          drawFanGauge(tft, cx, cy, gR, s.chamberFanPct, dispSettings.chamberFan.arc,
                       (s.airductFuncs & (1u << 2)) ? "Exhaust" : "Chamber", fr,
                       &dispSettings.chamberFan, smoothChamberFan);
          break;
        case GAUGE_EXHAUST_FAN:
          drawFanGauge(tft, cx, cy, gR, s.exhaustFanPct, dispSettings.exhaustFan.arc, "Exhaust", fr,
                       &dispSettings.exhaustFan, smoothExhaustFan);
          break;
        case GAUGE_CHAMBER_TEMP:
          drawTempGauge(tft, cx, cy, gR, s.chamberTemp, 0.0f, (float)dispSettings.chamberScaleMax,
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
        case GAUGE_POWER:
          drawPowerGauge(tft, cx, cy, gR,
                         tasmotaGetWattsForSlot(rotState.displayIndex),
                         tasmotaIsActiveForSlot(rotState.displayIndex), "Power", fr);
          break;
        case GAUGE_EMPTY:
          if (fr) tft.fillCircle(cx, cy, gR + 2, dispSettings.bgColor);
          break;
        default: {
          // AMS humidity / temperature / filament gauges — index derived from enum value
          static const char* amsLabel[AMS_MAX_UNITS] = { "AMS 1", "AMS 2", "AMS 3", "AMS 4" };
          if (gt >= GAUGE_AMS_HUM_1 && gt <= GAUGE_AMS_HUM_4) {
            uint8_t ui = gt - GAUGE_AMS_HUM_1;
            const AmsUnit& u = s.ams.units[ui];
            drawHumidityGauge(tft, cx, cy, gR, u.humidityRaw, u.humidity, u.present, amsLabel[ui], fr);
          } else if (gt >= GAUGE_AMS_TEMP_1 && gt <= GAUGE_AMS_TEMP_4) {
            uint8_t ui = gt - GAUGE_AMS_TEMP_1;
            const AmsUnit& u = s.ams.units[ui];
            drawTempGauge(tft, cx, cy, gR, u.present ? u.temp : 0, 0, (float)dispSettings.chamberScaleMax,
                          dispSettings.chamberTemp.arc, amsLabel[ui], nullptr, fr, &dispSettings.chamberTemp);
          } else if (gt >= GAUGE_AMS_FILAMENT_1 && gt <= GAUGE_AMS_FILAMENT_4) {
            uint8_t ui = gt - GAUGE_AMS_FILAMENT_1;
            drawAmsFilamentAllGauge(tft, cx, cy, gR, gT, s.ams, ui, fr);
          } else if (gt >= GAUGE_AMS_BARS_1 && gt <= GAUGE_AMS_BARS_4) {
            uint8_t ui = gt - GAUGE_AMS_BARS_1;
            drawAmsBarsGauge(cx, cy, gR, s.ams, ui, fr);
          } else {
            if (fr) tft.fillCircle(cx, cy, gR + 2, dispSettings.bgColor);
          }
        } break;
      }
    }
  }

  // === AMS zone (CYD: portrait + landscape) ===
  // Landscape always calls drawAmsZone — the right column also hosts the
  // status badge, so it must refresh even when AMS data is empty. Force a
  // redraw when the AMS unit-count zone changed so static change-detection
  // inside drawAmsZone cannot skip a frame after the right column was wiped
  // (e.g. transient unitCount=0 from MQTT reconnect).
#if defined(LAYOUT_HAS_AMS_STRIP)
  const bool amsForce = forceRedraw || unitsZoneChanged;
  // Both extended grid modes claim the area drawAmsZone would paint into:
  //  - landscape 8-slot replaces the right-side sidebar
  //  - portrait 9-slot replaces the bottom AMS strip
  // Skip drawAmsZone whenever the corresponding mode is on; the toggle-change
  // detector at the top of displayUI() forces a clean repaint when flipping.
  const bool skipAmsZone =
        ( isLandscape() && dispSettings.landscape8Slots)
#if defined(LY_PORT9_GAUGE_R)
     || (!isLandscape() && dispSettings.portrait9Slots)
#endif
        ;
  if (!skipAmsZone && (isLandscape() || (s.ams.present && s.ams.unitCount > 0))) {
    drawAmsZone(s, amsForce);
  }
#endif

  // === 240x240 AMS view: replaces gauge row 2 with the same AMS strip ===
  // drawAmsStrip self-clears its zone every call, so call only when state
  // actually changed - otherwise every render frame would flicker the bars.
  // Change set mirrors the existing GAUGE_AMS_FILAMENT detection.
#if defined(LAYOUT_240x240_AMS_VIEW)
  if (amsStripVisible) {
    bool needDraw = forceRedraw || amsStripDirty;
    if (!needDraw) {
      needDraw = (s.ams.unitCount != prevState.ams.unitCount)
              || (s.ams.activeTray != prevState.ams.activeTray);
    }
    if (!needDraw) {
      for (int u = 0; u < AMS_MAX_UNITS && !needDraw; u++) {
        const AmsUnit& cu = s.ams.units[u];
        const AmsUnit& pu = prevState.ams.units[u];
        if (cu.present != pu.present || cu.trayCount != pu.trayCount) needDraw = true;
      }
    }
    if (!needDraw) {
      for (int t = 0; t < AMS_MAX_TRAYS && !needDraw; t++) {
        const AmsTray& ct = s.ams.trays[t];
        const AmsTray& pt = prevState.ams.trays[t];
        if (ct.present != pt.present
            || ct.colorRgb565 != pt.colorRgb565
            || ct.remain != pt.remain) needDraw = true;
      }
    }
    bool enhanced = useEnhancedPortraitAms(s.ams);
    if (!needDraw && enhanced) {
      for (int t = 0; t < AMS_MAX_TRAYS && !needDraw; t++) {
        if (strcmp(s.ams.trays[t].type, prevState.ams.trays[t].type) != 0) needDraw = true;
      }
    }
    if (needDraw) {
      if (enhanced) {
        drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H,
                     LY_AMS_BAR_MAX_W_EXTRAS, /*showFilamentTypes=*/true);
      } else {
        drawAmsStrip(s.ams, LY_AMS_Y, LY_AMS_H, LY_AMS_BAR_H);
      }
      amsStripDirty = false;
    }
  }
#endif

  // === Info line — ETA finish time or PAUSE/ERROR alert ===
  if (etaChanged || stateChanged) {
    markFrameDirty();
    const int16_t etaCx = etaW / 2;
    tft.fillRect(0, eff_etaY, etaW, eff_etaH, CLR_BG);
    tft.setTextDatum(MC_DATUM);

    if (s.gcodeStateId == GCODE_PAUSE) {
      setFont(tft, FONT_LARGE);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString("PAUSED", etaCx, eff_etaTextY);
    } else if (s.gcodeStateId == GCODE_FAILED) {
      setFont(tft, FONT_LARGE);
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
        setFont(tft, FONT_LARGE);
        tft.setTextColor(CLR_GREEN, CLR_BG);
        tft.drawString(etaBuf, etaCx, eff_etaTextY);
      } else {
        // NTP not synced yet OR user requested remaining time - show remaining time only
        char remBuf[24];
        uint16_t h = s.remainingMinutes / 60;
        uint16_t m = s.remainingMinutes % 60;
        snprintf(remBuf, sizeof(remBuf), "Remaining: %dh %02dm", h, m);
        setFont(tft, FONT_LARGE);
        tft.setTextColor(CLR_TEXT, CLR_BG);
        tft.drawString(remBuf, etaCx, eff_etaTextY);
      }
    } else {
      setFont(tft, FONT_LARGE);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("ETA: ---", etaCx, eff_etaTextY);
    }
  }

#if defined(DISPLAY_320x480)
  // === File name line (320x480 only — fills the gap above the bottom bar) ===
  bool fileChanged = forceRedraw ||
                     strcmp(s.subtaskName, prevState.subtaskName) != 0;
  if (fileChanged) {
    markFrameDirty();
    const int16_t fileW  = (int16_t)tft.width();
    const int16_t fileCx = fileW / 2;
    tft.fillRect(0, LY_FILE_Y - LY_FILE_H / 2, fileW, LY_FILE_H, CLR_BG);
    if (s.subtaskName[0] != '\0') {
      setFont(tft, FONT_BODY);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      drawStringClipped(s.subtaskName, fileCx, LY_FILE_Y, fileW - 20);
    }
  }
#endif

  // === Bottom status bar — Filament/WiFi | Layer (or Power) | Speed ===
  // Tasmota alternation state (persists across redraws)
  static bool     altShowPower    = false;
  static uint32_t altFlipMs       = 0;
  static bool     prevAltShowPower = false;
  static bool     prevTasmotaOnline = false;
  static float    prevWatts        = -2.0f;

  bool tasmotaOnline = tasmotaIsActiveForSlot(rotState.displayIndex);
  uint8_t tasmotaDM  = tasmotaDisplayModeForSlot(rotState.displayIndex);
  float curWatts     = tasmotaGetWattsForSlot(rotState.displayIndex);

  if (tasmotaOnline && tasmotaDM == 0) {
    if (millis() - altFlipMs > 4000) {
      altShowPower = !altShowPower;
      altFlipMs    = millis();
    }
  } else {
    altShowPower = false;
    altFlipMs    = 0;
  }

  bool showingWifi = !(s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS && s.ams.trays[s.ams.activeTray].present)
                  && !(s.ams.vtPresent && s.ams.activeTray == 254);
  bool batChanged320 = batteryStateChanged();
  bool bottomChanged = batChanged320 || forceRedraw || unitsZoneChanged ||
                       (s.speedLevel != prevState.speedLevel) ||
                       (s.doorOpen != prevState.doorOpen) ||
                       (s.doorSensorPresent != prevState.doorSensorPresent) ||
                       (s.layerNum != prevState.layerNum) ||
                       (s.totalLayers != prevState.totalLayers) ||
                       (s.ams.activeTray != prevState.ams.activeTray) ||
                       (showingWifi && s.wifiSignal != prevState.wifiSignal) ||
                       (altShowPower != prevAltShowPower) ||
                       (tasmotaOnline != prevTasmotaOnline) ||
                       (tasmotaOnline && curWatts != prevWatts);
  prevAltShowPower  = altShowPower;
  prevTasmotaOnline = tasmotaOnline;
  prevWatts         = curWatts;

  if (bottomChanged) {
    markFrameDirty();
    const int16_t botCx = botW / 2;
    // Right-edge cleanup only on actual bottom-bar width transitions —
    // wiping x=240..320 in the bottom-bar y-range every bottomChanged event
    // would also erase the bottom of the AMS column (AMS_BOT_FULL=236
    // overlaps eff_botY=222..240) and drawAmsZone wouldn't repaint it.
#if defined(LAYOUT_HAS_AMS_STRIP)
    if (botW < uiW() && unitsZoneChanged) {
      int16_t cleanY = eff_botY;
      int16_t cleanH = eff_botH;
      if (landAmsCol) {
        cleanY = LY_LAND_AMS_BOT_FULL;
        cleanH = eff_botY + eff_botH - cleanY;
      }
      if (cleanH > 0) {
        tft.fillRect(botW, cleanY, uiW() - botW, cleanH, CLR_BG);
      }
    }
#endif
    tft.fillRect(0, eff_botY, botW, eff_botH, CLR_BG);
    setFont(tft, FONT_BODY);

    // Predict center text so we can clamp the filament name's right edge
    // and avoid overlap with the layer/power readout (smooth fonts in v2.8
    // are slightly wider than the previous bitmap font).
    bool showPowerNow = tasmotaOnline && (tasmotaDM == 1 || altShowPower);
    char centerBuf[20];
    int16_t centerLeftX;
    if (showPowerNow) {
      snprintf(centerBuf, sizeof(centerBuf), "%.0fW", curWatts);
      centerLeftX = botCx - 20;  // icon starts here (icon_lightning is 16px)
    } else {
      snprintf(centerBuf, sizeof(centerBuf), "%d/%d", s.layerNum, s.totalLayers);
      centerLeftX = botCx - tft.textWidth(centerBuf) / 2;
    }

    // Left: filament indicator (if AMS active) or WiFi signal
    // Dual nozzle (H2C/H2D): activeTray set from extruder.info[].snow per-nozzle
    if (s.ams.present && s.ams.activeTray < AMS_MAX_TRAYS) {
      AmsTray& t = s.ams.trays[s.ams.activeTray];
      if (t.present) {
        int16_t bx = drawBatteryPrefix(eff_botCY);
        tft.drawCircle(10 + bx, eff_botCY, 5, CLR_TEXT_DARK);
        tft.fillCircle(10 + bx, eff_botCY, 4, t.colorRgb565);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
        drawStringClipped(t.type, 19 + bx, eff_botCY, centerLeftX - 3 - (19 + bx));
      } else {
        drawWifiSignalIndicator(s, eff_botCY);
      }
    } else if (s.ams.vtPresent && s.ams.activeTray == 254) {
      int16_t bx = drawBatteryPrefix(eff_botCY);
      tft.drawCircle(10 + bx, eff_botCY, 5, CLR_TEXT_DARK);
      tft.fillCircle(10 + bx, eff_botCY, 4, s.ams.vtColorRgb565);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      drawStringClipped(s.ams.vtType, 19 + bx, eff_botCY, centerLeftX - 3 - (19 + bx));
    } else {
      drawWifiSignalIndicator(s, eff_botCY);
    }

    // Center: power (if Tasmota active) or layer count (centerBuf preformatted above)
    if (showPowerNow) {
      drawIcon16(tft, botCx - 20, eff_botCY - 8, icon_lightning, CLR_YELLOW);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(centerBuf, botCx - 2, eff_botCY);
    } else {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString(centerBuf, botCx, eff_botCY);
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
#if defined(LAYOUT_HAS_AMS_STRIP)
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
    markFrameDirty();
    drawLedProgressBar(tft, 0, 100);
  }

  // === Header bar — same as printing screen ===
#if defined(LAYOUT_HAS_LANDSCAPE)
  const int16_t finHdrY     = land ? LY_LAND_HDR_Y     : LY_HDR_Y;
  const int16_t finHdrH     = land ? LY_LAND_HDR_H     : LY_HDR_H;
  const int16_t finHdrCY    = land ? LY_LAND_HDR_CY    : LY_HDR_CY;
  const int16_t finHdrDotCY = land ? LY_LAND_HDR_DOT_CY : LY_HDR_DOT_CY;
#else
  const int16_t finHdrY     = LY_HDR_Y;
  const int16_t finHdrH     = LY_HDR_H;
  const int16_t finHdrCY    = LY_HDR_CY;
  const int16_t finHdrDotCY = LY_HDR_DOT_CY;
#endif
  if (forceRedraw) {
    markFrameDirty();
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, finHdrY, scrW, finHdrH, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    setFont(tft, FONT_BODY);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Printer";
    tft.drawString(name, LY_HDR_NAME_X, finHdrCY);

    // FINISH badge (right)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_GREEN, hdrBg);
    setFont(tft, FONT_BODY);
    tft.fillCircle(scrW - LY_HDR_BADGE_RX - tft.textWidth("FINISH") - 10, finHdrCY, 4, CLR_GREEN);
    tft.drawString("FINISH", scrW - LY_HDR_BADGE_RX, finHdrCY);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(cx - 5 + di * 10, finHdrDotCY, 3, dotClr);
      }
    }
  }

  // === Row 1: Nozzle | Bed (two gauges centered) ===
  if (tempChanged) {
    markFrameDirty();
    drawTempGauge(tft, gaugeLeft, gaugeY, gR,
                  s.nozzleTemp, s.nozzleTarget, (float)dispSettings.nozzleScaleMax,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle, smoothNozzleTemp);

    drawTempGauge(tft, gaugeRight, gaugeY, gR,
                  s.bedTemp, s.bedTarget, (float)dispSettings.bedScaleMax,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed, smoothBedTemp);
  }

  // === "Print Complete!" status ===
  if (forceRedraw) {
    markFrameDirty();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    setFont(tft, FONT_LARGE);
    tft.drawString("Print Complete!", cx, finTextY);
  }

  // === File name ===
  if (forceRedraw) {
    markFrameDirty();
    tft.setTextDatum(MC_DATUM);
    setFont(tft, FONT_BODY);
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
  // STRICT mapping for print kWh/cost: on single-plug "Any" config, watts can
  // be visible on both printer screens but the kWh row must NOT show plug 0's
  // value on printer 2's screen.
  float finishKwh = tasmotaGetPrintKwhUsedForSlot(rotState.displayIndex);
  float finishTariff = tasmotaTariffForSlot(rotState.displayIndex);
  bool kwhChanged = tasmotaKwhChangedForSlot(rotState.displayIndex) ||
                    (tasmotaActiveHere != prevFinTasmotaOnline) ||
                    (finishKwh != prevFinKwh);
  if (forceRedraw || kwhChanged) {
    markFrameDirty();
    const int16_t kwhY = finKwhY;
    // Two-line mode (kWh on first row, cost on second) needs vertical room.
    // 240x320 landscape: gap below kWh band is ~29px (bot bar at 216).
    // 240x320 portrait without AMS: ~63px gap (bot bar at 290).
    // 240x320 portrait WITH AMS: AMS strip starts ~10px below — single line only.
    // 240x240: only ~11px to bottom bar — single line only.
#if defined(LAYOUT_HAS_AMS_STRIP)
    const bool twoLineCost = (finishTariff > 0.0f) &&
                             (land || !(s.ams.present && s.ams.unitCount > 0));
#else
    const bool twoLineCost = false;
#endif
    const int16_t bandH = twoLineCost ? 34 : 18;
    tft.fillRect(0, kwhY - 9, scrW, bandH, CLR_BG);
    if (finishKwh >= 0.0f) {
      setFont(tft, FONT_BODY);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);

      char kwhBuf[24];
      if (twoLineCost || finishTariff <= 0.0f) {
        snprintf(kwhBuf, sizeof(kwhBuf), "%.3f kWh", finishKwh);
      } else {
        snprintf(kwhBuf, sizeof(kwhBuf), "%.3f kWh  (%.2f %s)",
                 finishKwh, finishKwh * finishTariff, tasmotaCurrencySymbol());
      }

      // Center icon + text as a group so longer strings don't drift right.
      const int16_t kwhTextW = tft.textWidth(kwhBuf);
      const int16_t groupW   = 16 + 4 + kwhTextW;
      const int16_t iconX    = cx - groupW / 2;
      drawIcon16(tft, iconX, kwhY - 8, icon_lightning, CLR_YELLOW);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(kwhBuf, iconX + 16 + 4, kwhY);

      if (twoLineCost) {
        char costBuf[24];
        snprintf(costBuf, sizeof(costBuf), "%.2f %s",
                 finishKwh * finishTariff, tasmotaCurrencySymbol());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(costBuf, cx, kwhY + 16);
      }
    }
  }
  prevFinKwh = finishKwh;

  // === AMS strip (portrait, layouts with permanent AMS strip) ===
#if defined(LAYOUT_HAS_AMS_STRIP)
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
      markFrameDirty();
    }
  }
#endif

  // === Bottom status bar ===
  bool waitingForDoor = dpSettings.doorAckEnabled && s.doorSensorPresent && !s.doorAcknowledged;
  float finCurWatts = tasmotaGetWattsForSlot(rotState.displayIndex);
  bool finBatChanged = batteryStateChanged();
  bool finBottomChanged = finBatChanged || forceRedraw ||
                          (waitingForDoor != prevWaitingForDoor) ||
                          (s.doorSensorPresent && s.doorOpen != prevState.doorOpen) ||
                          (tasmotaActiveHere != prevFinTasmotaOnline) ||
                          (tasmotaActiveHere && finCurWatts != prevFinWatts);
  if (finBottomChanged) {
    markFrameDirty();
    prevWaitingForDoor = waitingForDoor;
    tft.fillRect(0, eff_finBotY, scrW, eff_finBotH, CLR_BG);
    setFont(tft, FONT_SMALL);
    if (waitingForDoor) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(CLR_ORANGE, CLR_BG);
      tft.drawString("Open door to dismiss", cx, eff_finWifiY);
    } else {
      drawWifiSignalIndicator(s, eff_finWifiY);

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
      setFont(tft, FONT_SMALL);
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
    markFrameDirty();
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
      markFrameDirty();
    }
  }
  // Pong clock runs at ~50fps, independent of display refresh
  if (currentScreen == SCREEN_CLOCK && dispSettings.pongClock) {
    tickPongClock();
    markFrameDirty();
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
    markFrameDirty();
    forceRedraw = true;
    if (currentScreen == SCREEN_CONNECTING_WIFI || currentScreen == SCREEN_CONNECTING_MQTT) {
      connectScreenStart = millis();
    }
    resetActiveClockCache();
    if (currentScreen == SCREEN_CLOCK) {
      setBacklight(getEffectiveBrightness());  // dim for screensaver
    }
    prevScreen = currentScreen;
  }

  // Grid-mode toggles (landscape-8 / portrait-9): flipping either mid-frame
  // leaves pixels from the previous layout on screen (the AMS sidebar, the
  // AMS strip, or a now-unused column/row) until something else forces a
  // redraw. Treat it like a screen change so the next frame paints clean.
  static bool prev8Slots = dispSettings.landscape8Slots;
  static bool prev9Slots = dispSettings.portrait9Slots;
  if (prev8Slots != dispSettings.landscape8Slots ||
      prev9Slots != dispSettings.portrait9Slots) {
    tft.fillScreen(dispSettings.bgColor);
    markFrameDirty();
    forceRedraw = true;
    // The clock ignores forceRedraw (private digit cache), so reset it after the
    // fillScreen or it stays blank until the next minute/hour roll.
    resetActiveClockCache();
    prev8Slots = dispSettings.landscape8Slots;
    prev9Slots = dispSettings.portrait9Slots;
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

    case SCREEN_SPLIT:
      drawSplit();
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
        markFrameDirty();
        setBacklight(0);
        triggerDisplayTransition();  // clear gauge cache so wake shows fresh data
      }
      break;
  }

  // Save state for next smart-redraw comparison
  memcpy(&prevState, &displayedPrinter().state, sizeof(BambuState));
  forceRedraw = false;
}
