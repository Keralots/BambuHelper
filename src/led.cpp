#include "led.h"
#include "settings.h"
#include "config.h"
#include <Arduino.h>
#if HAS_LED_PIXEL
#include <esp32-hal-rmt.h>
#endif
#include <math.h>

// ---------------------------------------------------------------------------
//  Pin attachment + output tracking
// ---------------------------------------------------------------------------
// attachedPin holds the single pin for LED_DRV_SINGLE and LED_DRV_PIXEL, and the
// red channel for LED_DRV_RGB; it doubles as the "driver is live" flag. The
// driver is latched at attach time rather than read from ledSettings on every
// write, so a settings change mid-effect cannot make the teardown path detach
// something it never attached.
static int8_t  attachedPin  = -1;
static int8_t  attachedPinG = -1;
static int8_t  attachedPinB = -1;
static uint8_t attachedDrv  = LED_DRV_SINGLE;
// Polarity as it was when the pins were claimed. The live value can change under
// a preview without the pins moving, and the teardown has to park them the way
// they were wired, not the way the form now says.
static bool    attachedAnode = false;
#if HAS_LED_PIXEL
static rmt_obj_t* pixelRmt  = NULL;
#endif

// Two trackers with distinct jobs. lastAppliedDuty is the intensity envelope the
// engine last computed - hold-to-dim seeds itself from it. lastPushedOut is what
// actually reached the hardware (a duty in single mode, a packed 0xRRGGBB in the
// colour modes) and exists only to skip redundant writes; setting it to -1 is how
// the rest of the file says "recompute and push on the next tick".
static int16_t lastAppliedDuty = -1;
static int32_t lastPushedOut   = -1;

// ---------------------------------------------------------------------------
//  Effect engine state
// ---------------------------------------------------------------------------
static LedActivity   currentActivity      = LED_ACT_IDLE;
static bool          finishEffectActive   = false;
static unsigned long finishEffectStartMs  = 0;
static unsigned long finishEffectEndMs    = 0;
static uint8_t       finishEffectMode     = LED_FINISH_OFF;
static uint8_t       finishEffectPeak     = 255;
// Latched with the rest of the effect so a colour edit mid-celebration does not
// change the running one, and so the portal's test button can play an unsaved
// colour without touching ledSettings.
static uint32_t      finishEffectColor    = LED_COLOR_FINISHED_DEFAULT;
// True while the running finish effect came from the portal's "Test effect"
// button rather than a real print. Only used to let the test be seen while the
// panel is asleep - a genuine finish still goes dark with the display.
static bool          finishEffectIsTest   = false;
static unsigned long lastTickMs           = 0;

// Error-strobe episode state. Armed on the first tick of a GCODE_FAILED episode
// (not in ledSetActivity - main.cpp re-asserts IDLE then FAILED every loop, which
// would reset the timer). Auto-stops after errorStrobeSeconds (0 = never), or when
// dismissed by a user interaction. Re-arms once the activity leaves FAILED.
static bool          errorStrobeArmed     = false;
static unsigned long errorStrobeEndMs     = 0;
static bool          errorStrobeDismissed = false;

#if HAS_HMS_UI
// One-shot episode for a printer error report (ledStartErrorEpisode). Separate
// from the state above because it is not tied to an activity level: the printer
// keeps printing while the code stands, so there is no episode to leave.
static unsigned long errorEpisodeEndMs    = 0;   // 0 = no episode
// Used when the user set the strobe to "until dismissed" (0). An error report
// is not a stopped print, so it gets a bounded window regardless.
static const uint16_t LED_ERROR_EPISODE_S = 20;
#endif

// Live-preview override. While set, ledTick() uses previewBrightness as the
// resting/peak duty instead of ledSettings.brightness, so the 60Hz tick stops
// fighting rapid slider previews with the stale saved value. Cleared on
// initLed() (called after save).
static bool          previewActive        = false;
static uint8_t       previewBrightness    = 0;
static uint32_t      previewColor         = LED_COLOR_IDLE_DEFAULT;
// Polarity is a wiring fact, not a colour, so it cannot ride in previewColor -
// and it must not be written into ledSettings either, or abandoning the form
// would leave the saved config inverted until the next boot re-read NVS.
static bool          previewAnode         = false;

// ---------------------------------------------------------------------------
//  Hold-to-dim state machine (driven by ledHoldDimUpdate from handleWakeButton)
// ---------------------------------------------------------------------------
static const uint32_t HOLD_THRESHOLD_MS       = 300;
static const uint32_t DIM_STEP_INTERVAL_MS    = 20;
static const uint8_t  DIM_STEP                = 3;
static const uint8_t  LED_MIN_BRIGHTNESS_DIM  = 10;
static const uint32_t LED_SAVE_DEBOUNCE_MS    = 2000;

static enum { DIM_IDLE, DIM_ACTIVE } dimState = DIM_IDLE;
static int8_t   dimDirection   = +1;
static uint32_t dimLastStepMs  = 0;
static uint32_t dimSaveAtMs    = 0;
static uint8_t  dimWorkingDuty = 0;

static inline uint8_t restingBrightness() {
  return previewActive ? previewBrightness : ledSettings.brightness;
}

static inline bool activeAnode() {
  return previewActive ? previewAnode : ledSettings.commonAnode;
}

// ---------------------------------------------------------------------------
//  Colour selection
// ---------------------------------------------------------------------------
// The effect engine stays colour-blind: it produces an intensity envelope and
// this picks the hue that envelope is painted in. Branches of ledTick() that are
// not tied to the activity level (an HMS error episode raised mid-print, the
// portal's "Test effect" button) override the result explicitly.
static uint32_t activeColor() {
  switch (currentActivity) {
    case LED_ACT_PRINTING: return ledSettings.colorPrinting;
    case LED_ACT_PAUSED:   return ledSettings.colorPaused;
    case LED_ACT_FINISHED: return ledSettings.colorFinished;
    case LED_ACT_FAILED:   return ledSettings.colorError;
    default:               return ledSettings.colorIdle;
  }
}

