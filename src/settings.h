#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "bambu_state.h"

// Gauge type identifiers for configurable slot layout
enum GaugeType : uint8_t {
  GAUGE_EMPTY       = 0,
  GAUGE_PROGRESS    = 1,
  GAUGE_NOZZLE      = 2,
  GAUGE_BED         = 3,
  GAUGE_PART_FAN    = 4,
  GAUGE_AUX_FAN     = 5,
  GAUGE_CHAMBER_FAN = 6,
  GAUGE_CHAMBER_TEMP= 7,
  GAUGE_HEATBREAK   = 8,
  GAUGE_CLOCK       = 9,
  GAUGE_AMS_HUM_1   = 10,  // AMS unit 1 humidity
  GAUGE_AMS_HUM_2   = 11,  // AMS unit 2 humidity
  GAUGE_AMS_HUM_3   = 12,  // AMS unit 3 humidity
  GAUGE_AMS_HUM_4   = 13,  // AMS unit 4 humidity
  GAUGE_LAYER       = 14,  // layer progress (current/total)
  GAUGE_AMS_TEMP_1  = 15,  // AMS unit 1 temperature
  GAUGE_AMS_TEMP_2  = 16,  // AMS unit 2 temperature
  GAUGE_AMS_TEMP_3  = 17,  // AMS unit 3 temperature
  GAUGE_AMS_TEMP_4  = 18,  // AMS unit 4 temperature
  GAUGE_AMS_FILAMENT_1 = 19,    // AMS unit 1 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_2 = 20,    // AMS unit 2 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_3 = 21,    // AMS unit 3 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_4 = 22,    // AMS unit 4 - all 4 trays + humidity
  GAUGE_AUX_FAN_RIGHT  = 23,    // X2D right aux fan (device.airduct.parts func=6)
  GAUGE_EXHAUST_FAN    = 24,    // X2D exhaust fan (device.airduct.parts func=2)
  GAUGE_AMS_BARS_1     = 25,    // AMS unit 1 - 4 vertical color bars (no humidity)
  GAUGE_AMS_BARS_2     = 26,    // AMS unit 2
  GAUGE_AMS_BARS_3     = 27,    // AMS unit 3
  GAUGE_AMS_BARS_4     = 28,    // AMS unit 4
  GAUGE_NOZZLE_RIGHT   = 29,    // dual-nozzle (H2D/H2C/X2D): right extruder (id 0), fixed side
  GAUGE_NOZZLE_LEFT    = 30,    // dual-nozzle: left extruder (id 1), fixed side
  GAUGE_POWER          = 31,    // Tasmota smart-plug live power draw (W / kW)
  GAUGE_TYPE_COUNT  // sentinel - always last
};

// Standard 2x3 gauge grid - the 6 slots are used in every mode.
static const uint8_t GAUGE_SLOT_COUNT = 6;
// Extended grids add a 4th column (landscape) or 3rd row (portrait).
// Each extension stores its slots in its own PrinterConfig array so the gauge
// type for "col 4 row 1" and "row 3 col 1" can be set independently.
static const uint8_t LANDSCAPE_EXTRA_COUNT = 2;  // col 4 top, col 4 bot
static const uint8_t PORTRAIT_EXTRA_COUNT  = 3;  // row 3 left, mid, right
// Max slots displayed by any mode at once. Used to size per-frame slot caches
// in display_ui.cpp (prevSlotTypes, SlotGrid arrays).
static const uint8_t GAUGE_SLOT_MAX = GAUGE_SLOT_COUNT + PORTRAIT_EXTRA_COUNT;  // 9

// Per-gauge color config
struct GaugeColors {
  uint16_t arc;       // arc fill color (RGB565)
  uint16_t label;     // label text color
  uint16_t value;     // value text color
};

// All display customization settings
struct DisplaySettings {
  uint8_t  rotation;       // 0, 1, 2, 3 (x90 degrees)
  uint16_t bgColor;        // background color
  uint16_t trackColor;     // inactive arc track color
  uint16_t progressBarColor; // top LED progress bar fill color (independent of Progress gauge arc)
  bool     animatedBar;       // shimmer effect on progress bar
  bool     pongClock;         // Pong/Breakout animated clock
  bool     smallLabels;       // use smaller gauge labels (Font 1 instead of Font 2)
  bool     showTimeRemaining; // always show time remaining instead of ETA
  bool     fanMatchPrinter;   // round fan % to 10% steps to match printer LCD (else 1% precision from fan_gear)
  bool     invertColors;   // invert display colors (fixes white-bg on some panels)
  bool     cydPanelClassic; // CYD only: use plain Panel_ILI9341 (no inversion)
                            // instead of Panel_ILI9341_2 — for the other
                            // hardware revision that shows mirrored image.
  bool     landscape8Slots; // 240x320 landscape: replace AMS sidebar with a
                            // symmetric 2x4 gauge grid (8 slots, no sidebar).
  bool     portrait9Slots;  // 240x320 / 320x480 portrait: replace AMS strip
                            // with a 3x3 gauge grid (9 slots, smaller R).
  uint16_t clockTimeColor; // clock digits color (RGB565)
  uint16_t clockDateColor; // clock date/AM-PM color (RGB565)
  uint8_t  clockTimeSize;  // 0=Auto, 1=Normal(1.0x), 2=Medium(1.5x), 3=Large(2.0x). Auto = Large on >=480, Medium on >=320, else Normal.
  bool     hideClockDate;  // minimalist mode: time only, no date line
  bool     showClockInfo;  // footer on the idle/clock screen: each configured printer's name + LAN IP
  bool     amsTrayTypes;   // show per-tray filament-type label under AMS bars (portrait strip); off = taller bars, no text
  bool     showBatteryIndicator; // Waveshare boards: show battery icon in status bar
  // Gauge full-scale ranges (arc maxima). Lowering a scale makes the arc sweep
  // fuller for a printer's normal range. Defaults suit any Bambu printer.
  uint16_t nozzleScaleMax;   // C
  uint16_t bedScaleMax;      // C
  uint16_t chamberScaleMax;  // C (also drives the AMS unit-temp gauge)
  uint16_t powerScaleW;      // W (Tasmota power gauge full-scale)
  uint8_t  gaugeSmoothing;   // arc easing speed: 0=Off(instant) 1=Slow 2=Normal 3=Fast
  uint16_t warnColor;        // temp-gauge over-threshold color (arc + value), RGB565
  uint8_t  warnThresholdPct; // temp gauge turns warnColor at >= this % of scale; 0 = off
  GaugeColors progress;
  GaugeColors nozzle;
  GaugeColors bed;
  GaugeColors partFan;
  GaugeColors auxFan;
  GaugeColors auxFanRight;  // X2D right aux fan
  GaugeColors chamberFan;
  GaugeColors exhaustFan;   // X2D exhaust fan
  GaugeColors chamberTemp;
  GaugeColors heatbreak;
};

// Network settings
struct NetworkSettings {
  bool useDHCP;           // true = DHCP, false = static
  char staticIP[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  bool showIPAtStartup;   // show IP screen for 3s after WiFi connects
  uint8_t timezoneIndex;  // index into timezoneDatabase[]
  char timezoneStr[64];   // POSIX TZ string (e.g. "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00")
  bool use24h;            // true = 24h format (default), false = 12h AM/PM
  uint8_t dateFormat;     // 0=DD.MM.YYYY, 1=DD-MM-YYYY, 2=MM/DD/YYYY, 3=YYYY-MM-DD, 4=DD MMM YYYY, 5=MMM DD, YYYY
  bool mdnsEnabled;       // advertise <hostname>.local over mDNS (default false)
  char hostname[32];      // mDNS/host label, sanitized [a-z0-9-], default "bambuhelper"
};

// Display power settings
struct DisplayPowerSettings {
  uint16_t finishDisplayMins;  // minutes to show finish screen (0 = keep on)
  bool keepDisplayOn;          // override: never turn off display
  bool showClockAfterFinish;   // show clock instead of turning display off
  bool doorAckEnabled;         // wait for door open after print finish before timeout
  bool keepPrintScreen;        // show printing screen instead of finish screen after print
  // Night mode (scheduled dimming)
  bool nightModeEnabled;       // enable time-based dimming
  uint8_t nightStartHour;      // dim start hour (0-23)
  uint8_t nightEndHour;        // dim end hour (0-23)
  uint8_t nightBrightness;     // brightness during night (0-255)
  // Screensaver dimming (idle/clock screen)
  uint8_t screensaverBrightness; // brightness when clock/screensaver is active (0-255)
};

// Button type
enum ButtonType : uint8_t { BTN_DISABLED = 0, BTN_PUSH = 1, BTN_TOUCH = 2, BTN_TOUCHSCREEN = 3 };

// Buzzer settings
struct BuzzerSettings {
  bool enabled;
  uint8_t pin;
  uint8_t quietStartHour;   // quiet hours start (0-23), 0 = disabled
  uint8_t quietEndHour;     // quiet hours end (0-23)
  bool buttonClick;          // play click sound on button press
  bool bedCooldownAlert;          // play second alert when bed cools after print
  uint8_t bedCooldownThresholdC;  // bed temperature threshold (20-80 C)
};

// External LED settings (optional, PWM dimmable)
enum LedFinishMode : uint8_t {
  LED_FINISH_OFF       = 0,
  LED_FINISH_BREATHING = 1,
  LED_FINISH_HEARTBEAT = 2,
};

struct LedSettings {
  bool    enabled;
  uint8_t pin;
  uint8_t brightness;          // 0-255, persisted "working" level