// ---------------------------------------------------------------------------
//  Pin deny-list (board-specific peripheral conflicts)
// ---------------------------------------------------------------------------
bool isLedPinAllowed(uint8_t pin) {
  if (pin == 0) return false;

  // Generic dynamic conflicts (all boards)
  if (pin == BACKLIGHT_PIN) return false;
  if (buzzerSettings.enabled && pin == buzzerSettings.pin) return false;
  if (buttonType != BTN_DISABLED && pin == buttonPin) return false;

#if defined(BOARD_IS_DIY)
  // Standalone (not part of the board #elif chain below): a DIY+C3 build must
  // reject BOTH the DIY display pins here AND the C3 reserved pins in the chain.
  if (isDiyReservedPin(pin)) return false;
#endif

#if defined(DISPLAY_CYD) || defined(BOARD_IS_TZT_2432)
  // CYD (ESP32-2432S028) and TZT L1435-2.4 - same ESP32 pinout, both esp32dev
  if (pin == 2 || pin == 12 || pin == 13 || pin == 14 || pin == 15) return false;  // display SPI
  if (pin == 25 || pin == 32 || pin == 33 || pin == 36 || pin == 39) return false; // XPT2046 touch
  if (pin == 4 || pin == 16 || pin == 17) return false;                            // onboard RGB (32E clone: amp EN + G/B)
  if (pin == 26) return false;                                                     // onboard speaker amp
#if defined(DISPLAY_CYD)
  if (dispSettings.cyd32eVariant && pin == CYD32E_LED_R_PIN) return false;         // 32E clone: red LED moved to GPIO22
#endif
  if (pin >= 6 && pin <= 11) return false;                                         // SPI flash
  if (pin >= 34 && pin <= 39) return false;                                        // input-only
  if (pin > 39) return false;

#elif defined(BOARD_IS_S3_ZERO)
  // Waveshare ESP32-S3-Zero + external ST7789 240x240
  if (pin == 8 || pin == 9 || pin == 10 || pin == 11 || pin == 12) return false;  // display SPI
  if (pin == 13) return false;                                                     // display backlight
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin == 21) return false;                                                     // onboard WS2812 RGB LED
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM / not exposed
  if (pin > 48) return false;

#elif defined(BOARD_IS_S3)
  // LOLIN S3 mini + ST7789 240x240
  if (pin == 8 || pin == 9 || pin == 10 || pin == 11 || pin == 12) return false;   // display SPI
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM (qio_qspi)
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS200)
  // Waveshare ESP32-S3-Touch-LCD-2.0"
  if (pin == 38 || pin == 39 || pin == 40 || pin == 42 || pin == 45) return false; // display SPI
  if (pin == 47 || pin == 48) return false;                                        // CST816D I2C
  if (pin == 5) return false;                                                      // battery ADC (BAT_ADC_PIN)
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS280)
  // Waveshare ESP32-S3-Touch-LCD-2.8" (community, untested in-house)
  if (pin == 39 || pin == 40 || pin == 41 || pin == 42 || pin == 45) return false; // display SPI
  if (pin == 1 || pin == 2 || pin == 3 || pin == 4) return false;                  // CST328 I2C + RST/IRQ
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_ES3N28P)
  // QD ES3N28P 2.8" (ILI9341V SPI + FT6336 I2C + ES8311 audio)
  if (pin == 10 || pin == 11 || pin == 12 || pin == 13 || pin == 46) return false; // display SPI (CS/MOSI/SCLK/MISO/DC)
  if (pin == 45) return false;                                                     // backlight
  if (pin == 15 || pin == 16 || pin == 17 || pin == 18) return false;             // FT6336 touch I2C + INT/RST
  if (pin == 1 || pin == 4 || pin == 5 || pin == 6 || pin == 7 || pin == 8) return false; // ES8311 audio (AP_EN/MCK/BCK/DIN/WS/DOUT)
  if (pin == 9) return false;                                                      // battery ADC (ADC1_CH8)
  if (pin == 42) return false;                                                     // onboard WS2812 RGB
  if (pin == 38 || pin == 39 || pin == 40 || pin == 41 || pin == 47 || pin == 48) return false; // microSD (SDIO)
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM (qio_opi)
  if (pin > 48) return false;

#elif defined(BOARD_IS_SC05X)
  // Panlee SC05_X / ZX2D80CE02S (ST7789 8-bit 8080 parallel + FT5X06 I2C)
  if (pin == 1 || pin == 2 || pin == 7 ||
      pin == 15 || pin == 16 || pin == 17 || pin == 18 ||
      pin == 40 || pin == 41 || pin == 42) return false;                           // 8080 bus: D0-D7 + WR + RS/DC
  if (pin == 3) return false;                                                       // LCD reset
  if (pin == 47) return false;                                                      // backlight
  if (pin == 38) return false;                                                      // LCD_TE (frame sync)
  if (pin == 8 || pin == 9 || pin == 48) return false;                              // FT5X06 I2C + INT
  if (pin == 4 || pin == 5 || pin == 6) return false;                               // RS485
  if (pin == 19 || pin == 20) return false;                                         // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                         // SPI flash/PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS154)
  // Waveshare ESP32-S3-Touch-LCD-1.54"
  if (pin == 21 || pin == 38 || pin == 39 || pin == 40 || pin == 45) return false; // display SPI
  if (pin == 41 || pin == 42 || pin == 47 || pin == 48) return false;              // CST816 I2C + RST/IRQ
  if (pin == 8 || pin == 9 || pin == 10 || pin == 12) return false;                // ES8311 I2S
  if (pin == 7) return false;                                                      // audio PA ctrl
  if (pin == 2) return false;                                                      // BAT_EN
  if (pin == 0 || pin == 4 || pin == 5) return false;                              // board buttons
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM
  if (pin > 48) return false;

#elif defined(BOARD_IS_WS350)
  // Waveshare ESP32-S3-Touch-LCD-3.5 (ST7796 SPI + FT6336/TCA9554 I2C)
  if (pin == 1 || pin == 2 || pin == 3 || pin == 5) return false;                  // display SPI (MOSI/MISO/DC/SCLK)
  if (pin == 6) return false;                                                      // backlight
  if (pin == 7 || pin == 8) return false;                                          // I2C (FT6336 touch + TCA9554 expander)
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM (qio_opi)
  if (pin > 48) return false;