  // Print-finished one-shot effect
  uint8_t  finishMode;         // LedFinishMode
  uint16_t finishSeconds;      // 5..600
  uint8_t  finishBrightness;   // 0..255 peak

  // Continuous state-driven behaviors
  bool autoOnWhilePrinting;    // LED on only while printer is printing
  bool pauseBreathing;         // slow breath during GCODE_PAUSE
  bool errorStrobe;            // fast strobe during GCODE_FAILED
};

// Tasmota smart plug power monitoring
// Dual plug on full-RAM builds, single plug on BOARD_LOW_RAM (CYD/tzt_2432/esp32c3).
#ifdef BOARD_LOW_RAM
  #define TASMOTA_PLUG_COUNT 1
#else
  #define TASMOTA_PLUG_COUNT 2
#endif

struct TasmotaSettings {
  bool    enabled;
  uint8_t plugType;          // 0=Tasmota, 1=Shelly Gen2/Gen3 (same RPC API)
  char    ip[16];
  uint8_t displayMode;       // 0=alternate layers/power every 4s, 1=always show power
  uint8_t pollInterval;      // poll interval in seconds (10-60)
#if TASMOTA_PLUG_COUNT == 1
  uint8_t assignedSlot;      // single-plug builds: which printer slot (0, 1, ... or 255=any)
#endif
  bool    autoOffEnabled;    // power off plug N minutes after FINISH and cooldown
  uint8_t autoOffDelayMin;   // minutes after FINISH (1-240)
  bool    autoOffCancelOnDoor; // cancel this auto-off cycle if door opens while waiting
};

extern char wifiSSID[33];
extern char wifiPass[65];
extern uint8_t brightness;
extern DisplaySettings dispSettings;
extern NetworkSettings netSettings;
extern DisplayPowerSettings dpSettings;
extern ButtonType buttonType;
extern uint8_t buttonPin;
extern BuzzerSettings buzzerSettings;
extern LedSettings ledSettings;
extern TasmotaSettings tasmotaSettings[TASMOTA_PLUG_COUNT];
extern char  tasmotaCurrency[8];      // e.g. "€", "$", "zł"
extern float tasmotaTariffPerKwh;     // global tariff (same for all plugs)
extern bool dualPrinterUnsafe;
extern bool quadPrinterBeta;

void loadSettings();
void saveSettings();

// Normalize a string into a valid mDNS/DNS label: lowercase, keep only
// [a-z0-9-], strip leading/trailing hyphens. Falls back to "bambuhelper" if
// nothing usable remains. Used by every path that writes netSettings.hostname.
void sanitizeHostname(const char* in, char* out, size_t outSize);
void savePrinterConfig(uint8_t index);
void clearPrinterConfig(uint8_t index);  // reset slot to factory defaults + persist
void saveRotationSettings();
void saveButtonSettings();
void saveBuzzerSettings();
void saveLedSettings();
void saveBatteryIndicatorSetting();
void resetSettings();

// Cloud token persistence (shared across printer slots)
extern char cloudEmail[64];
void saveCloudToken(const char* token);
bool loadCloudToken(char* buf, size_t bufLen);
void clearCloudToken();
void saveCloudEmail(const char* email);

// RGB565 <-> HTML hex conversion
uint16_t htmlToRgb565(const char* hex);
void rgb565ToHtml(uint16_t color, char* buf);  // buf must be >= 8 chars

// Bambu RRGGBBAA hex to RGB565 (e.g. "9D432CFF" -> RGB565)
uint16_t bambuColorToRgb565(const char* rrggbbaa);

// Load default display settings
void defaultDisplaySettings(DisplaySettings& ds);

#endif // SETTINGS_H