#elif defined(BOARD_IS_SC01PLUS)
  // Panlee WT32-SC01 Plus (ST7796 8-bit 8080 parallel + FT6336 I2C)
  if (pin == 0 || pin == 3 || pin == 8 || pin == 9 ||
      pin == 15 || pin == 16 || pin == 17 || pin == 18 ||
      pin == 46 || pin == 47) return false;                                        // 8080 bus: D0-D7 + WR + RS/DC
  if (pin == 4) return false;                                                      // LCD reset (mux'd with touch reset)
  if (pin == 45) return false;                                                     // backlight
  if (pin == 48) return false;                                                     // LCD_TE (frame sync)
  if (pin == 5 || pin == 6 || pin == 7) return false;                              // FT6336 I2C (SCL/SDA/INT)
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash/PSRAM (also I2S audio 35/36/37)
  if (pin > 48) return false;

#elif defined(BOARD_IS_JC3248W535)
  // Guition JC3248W535 (AXS15231B QSPI 320x480)
  if (pin == 21 || pin == 39 || pin == 40 || pin == 45 || pin == 47 || pin == 48) return false; // display QSPI
  if (pin == 38) return false;                                                     // display TE
  if (pin == 4 || pin == 8) return false;                                          // touch I2C
  if (pin == 2 || pin == 41 || pin == 42) return false;                            // NS4168 I2S audio
  if (pin == 19 || pin == 20) return false;                                        // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                                        // SPI flash + PSRAM (qio_opi)
  if (pin > 48) return false;

#elif defined(BOARD_IS_C3)
  // LOLIN C3 mini
  if (pin == 6 || pin == 7 || pin == 10 || pin == 20 || pin == 21) return false;   // display SPI
  if (pin == 18 || pin == 19) return false;                                        // USB CDC D-/D+
  if (pin >= 11 && pin <= 17) return false;                                        // flash/PSRAM
  if (pin > 21) return false;

#elif defined(BOARD_IS_WS28C)
  // Waveshare ESP32-S3-Touch-LCD-2.8C (ST7701 480x480 RGB)
  if (pin >= 8 && pin <= 21) return false;                       // RGB data D3-D10, D13-D15 + touch INT
  if (pin >= 1 && pin <= 5) return false;                        // SPI MOSI/SCLK, D12, battery ADC, D0
  if (pin >= 38 && pin <= 41) return false;                      // RGB HSYNC/VSYNC/DE/PCLK
  if (pin == 45 || pin == 46 || pin == 47 || pin == 48) return false; // RGB D1, D11, D3, D2
  if (pin == 6) return false;                                    // backlight PWM
  if (pin == 7 || pin == 15) return false;                       // I2C SCL / SDA (GT911 + TCA9554)
  if (pin == 19 || pin == 20) return false;                      // USB CDC D-/D+
  if (pin >= 26 && pin <= 37) return false;                      // SPI flash + PSRAM (qio_opi)
  if (pin > 48) return false;
  // Leaves 42/43/44 free (43/44 are UART0). LCD reset, LCD CS, touch reset and
  // the buzzer are expander bits, not GPIOs, so nothing else to reserve.

#elif defined(BOARD_IS_SENSECAP)
  // SenseCAP Indicator (ESP32-S3 + ST7701S 480x480 RGB)
  // RGB565 data pins: D0-D15 = GPIOs 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
  // RGB control: PCLK=21, VSYNC=17, HSYNC=16, DE=18
  if (pin <= 21) return false;                                   // All RGB data + control pins (0-21)
  // SPI for display init
  if (pin == 41 || pin == 48) return false;                      // SPI CLK, MOSI
  // I2C (shared by touch FT5X06 and PCA9535PW)
  if (pin == 39 || pin == 40) return false;                      // I2C SDA, SCL
  // Peripherals
  if (pin == 45) return false;                                   // backlight PWM
  if (pin == 38) return false;                                   // user button
  if (pin == 19 || pin == 20) return false;                                        // UART0 to RP2040 (buzzer is on RP2040 GPIO 19, not ESP32-S3 GPIO 19)
  // SPI flash + PSRAM (opi_qspi)
  if (pin >= 26 && pin <= 37) return false;
  // MISO not used but on SPI bus
  if (pin == 47) return false;
  if (pin > 48) return false;

#elif defined(BOARD_IS_DIY)
  // Last resort for a DIY env that named no chip target, so none of the
  // BOARD_IS_* branches above matched. isDiyReservedPin() (checked near the top
  // of this function) already covers the display lines plus the chip's flash /
  // PSRAM / USB pins; what is left is output capability, which only matters
  // here - GPIO 34-39 on a classic ESP32 are input-only, perfectly good as a
  // button but unable to drive an LED at all.
  #if defined(CONFIG_IDF_TARGET_ESP32)
  if (pin >= 34 && pin <= 39) return false;
  #endif
#endif

  return true;
}

// Named resolver: the pins the board's own onboard RGB hardware sits on. They
// are in the deny-list above because nothing else may steal them, but a colour
// driver IS their rightful owner, so it gets to claim them back.
static bool isOnboardRgbPin(uint8_t pin, uint8_t drv) {
#if BOARD_HAS_ONBOARD_RGB_PWM
  // The three-GPIO onboard LED is the PWM RGB driver's alone. A WS2812 driver
  // pointed at one of these pins is a misconfig, not a rightful claim-back, so it
  // must not be exempted from the deny-list.
  if (drv == LED_DRV_RGB) {
    if (pin == ONBOARD_RGB_G_PIN || pin == ONBOARD_RGB_B_PIN) return true;
#if defined(DISPLAY_CYD)
    // ESP32-32E clone: red moved to GPIO 22 and GPIO 4 became the speaker-amp
    // enable, so on that variant GPIO 4 is emphatically not ours.
    if (dispSettings.cyd32eVariant) return pin == CYD32E_LED_R_PIN;
#endif
    if (pin == ONBOARD_RGB_R_PIN) return true;
  }
#endif
#if BOARD_HAS_ONBOARD_RGB_PIXEL
  // ...and the single WS2812 data line is the pixel driver's alone.
  if (drv == LED_DRV_PIXEL && pin == ONBOARD_RGB_DATA_PIN) return true;
#endif
  (void)pin; (void)drv;
  return false;
}

// Onboard pins are claimed back only for the driver they physically belong to,
// and never when a live input has taken one over. The onboard exemption reverses
// the deny-list's *static* reservation; a button or buzzer the user has since
// parked on an onboard pin is a dynamic conflict that still wins.
static bool isRgbLedPinAllowedFor(uint8_t pin, uint8_t drv) {
  if (pin == 0) return false;
  if (buzzerSettings.enabled && pin == buzzerSettings.pin) return false;
  if (buttonType != BTN_DISABLED && pin == buttonPin) return false;
  if (isOnboardRgbPin(pin, drv)) return true;
  return isLedPinAllowed(pin);
}

bool isRgbLedPinAllowed(uint8_t pin) {
  return isRgbLedPinAllowedFor(pin, LED_DRV_RGB);
}

bool onboardRgbPins(uint8_t &r, uint8_t &g, uint8_t &b, bool &anode) {
#if BOARD_HAS_ONBOARD_RGB_PWM
  r = ONBOARD_RGB_R_PIN;
#if defined(DISPLAY_CYD)
  if (dispSettings.cyd32eVariant) r = CYD32E_LED_R_PIN;
#endif
  g = ONBOARD_RGB_G_PIN;
  b = ONBOARD_RGB_B_PIN;
  anode = (ONBOARD_RGB_ANODE != 0);
  return true;
#elif BOARD_HAS_ONBOARD_RGB_PIXEL
  r = ONBOARD_RGB_DATA_PIN;
  g = 0;
  b = 0;
  anode = false;
  return true;
#else
  (void)r; (void)g; (void)b; (void)anode;
  return false;
#endif
}

void sanitizeLedPin() {
  // Clamp effect fields first - independent of pin/enabled state, so NVS-loaded
  // garbage gets normalized even on the default disabled+pin=0 path.
  if (ledSettings.finishMode > LED_FINISH_HEARTBEAT) ledSettings.finishMode = LED_FINISH_OFF;
  if (ledSettings.finishSeconds < 5)   ledSettings.finishSeconds = 5;
  if (ledSettings.finishSeconds > 600) ledSettings.finishSeconds = 600;
  // errorStrobeSeconds: 0 = never time out; otherwise clamp to 5..600.
  if (ledSettings.errorStrobeSeconds != 0 && ledSettings.errorStrobeSeconds < 5)
    ledSettings.errorStrobeSeconds = 5;
  if (ledSettings.errorStrobeSeconds > 600) ledSettings.errorStrobeSeconds = 600;

  if (ledSettings.driver > LED_DRV_PIXEL) ledSettings.driver = LED_DRV_SINGLE;
#if !HAS_LED_PIXEL
  // Imported from a board that has the pixel driver: fall back rather than
  // silently pretending to drive something.
  if (ledSettings.driver == LED_DRV_PIXEL) ledSettings.driver = LED_DRV_SINGLE;
#endif
  ledSettings.colorIdle     &= 0xFFFFFF;
  ledSettings.colorPrinting &= 0xFFFFFF;
  ledSettings.colorPaused   &= 0xFFFFFF;
  ledSettings.colorFinished &= 0xFFFFFF;
  ledSettings.colorError    &= 0xFFFFFF;

  // Default unset state (disabled + pin=0) is valid - skip pin validation.
  if (!ledSettings.enabled && ledSettings.pin == 0) return;

  if (ledSettings.driver == LED_DRV_RGB) {
    // All three pins must be drivable and distinct - two channels sharing a pin
    // would fight over one LEDC output and light neither colour correctly.
    const bool ok = isRgbLedPinAllowed(ledSettings.pin) &&
                    isRgbLedPinAllowed(ledSettings.pinG) &&
                    isRgbLedPinAllowed(ledSettings.pinB) &&
                    ledSettings.pin  != ledSettings.pinG &&
                    ledSettings.pin  != ledSettings.pinB &&
                    ledSettings.pinG != ledSettings.pinB;
    if (!ok) {
      Serial.printf("LED: RGB pins %d/%d/%d not allowed, disabling\n",
                    ledSettings.pin, ledSettings.pinG, ledSettings.pinB);
      ledSettings.enabled = false;
      ledSettings.driver  = LED_DRV_SINGLE;
      ledSettings.pin = 0;
    }
    return;
  }

  // Single and pixel both hang off one pin; only the pixel may claim an onboard
  // RGB data line.
  const bool ok = (ledSettings.driver == LED_DRV_PIXEL)
                    ? isRgbLedPinAllowedFor(ledSettings.pin, LED_DRV_PIXEL)
                    : isLedPinAllowed(ledSettings.pin);
  if (!ok) {
    Serial.printf("LED: pin %d not allowed, disabling\n", ledSettings.pin);
    ledSettings.enabled = false;
    ledSettings.driver = LED_DRV_SINGLE;
    ledSettings.pin = 0;
  }
}

// ---------------------------------------------------------------------------
//  Low-level duty write (with change-detection to skip redundant SPI traffic)
// ---------------------------------------------------------------------------
static bool suspendedForSleep = false;

static inline uint8_t scaleChannel(uint8_t v, uint8_t duty) {
  return (uint8_t)(((uint16_t)v * (uint16_t)duty + 127u) / 255u);
}

// One WS2812 frame: 24 bits, GRB on the wire, 100 ns RMT tick so the durations
// below read as 0.4 us / 0.8 us. rmtWriteBlocking() takes ~30 us for the frame,
// which is nothing against the ~16 ms tick.
static void pixelWrite(uint8_t r, uint8_t g, uint8_t b) {
#if !HAS_LED_PIXEL
  (void)r; (void)g; (void)b;
  return;
#else
  if (!pixelRmt) return;
  rmt_data_t bits[24];
  const uint8_t ch[3] = { g, r, b };
  uint8_t i = 0;
  for (uint8_t c = 0; c < 3; c++) {
    for (int8_t bit = 7; bit >= 0; bit--) {
      const bool one = (ch[c] >> bit) & 1;
      bits[i].level0    = 1;
      bits[i].duration0 = one ? 8 : 4;
      bits[i].level1    = 0;
      bits[i].duration1 = one ? 4 : 8;
      i++;
    }
  }
  rmtWriteBlocking(pixelRmt, bits, 24);
#endif
}

// The single choke point for everything that reaches the hardware. duty is the
// intensity envelope, rgb the colour it is painted in (ignored in single mode).
static void writeOut(uint8_t duty, uint32_t rgb) {
  if (attachedPin < 0) return;
  // Display asleep: every write stays dark - unless the user is standing in the
  // portal asking to see the LED right now. Without the exception, configuring
  // the LED on a device whose panel has gone to sleep shows nothing at all and
  // reads as broken hardware. A real print finish still respects the sleep.
  if (suspendedForSleep && !previewActive && !finishEffectIsTest) duty = 0;
  lastAppliedDuty = (int16_t)duty;

  if (attachedDrv == LED_DRV_SINGLE) {
    if ((int32_t)duty == lastPushedOut) return;
    ledcWrite(LED_PWM_CH, duty);
    lastPushedOut = duty;
    return;
  }

  const uint8_t r = scaleChannel((rgb >> 16) & 0xFF, duty);
  const uint8_t g = scaleChannel((rgb >>  8) & 0xFF, duty);
  const uint8_t b = scaleChannel( rgb        & 0xFF, duty);
  const int32_t packed = ((int32_t)r << 16) | ((int32_t)g << 8) | (int32_t)b;
  if (packed == lastPushedOut) return;
  lastPushedOut = packed;

  if (attachedDrv == LED_DRV_RGB) {
    // Common anode: the shared leg is on 3V3, so the GPIO sinks the current and
    // the duty has to be inverted or the LED reads as a photographic negative.
    const bool inv = activeAnode();
    ledcWrite(LED_PWM_CH,   inv ? (uint8_t)(255 - r) : r);
    ledcWrite(LED_PWM_CH_G, inv ? (uint8_t)(255 - g) : g);
    ledcWrite(LED_PWM_CH_B, inv ? (uint8_t)(255 - b) : b);
  } else {
    pixelWrite(r, g, b);
  }
}

static void writeDuty(uint8_t duty) {
  writeOut(duty, activeColor());
}

// Display-sleep coupling: while the panel is in SCREEN_OFF the status LED goes
// dark too, and the clamp in writeOut() keeps a mid-sleep effect from
// relighting it. On wake the resting brightness comes back immediately (a
// running effect overwrites it on its next tick).
void ledSetSuspended(bool suspended) {
  if (suspended == suspendedForSleep) return;
  suspendedForSleep = suspended;
  if (attachedPin < 0) return;
  writeDuty(suspended ? 0 : restingBrightness());
}

// Park one released pin at its dark level. On a common-anode wiring that level
// is HIGH, and driving it LOW instead would leave the channel at full blast -
// the exact opposite of releasing it.
static void parkPin(int8_t pin, bool activeLow) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, activeLow ? HIGH : LOW);
}

static void detachAndForceLow() {
  if (attachedPin < 0) return;

  switch (attachedDrv) {
    case LED_DRV_RGB: {
      const bool inv = attachedAnode;
      ledcWrite(LED_PWM_CH,   inv ? 255 : 0);
      ledcWrite(LED_PWM_CH_G, inv ? 255 : 0);
      ledcWrite(LED_PWM_CH_B, inv ? 255 : 0);
      ledcDetachPin(attachedPin);
      ledcDetachPin(attachedPinG);
      ledcDetachPin(attachedPinB);
      parkPin(attachedPin,  inv);
      parkPin(attachedPinG, inv);
      parkPin(attachedPinB, inv);
      break;
    }
    case LED_DRV_PIXEL:
      pixelWrite(0, 0, 0);       // blank it before the RMT channel goes away
#if HAS_LED_PIXEL
      if (pixelRmt) { rmtDeinit(pixelRmt); pixelRmt = NULL; }
#endif
      parkPin(attachedPin, false);
      break;
    default:
      ledcWrite(LED_PWM_CH, 0);
      ledcDetachPin(attachedPin);
      parkPin(attachedPin, false);
      break;
  }

  attachedPin  = -1;
  attachedPinG = -1;
  attachedPinB = -1;
  attachedDrv  = LED_DRV_SINGLE;
  lastAppliedDuty = -1;
  lastPushedOut   = -1;
}

// Bring up whichever driver the config asks for. Returns false and leaves
// nothing attached if the hardware refuses (only the RMT channel can).
static bool attachDriver(uint8_t drv, uint8_t pinR, uint8_t pinG, uint8_t pinB) {
  switch (drv) {
    case LED_DRV_RGB:
      ledcSetup(LED_PWM_CH,   LED_PWM_FREQ, LED_PWM_RES);
      ledcSetup(LED_PWM_CH_G, LED_PWM_FREQ, LED_PWM_RES);
      ledcSetup(LED_PWM_CH_B, LED_PWM_FREQ, LED_PWM_RES);
      ledcAttachPin(pinR, LED_PWM_CH);
      ledcAttachPin(pinG, LED_PWM_CH_G);
      ledcAttachPin(pinB, LED_PWM_CH_B);
      attachedPinG = (int8_t)pinG;
      attachedPinB = (int8_t)pinB;
      break;

    case LED_DRV_PIXEL:
#if !HAS_LED_PIXEL
      return false;               // driver compiled out on this board
#else
      pixelRmt = rmtInit(pinR, RMT_TX_MODE, RMT_MEM_64);
      if (!pixelRmt) {
        Serial.printf("LED: RMT init failed for WS2812 on pin %d\n", pinR);
        return false;
      }
      rmtSetTick(pixelRmt, 100);   // 100 ns per duration unit
      break;
#endif

    default:
      ledcSetup(LED_PWM_CH, LED_PWM_FREQ, LED_PWM_RES);
      ledcAttachPin(pinR, LED_PWM_CH);
      break;
  }

  attachedDrv     = drv;
  attachedPin     = (int8_t)pinR;
  attachedAnode   = activeAnode();
  lastAppliedDuty = -1;
  lastPushedOut   = -1;
  return true;
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
void initLed() {
  // Cancel any in-flight effect from previous config (e.g. user changed pin
  // while the finish-pulse was still running).
  finishEffectActive = false;
  finishEffectIsTest = false;
  currentActivity = LED_ACT_IDLE;
  previewActive = false;
  errorStrobeArmed = false;
  errorStrobeDismissed = false;

#if BOARD_HAS_ONBOARD_RGB_PWM
  // The onboard RGB pins float at boot, and on a common-anode LED a floating
  // pin is a dimly-lit channel - which is why an untouched CYD sits there
  // glowing red. Park the whole triple at its dark level. Both panel variants
  // need this, not just the 32E clone: they differ only in which pin is red
  // (GPIO 4 classic, GPIO 22 on the clone, where GPIO 4 became the amp enable
  // and belongs to the buzzer). Skipped when a colour driver owns those pins -
  // the attach below is about to drive them itself.
  uint8_t obR = 0, obG = 0, obB = 0; bool obAnode = false;
  onboardRgbPins(obR, obG, obB, obAnode);
  // Skip the park only when an enabled RGB driver is wired to this exact onboard
  // triple - the attach below is about to drive those pins itself. An RGB driver
  // on unrelated external pins leaves the onboard LED unowned, so it still needs
  // parking or it floats and glows.
  const bool colorDriverOwnsOnboard =
      ledSettings.enabled && ledSettings.driver == LED_DRV_RGB &&
      ledSettings.pin == obR && ledSettings.pinG == obG && ledSettings.pinB == obB;
  if (!colorDriverOwnsOnboard) {
    parkPin((int8_t)obR, obAnode);
    parkPin((int8_t)obG, obAnode);
    parkPin((int8_t)obB, obAnode);
  }
#endif

  detachAndForceLow();
  // Disabled with a saved pin: hold the wiring at its OFF level instead of leaving
  // it high-Z, so a floating gate cannot half-light the LED. What "off" means
  // depends on the driver and polarity - a common-anode RGB is off with its
  // channels HIGH, so the single active-high LOW that used to run here would have
  // lit red. Only touch allowed pins - never poke peripherals (SPI, touch, etc.).
  if (!ledSettings.enabled) {
    if (ledSettings.pin != 0) {
      if (ledSettings.driver == LED_DRV_RGB) {
        const bool inv = ledSettings.commonAnode;
        if (isRgbLedPinAllowed(ledSettings.pin))  parkPin(ledSettings.pin,  inv);
        if (isRgbLedPinAllowed(ledSettings.pinG)) parkPin(ledSettings.pinG, inv);
        if (isRgbLedPinAllowed(ledSettings.pinB)) parkPin(ledSettings.pinB, inv);
      } else if (ledSettings.driver == LED_DRV_PIXEL) {
#if HAS_LED_PIXEL
        // A warm reset does not power-cycle a WS2812, so it can boot still lit
        // from a previous config, and detachAndForceLow() above only fired if a
        // driver was already attached. Bring the driver up just to push one off
        // frame, then tear it straight down (which also parks the line low).
        if (isRgbLedPinAllowedFor(ledSettings.pin, LED_DRV_PIXEL) &&
            attachDriver(LED_DRV_PIXEL, ledSettings.pin, 0, 0)) {
          detachAndForceLow();
        }
#endif
      } else if (isLedPinAllowed(ledSettings.pin)) {
        parkPin(ledSettings.pin, false);   // single active-high LED
      }
    }
    return;
  }

  if (ledSettings.driver == LED_DRV_RGB) {
    if (!isRgbLedPinAllowed(ledSettings.pin) ||
        !isRgbLedPinAllowed(ledSettings.pinG) ||
        !isRgbLedPinAllowed(ledSettings.pinB)) return;
  } else if (ledSettings.driver == LED_DRV_PIXEL) {
    if (!isRgbLedPinAllowedFor(ledSettings.pin, LED_DRV_PIXEL)) return;
  } else if (!isLedPinAllowed(ledSettings.pin)) {
    return;
  }

  if (!attachDriver(ledSettings.driver, ledSettings.pin,
                    ledSettings.pinG, ledSettings.pinB)) return;
  writeDuty(ledSettings.brightness);
}

void applyLedDuty(uint8_t duty) {
  if (attachedPin >= 0 && ledSettings.enabled) writeDuty(duty);
}

void previewLed(bool enabled, uint8_t driver, uint8_t pinR, uint8_t pinG,
                uint8_t pinB, bool commonAnode, uint8_t brightness, uint32_t color) {
  // Preview overrides any active effect - user is actively configuring.
  finishEffectActive = false;
  finishEffectIsTest = false;

  if (driver > LED_DRV_PIXEL) driver = LED_DRV_SINGLE;

  bool pinsOk;
  if (driver == LED_DRV_RGB) {
    pinsOk = isRgbLedPinAllowed(pinR) && isRgbLedPinAllowed(pinG) &&
             isRgbLedPinAllowed(pinB) &&
             pinR != pinG && pinR != pinB && pinG != pinB;
  } else if (driver == LED_DRV_PIXEL) {
    pinsOk = isRgbLedPinAllowedFor(pinR, LED_DRV_PIXEL);
  } else {
    pinsOk = isLedPinAllowed(pinR);
  }

  if (!enabled || !pinsOk) {
    previewActive = false;
    detachAndForceLow();
    return;
  }

  // Latch the preview values first so ledTick()'s resting path uses them instead
  // of the stale saved ones (which would otherwise overwrite the preview every
  // 16ms and flicker the LED) - and so attachDriver() below latches the form's
  // polarity, not the saved one. detachAndForceLow() still parks the outgoing
  // pins the way they were actually wired, from attachedAnode.
  previewActive     = true;
  previewAnode      = commonAnode;
  previewBrightness = brightness;
  previewColor      = color & 0xFFFFFF;

  const bool sameRig = (attachedDrv == driver) && (attachedPin == (int8_t)pinR) &&
                       (driver != LED_DRV_RGB ||
                        (attachedPinG == (int8_t)pinG && attachedPinB == (int8_t)pinB));
  if (!sameRig) {
    detachAndForceLow();
    if (!attachDriver(driver, pinR, pinG, pinB)) {
      previewActive = false;
      return;
    }
  }
  // Flipping only the polarity leaves the pre-inversion colour identical, which
  // the change-detection in writeOut() would read as "nothing to do".
  lastPushedOut = -1;
  writeOut(brightness, previewColor);
}

// ---------------------------------------------------------------------------
//  Effect engine
// ---------------------------------------------------------------------------
void ledSetActivity(LedActivity act) {
  currentActivity = act;
}

void ledStartFinishEffect() {
  if (!ledSettings.enabled) return;
  if (ledSettings.finishMode == LED_FINISH_OFF) return;
  if (attachedPin < 0) return;
  finishEffectMode    = ledSettings.finishMode;
  finishEffectPeak    = ledSettings.finishBrightness;
  finishEffectColor   = ledSettings.colorFinished;
  finishEffectIsTest  = false;
  finishEffectStartMs = millis();
  finishEffectEndMs   = finishEffectStartMs + (unsigned long)ledSettings.finishSeconds * 1000UL;
  finishEffectActive  = true;
}

void ledStopFinishEffect() {
  if (!finishEffectActive) return;
  finishEffectActive = false;
  finishEffectIsTest = false;
  // Force tick to recompute resting duty (idle/auto-on/etc.) on next call.
  lastPushedOut = -1;
}

bool ledTriggerTestEffect(uint8_t mode, uint16_t seconds, uint8_t peakBrightness,
                          uint32_t color) {
  // attachedPin is the right gate (not ledSettings.enabled) so the test works
  // for a previewed-but-unsaved LED config; ledTick() also runs during preview.
  if (attachedPin < 0) return false;
  if (mode == 0) mode = ledSettings.finishMode;
  if (mode == LED_FINISH_OFF) return false;
  if (mode > LED_FINISH_HEARTBEAT) return false;
  if (seconds < 1) seconds = 1;
  if (seconds > 600) seconds = 600;
  finishEffectMode    = mode;
  finishEffectPeak    = peakBrightness;
  finishEffectColor   = (color == 0xFFFFFFFFUL) ? ledSettings.colorFinished
                                                : (color & 0xFFFFFF);
  finishEffectIsTest  = true;
  finishEffectStartMs = millis();
  finishEffectEndMs   = finishEffectStartMs + (unsigned long)seconds * 1000UL;
  finishEffectActive  = true;
  return true;
}

#if HAS_HMS_UI
void ledStartErrorEpisode() {
  if (!ledSettings.enabled || attachedPin < 0) return;
  if (!ledSettings.errorStrobe) return;   // same opt-out as the FAILED strobe
  const uint16_t secs = ledSettings.errorStrobeSeconds ? ledSettings.errorStrobeSeconds
                                                       : LED_ERROR_EPISODE_S;
  errorEpisodeEndMs = millis() + (unsigned long)secs * 1000UL;
  if (errorEpisodeEndMs == 0) errorEpisodeEndMs = 1;   // 0 is the "none" sentinel
  lastPushedOut = -1;
}

void ledStopErrorEpisode() {
  if (errorEpisodeEndMs == 0) return;
  errorEpisodeEndMs = 0;
  lastPushedOut = -1;   // force resting duty recompute on the next tick
}
#endif  // HAS_HMS_UI

void ledOnUserInteraction() {
  if (finishEffectActive) ledStopFinishEffect();
  // Button / touch / wake also silences an active error strobe - user has seen
  // the fault. Stays dismissed until the activity leaves FAILED and a new fault
  // re-arms it.
  if (errorStrobeArmed && !errorStrobeDismissed) {
    errorStrobeDismissed = true;
    lastPushedOut = -1;  // force resting duty recompute on next tick
  }
#if HAS_HMS_UI
  // Same for a one-shot error episode - it just ends, there is nothing to
  // re-arm.
  if (errorEpisodeEndMs != 0) {
    errorEpisodeEndMs = 0;
    lastPushedOut = -1;
  }
#endif
}

// ---------------------------------------------------------------------------
//  Pattern generators (all return 0..peak)
// ---------------------------------------------------------------------------
static uint8_t patternBreath(unsigned long phaseMs, uint16_t periodMs, uint8_t peak) {
  if (periodMs == 0) return 0;
  float t = (float)(phaseMs % periodMs) / (float)periodMs;       // 0..1
  float v = 0.5f - 0.5f * cosf(t * 2.0f * (float)M_PI);          // 0..1 sinus
  return (uint8_t)(v * (float)peak);
}

// Heartbeat: two short pulses then a pause, repeating.
// Within a 1500ms window (LED_HEARTBEAT_PERIOD_MS):
//   0..150   ramp up to peak  (first beat rising)
//   150..300 ramp down to 0   (first beat falling)
//   300..450 ramp up to peak  (second beat rising)
//   450..600 ramp down to 0   (second beat falling)
//   600..1500 silence
static uint8_t patternHeartbeat(unsigned long phaseMs, uint16_t periodMs, uint8_t peak) {
  if (periodMs == 0) return 0;
  unsigned long p = phaseMs % periodMs;
  if (p < 150)  return (uint8_t)((p * peak) / 150);
  if (p < 300)  return (uint8_t)(((300 - p) * peak) / 150);
  if (p < 450)  return (uint8_t)(((p - 300) * peak) / 150);
  if (p < 600)  return (uint8_t)(((600 - p) * peak) / 150);
  return 0;
}

static uint8_t patternStrobe(unsigned long phaseMs, uint16_t halfMs, uint8_t peak) {
  if (halfMs == 0) return peak;
  return ((phaseMs / halfMs) & 1) ? 0 : peak;
}

// ---------------------------------------------------------------------------
//  Tick (called from main loop)
// ---------------------------------------------------------------------------
void ledTick() {
  // Run when saved-on OR while a preview is active, so the test effect can
  // animate for a previewed-but-unsaved LED config.
  if (!ledSettings.enabled && !previewActive) return;
  if (attachedPin < 0) return;

  // Hold-to-dim owns the duty while active - skip the normal priority stack so
  // strobe / breath / auto-on don't fight the ramp. Aborts always clear the
  // state machine back to DIM_IDLE, so this early-return can't get stuck.
  if (dimState == DIM_ACTIVE) return;

  unsigned long now = millis();
  if (now - lastTickMs < LED_TICK_MIN_INTERVAL_MS) return;
  lastTickMs = now;

  // Auto-stop finish effect on timeout.
  if (finishEffectActive && (long)(now - finishEffectEndMs) >= 0) {
    finishEffectActive = false;
    finishEffectIsTest = false;
    lastPushedOut = -1;  // force resting duty on this tick
  }

  uint8_t duty;

  // Priority order, high to low:
  // 1) Error strobe (FAILED) - real printer fault, beats config UX
  // 2) Finish effect (one-shot timed) - and the "Test effect" button uses it
  // 3) Live preview - user is actively dragging the brightness slider; must
  //    bypass auto-on/pause-breath so they don't gate the LED to 0 mid-drag
  // 4) Pause breathing (continuous while paused)
  // 5) Auto on/off based on activity
  // 6) Idle: configured brightness
  // Error-strobe arming / auto-off. Arm on the first FAILED tick of an episode;
  // re-arm only after the activity has left FAILED. errorStrobeSeconds == 0 means
  // strobe until dismissed (button) or the fault clears.
  if (currentActivity == LED_ACT_FAILED) {
    if (!errorStrobeArmed) {
      errorStrobeArmed = true;
      errorStrobeDismissed = false;
      errorStrobeEndMs = now + (unsigned long)ledSettings.errorStrobeSeconds * 1000UL;
    }
    if (!errorStrobeDismissed && ledSettings.errorStrobeSeconds > 0 &&
        (long)(now - errorStrobeEndMs) >= 0) {
      errorStrobeDismissed = true;
      lastPushedOut = -1;  // force resting duty on this tick
    }
  } else {
    errorStrobeArmed = false;
  }

#if HAS_HMS_UI
  if (errorEpisodeEndMs != 0 && (long)(now - errorEpisodeEndMs) >= 0) {
    errorEpisodeEndMs = 0;
    lastPushedOut = -1;
  }
#endif

  // The colour follows the same priority stack as the duty. activeColor() is
  // right for every branch driven by the activity level; the three that are not
  // - an HMS episode raised while the printer is still PRINTING, the finish
  // one-shot (whose colour is latched, so the portal can test an unsaved one),
  // and a live preview - say so explicitly.
  uint8_t baseBrightness = restingBrightness();
  uint32_t color = activeColor();
#if HAS_HMS_UI
  if (errorEpisodeEndMs != 0) {
    duty = patternStrobe(now, LED_ERROR_STROBE_MS, baseBrightness);
    color = ledSettings.colorError;
  } else
#endif
  if (ledSettings.errorStrobe && currentActivity == LED_ACT_FAILED && !errorStrobeDismissed) {
    duty = patternStrobe(now, LED_ERROR_STROBE_MS, baseBrightness);
  } else if (finishEffectActive) {
    unsigned long phase = now - finishEffectStartMs;
    if (finishEffectMode == LED_FINISH_HEARTBEAT) {
      duty = patternHeartbeat(phase, LED_HEARTBEAT_PERIOD_MS, finishEffectPeak);
    } else {
      duty = patternBreath(phase, LED_BREATH_PERIOD_MS, finishEffectPeak);
    }
    color = finishEffectColor;
  } else if (previewActive) {
    duty = previewBrightness;
    color = previewColor;
  } else if (ledSettings.pauseBreathing && currentActivity == LED_ACT_PAUSED) {
    duty = patternBreath(now, LED_PAUSE_PERIOD_MS, baseBrightness);
  } else if (ledSettings.autoOnWhilePrinting) {
    duty = (currentActivity == LED_ACT_PRINTING) ? baseBrightness : 0;
  } else {
    duty = baseBrightness;
  }

  writeOut(duty, color);
}

// ---------------------------------------------------------------------------
//  Hold-to-dim
// ---------------------------------------------------------------------------
bool ledHoldDimUpdate(bool heldNow, uint32_t holdMs, bool suppressDim) {
  uint32_t now = millis();

  // Drain a pending save regardless of state. saveLedSettings() runs even if
  // ledSettings.enabled was toggled off after the dim - it persists the current
  // RAM state, which is what the user just configured.
  if (dimState == DIM_IDLE && dimSaveAtMs != 0 && (int32_t)(now - dimSaveAtMs) >= 0) {
    saveLedSettings();
    dimSaveAtMs = 0;
  }

  if (dimState == DIM_ACTIVE) {
    bool mustAbort = !heldNow || suppressDim || !ledSettings.enabled ||
                     attachedPin < 0 || previewActive;

    if (mustAbort) {
      if (!heldNow) {
        // Clean release: flip direction, schedule save.
        dimDirection = -dimDirection;
        dimSaveAtMs = now + LED_SAVE_DEBOUNCE_MS;
        if (dimSaveAtMs == 0) dimSaveAtMs = 1;  // 0 sentinel: never zero accidentally
      }
      // Forced abort path keeps the in-RAM brightness change but skips the save
      // and the direction flip - the user didn't get a deliberate dim session.
      dimState = DIM_IDLE;
      return true;  // press was consumed by hold, still suppress tap actions
    }

    if ((int32_t)(now - dimLastStepMs) >= (int32_t)DIM_STEP_INTERVAL_MS) {
      int next = (int)dimWorkingDuty + (int)dimDirection * (int)DIM_STEP;
      if (next < (int)LED_MIN_BRIGHTNESS_DIM) next = LED_MIN_BRIGHTNESS_DIM;
      if (next > 255) next = 255;
      dimWorkingDuty = (uint8_t)next;
      applyLedDuty(dimWorkingDuty);
      ledSettings.brightness = dimWorkingDuty;
      dimLastStepMs = now;
    }
    return true;
  }

  // DIM_IDLE entry guard - only start a new session if every precondition holds.
  if (heldNow && !suppressDim && ledSettings.enabled && attachedPin >= 0 &&
      !previewActive && holdMs >= HOLD_THRESHOLD_MS) {
    dimSaveAtMs = 0;
    int seed = (lastAppliedDuty >= 0) ? lastAppliedDuty : (int)ledSettings.brightness;
    if (seed < (int)LED_MIN_BRIGHTNESS_DIM) seed = LED_MIN_BRIGHTNESS_DIM;
    if (seed > 255) seed = 255;
    dimWorkingDuty = (uint8_t)seed;
    dimLastStepMs = now;
    dimState = DIM_ACTIVE;
    return true;
  }

  return false;
}
