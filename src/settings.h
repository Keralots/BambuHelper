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
  GAUGE_CAMERA         = 32,    // P1/A1 LAN chamber-image thumbnail (PSRAM+touch boards)
  GAUGE_TYPE_COUNT  // sentinel - always last
};

// Standard 2x3 gauge grid - the 6 slots are used in every mode.
static const uint8_t GAUGE_SLOT_COUNT = 6;
// Extended grids add a 4th column (landscape) or 3rd row (portrait).
// Each extension stores its slots in its own PrinterConfig array so the gauge
// type for "col 4 row 1" and "row 3 col 1" can be set independently.
static const uint8_t LANDSCAPE_EXTRA_COUNT = 2;  // col 4 top, col 4 bot
static const uint8_t PORTRAIT_EXTRA_COUNT  = 3;  // row 3 left, mid, right
// The Ready and Print Complete screens show two gauges side by side, in their
// own slots so they can differ from the print-screen grid (e.g. chamber temp
// while a part cools). Shared by both screens - they never appear together.
static const uint8_t IDLE_SLOT_COUNT = 2;        // left, right
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
  uint8_t  timeDisplayMode;   // finish-time line: 0=ETA clock, 1=remaining duration,
                              // 2=both on one static line ("17:45 · 2h05m")
  bool     fanMatchPrinter;   // round fan % to 10% steps to match printer LCD (else 1% precision from fan_gear)
  bool     hideStatusReadout; // hide the printing status-bar center layer/power readout (redundant when shown as gauges); frees width for the filament name
  bool     invertColors;   // invert display colors (fixes white-bg on some panels)
  bool     cydPanelClassic; // CYD only: use plain Panel_ILI9341 (no inversion)
                            // instead of Panel_ILI9341_2 — for the other
                            // hardware revision that shows mirrored image.
  bool     cyd32eVariant;   // CYD only: 2.8" ESP32-32E clone (lcdwiki) with a
                            // shuffled pinout - speaker amp enable on GPIO4
                            // (classic: red LED) and red LED on GPIO22. Enables
                            // the amp while the buzzer is on and parks the
                            // moved RGB LED off at boot.
  uint8_t  roundSkin;       // round displays only: printing dashboard skin.
                            // 0 = Rim (rim progress ring + mini gauges),
                            // 1 = Speedo (one large 240-degree arc),
                            // 2 = Rings (concentric progress/nozzle/bed).
  bool     landscape8Slots; // 240x320 landscape: replace AMS sidebar with a
                            // symmetric 2x4 gauge grid (8 slots, no sidebar).
  bool     portrait9Slots;  // 240x320 / 320x480 portrait: replace AMS strip
                            // with a 3x3 gauge grid (9 slots, smaller R).
  uint16_t clockTimeColor; // clock digits color (RGB565)
  uint16_t clockDateColor; // clock date/AM-PM color (RGB565)
  uint8_t  clockTimeSize;  // 0=Auto, 1=Normal(1.0x), 2=Medium(1.5x), 3=Large(2.0x). Auto = Large on >=480, Medium on >=320, else Normal.
  uint8_t  clockDateSize;  // 0=Auto (match effective time size), 1=Normal(1.0x), 2=Medium(1.5x), 3=Large(2.0x). Clamped down if the date wouldn't fit.
  bool     hideClockDate;  // minimalist mode: time only, no date line
  bool     showClockInfo;  // footer on the idle/clock screen: each configured printer's name + LAN IP
  bool     amsTrayTypes;   // show per-tray filament-type label under AMS bars (portrait strip); off = taller bars, no text
  bool     buttonPowerControl; // #136: double/triple-click device button opens plug on/off confirm
  bool     showBatteryIndicator; // Waveshare boards: show battery icon in status bar
  // Edge glow: animated border light announcing print complete / print failed
  uint8_t  glowMode;       // 0 = Off, 1 = Single color, 2 = Rainbow
  uint16_t glowColor;      // Single-color mode band color (RGB565)
  uint8_t  glowStyle;      // 0 = Sweep (traveling head + tail), 1 = Pulse (breathing),
                           // 2 = Storm (shattered flickering shards)
  uint8_t  glowDuration;   // 0 = Burst, 1 = Until dismissed, 2 = Burst + reminder
#if HAS_HMS_UI
  // Printer errors (HMS + print_error). Global, not per-printer: the alert
  // hardware is one buzzer, one LED and one panel, so a per-printer mask would
  // have no answer for "A says buzz, B says silent, both erroring". Compiled
  // out where the feature is - the esp32c3 cannot spare flash for settings it
  // can never act on.
  bool     hmsEnabled;     // master opt-out. Off = no badge, no error screen,
                           // no alerts. Parsing stays on, so re-enabling is
                           // instant instead of waiting for the next report.
  bool     hmsSeverityAll; // false = important only (severity 1+2),
                           // true = also common (severity 3)
  uint8_t  hmsAlertMask;   // bit0 glow, bit1 buzzer, bit2 LED, bit3 wake
  uint8_t  hmsAutoPresent; // 0 = badge only, 1 = show briefly, 2 = until dismissed
  bool     hmsLookupOnline;// let the config page fetch the sentence list from
                           // the published mirror. The DEVICE never does this -
                           // the request comes from the browser - so the switch
                           // exists for isolated networks, where it can only
                           // fail, and for anyone who wants no third-party
                           // request at all. Off still leaves the code,
                           // severity, module and wiki link. Ignored on boards
                           // that carry the table (nothing to fetch).
#endif
  // Gauge full-scale ranges (arc maxima). Lowering a scale makes the arc sweep
  // fuller for a printer's normal range. Defaults suit any Bambu printer.
  uint16_t nozzleScaleMax;   // C
  uint16_t bedScaleMax;      // C
  uint16_t chamberScaleMax;  // C (also drives the AMS unit-temp gauge)
  uint16_t powerScaleW;      // W (Tasmota power gauge full-scale)
  uint8_t  gaugeSmoothing;   // arc easing speed: 0=Off(instant) 1=Slow 2=Normal 3=Fast
  uint16_t warnColor;        // temp-gauge over-threshold color (arc + value), RGB565
  uint8_t  warnThresholdPct; // temp gauge turns warnColor at >= this % of scale; 0 = off
  // Accent colors for the screen text that is not part of a gauge (#163). The
  // alarm colors (PAUSED/CANCELED yellow, ERROR red) stay hardcoded on purpose -
  // a theme must never be able to paint a fault in a reassuring color.
  uint16_t etaColor;         // finish-time line, every form: "ETA: 17:45",
                             // "Remaining: 2h 05m" and the combined line. Also
                             // the pre-NTP duration fallback. A missing value
                             // ("ETA: ---") is NOT an accent - it renders in
                             // textDimColor like every other placeholder.
  uint16_t finishColor;      // "Print Complete!" headline and the round skins'
                             // tick mark. NOT the round completion ring: that
                             // one is the fixed gold celebration ring, which
                             // the edge glow also plays against.
  uint16_t statusOkColor;    // healthy status accents: the RUNNING/Ready/FINISH
                             // badge and dot, the connected indicator, the
                             // active-printer dot and a closed door (an OPEN
                             // door keeps its fixed orange - that is a warning)
  uint16_t printerNameColor; // printer name wherever the UI prints it as itself:
                             // the Ready-screen headline and the header line of
                             // the print / finished / drying screens and the
                             // split bands. Its own knob rather than
                             // statusOkColor's - a name is not a status, and the
                             // two sit side by side, so one accent painting both
                             // is not always wanted.
  // Door status indicator (status bar + round rim). Its own pair rather than
  // riding statusOkColor: "closed" is not a print state, and the open colour
  // has to be settable next to it or the pair cannot be kept legible against a
  // themed background. Open still defaults to orange.
  uint16_t doorClosedColor;
  uint16_t doorOpenColor;
  // Neutral text, the two colors every readout that is NOT an accent uses:
  // values, file names, layer counts, dBm, kWh, "ETA: ---". They were the
  // compile-time CLR_TEXT / CLR_TEXT_DIM, which no theme could reach - a dark
  // accent scheme still had white numerals on it. The ~120 call sites did not
  // have to change because CLR_TEXT / CLR_TEXT_DIM are now defined once, at the
  // bottom of this header, as lookups into these two fields. There is no
  // per-TU #undef anywhere in the tree and there must not be one - see the
  // comment on those two #defines for why.
  uint16_t textColor;        // primary readout text (was 0xFFFF white)
  uint16_t textDimColor;     // secondary / placeholder text (was 0xC618 gray).
                             // CLR_TEXT_DARK (inactive dots, tracks) stays
                             // fixed - it is chrome, not text. So does the white
                             // end of a highlight blend: see the CLR_TEXT_DEFAULT
                             // uses in display_gauges.cpp.
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
  GaugeColors power;        // Tasmota power gauge (arc shown only when drawing power)
  GaugeColors layer;        // layer-progress gauge
};

// Per-gauge custom label overrides. Empty string = keep the built-in default
// (including dynamic defaults like "L.Aux"/"Exhaust"). Labels are UTF-8: the
// bundled VLW fonts carry ASCII + Latin-1 Supplement + Latin Extended-A + euro,
// so European diacritics render. The length below is in BYTES, sized so 12
// visible characters fit even when each is a 3-byte sequence.
static const uint8_t GAUGE_LABEL_LEN = 37;  // 12 visible chars x 3 UTF-8 bytes + NUL

struct GaugeLabels {
  char progress[GAUGE_LABEL_LEN];
  char nozzle[GAUGE_LABEL_LEN];     // single nozzle, or base for L/R when those are unset
  char nozzleRight[GAUGE_LABEL_LEN];// dual-nozzle right; empty = "<nozzle> R"
  char nozzleLeft[GAUGE_LABEL_LEN]; // dual-nozzle left;  empty = "<nozzle> L"
  char bed[GAUGE_LABEL_LEN];
  char partFan[GAUGE_LABEL_LEN];
  char auxFan[GAUGE_LABEL_LEN];
  char auxFanRight[GAUGE_LABEL_LEN];
  char chamberFan[GAUGE_LABEL_LEN];
  char exhaustFan[GAUGE_LABEL_LEN];
  char chamberTemp[GAUGE_LABEL_LEN];
  char heatbreak[GAUGE_LABEL_LEN];
  char power[GAUGE_LABEL_LEN];
  char layer[GAUGE_LABEL_LEN];
  char clock[GAUGE_LABEL_LEN];
  char amsBase[GAUGE_LABEL_LEN];    // "AMS" word; rendered as "<base> 1".."<base> HT"
  char door[GAUGE_LABEL_LEN];       // status-bar "Door" indicator
};

// Return the override if non-empty, else the default. Single decision point used
// at every gauge draw site.
inline const char* gaugeLabelOr(const char* override_, const char* def) {
  return (override_ && override_[0]) ? override_ : def;
}

// Copy a user-supplied label into a fixed buffer, keeping valid UTF-8 the fonts
// can render and stripping HTML-unsafe bytes (control chars, " < > &), 4-byte
// sequences, and malformed/overlong UTF-8. A multi-byte char is only kept if it
// fully fits, so the result never ends mid-character. Trims leading/trailing
// spaces. The single sanitize point - used by the web form, JSON import, and
// after NVS load.
void sanitizeGaugeLabel(const char* in, char* out, size_t outLen);

// Strip an incomplete trailing UTF-8 sequence left by a byte-wise copy
// (strlcpy/strncpy/snprintf into a fixed buffer). No-op if the string already
// ends on a character boundary. Call after any such copy of text that may now
// hold multi-byte UTF-8 (printer/file names, currency).
void utf8TrimPartial(char* s);

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
  bool finishShowTime;         // append the completion clock time to the finish headline
                               // (costs the large font on 240 px wide layouts)
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

// Status LED settings (optional, PWM dimmable)
enum LedFinishMode : uint8_t {
  LED_FINISH_OFF       = 0,
  LED_FINISH_BREATHING = 1,
  LED_FINISH_HEARTBEAT = 2,
};

// How the LED is wired. SINGLE is the original one-channel behaviour and stays
// the default, so an existing install keeps working with every colour field
// ignored. The two colour drivers reuse the whole effect engine unchanged: the
// patterns still produce a 0..255 intensity envelope, and the driver multiplies
// the state's colour by it.
enum LedDriver : uint8_t {
  LED_DRV_SINGLE = 0,   // one PWM channel on LedSettings::pin
  LED_DRV_RGB    = 1,   // three PWM channels: pin (red) / pinG / pinB
  LED_DRV_PIXEL  = 2,   // one WS2812 on LedSettings::pin, driven over RMT
};

struct LedSettings {
  bool    enabled;
  uint8_t pin;                 // SINGLE/PIXEL: the pin. RGB: the red channel.
  uint8_t brightness;          // 0-255, persisted "working" level

  // Print-finished one-shot effect
  uint8_t  finishMode;         // LedFinishMode
  uint16_t finishSeconds;      // 5..600
  uint8_t  finishBrightness;   // 0..255 peak

  // Continuous state-driven behaviors
  bool autoOnWhilePrinting;    // LED on only while printer is printing
  bool pauseBreathing;         // slow breath during GCODE_PAUSE
  bool errorStrobe;            // fast strobe during GCODE_FAILED
  uint16_t errorStrobeSeconds; // error strobe auto-off after N s (0 = never)

  // Colour output. Ignored entirely when driver == LED_DRV_SINGLE.
  uint8_t driver;              // LedDriver
  uint8_t pinG;                // RGB only: green channel
  uint8_t pinB;                // RGB only: blue channel
  bool    commonAnode;         // RGB only: shared leg on 3V3, so LOW = lit
  uint32_t colorIdle;          // 0xRRGGBB, one per printer state
  uint32_t colorPrinting;
  uint32_t colorPaused;
  uint32_t colorFinished;
  uint32_t colorError;
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
  uint8_t plugType;          // 0=Tasmota, 1=Shelly Gen2/Gen3, 2=TP-Link Kasa legacy (KP115), 3=Shelly Power Strip Gen4
  uint8_t plugOutlet;        // which outlet/relay (0-3) this slot tracks on a multi-channel device;
                             // currently wired for plugType 3 (Shelly Power Strip Gen4) only
  char    ip[16];
  uint8_t displayMode;       // 0=alternate layers/power every 4s, 1=always show power, 2=always layer count
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

// The live neutral text colors (#163). Defined here rather than in config.h so
// there is exactly one definition and no per-file #undef dance: a drawing file
// already includes settings.h, and one that does not fails to compile instead
// of silently painting the factory literal. config.h keeps only
// CLR_TEXT_DEFAULT / CLR_TEXT_DIM_DEFAULT, which settings.cpp uses to seed
// these fields.
#define CLR_TEXT      (dispSettings.textColor)
#define CLR_TEXT_DIM  (dispSettings.textDimColor)

extern GaugeLabels gaugeLabels;
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
// C3 antenna workaround (issue #146): true = this board's radio only works at
// reduced TX power, so every WiFi connect must be capped at 8.5 dBm. Set by
// wifi_manager when a reduced-power STA attempt succeeds after full-power
// attempts failed. Only meaningful on BOARD_IS_C3 builds.
extern bool wifiTxCapped;

void loadSettings();
void saveSettings();

// Normalize a string into a valid mDNS/DNS label: lowercase, keep only
// [a-z0-9-], strip leading/trailing hyphens. Falls back to "bambuhelper" if
// nothing usable remains. Used by every path that writes netSettings.hostname.
void sanitizeHostname(const char* in, char* out, size_t outSize);
void savePrinterConfig(uint8_t index);
void clearPrinterConfig(uint8_t index);  // reset slot to factory defaults + erase its NVS keys
void saveRotationSettings();
void saveButtonSettings();
void saveBuzzerSettings();
void saveLedSettings();
void saveBatteryIndicatorSetting();
void saveWifiTxCapped();
void resetSettings();

// Cloud token persistence (shared across printer slots). The token is stored
// as an NVS blob so it can span NVS pages; a fragmented partition that refuses
// a ~1 KB string write can still take it. Returns false when the write did not
// stick (storage genuinely full) - callers must surface that, never discard it.
bool saveCloudToken(const char* token);
bool loadCloudToken(char* buf, size_t bufLen);
void clearCloudToken();          // also drops the stored email and password

// NVS entry usage of the whole partition, for diagnostics and error messages.
void getNvsUsage(uint16_t& used, uint16_t& freeEntries, uint16_t& total);

// Persist "which BambuHelper version sits in the currently running app slot"
// so the OTA rollback UI can name the other slot's firmware. Call once per
// boot, after loadSettings().
void recordBootSlotVersion();

// Read that note ("<version>|<sha8>") for an app slot by partition label.
// Returns false when no firmware ever recorded one there.
bool loadSlotVersionNote(const char* label, char* buf, size_t bufLen);

void saveCloudEmail(const char* email);
bool loadCloudEmail(char* buf, size_t bufLen);
void saveCloudPassword(const char* password);
bool loadCloudPassword(char* buf, size_t bufLen);
void clearCloudPassword();

// RGB565 <-> HTML hex conversion
uint16_t htmlToRgb565(const char* hex);
void rgb565ToHtml(uint16_t color, char* buf);  // buf must be >= 8 chars

// Bambu RRGGBBAA hex to RGB565 (e.g. "9D432CFF" -> RGB565)
uint16_t bambuColorToRgb565(const char* rrggbbaa);

// Load default display settings
void defaultDisplaySettings(DisplaySettings& ds);

#endif // SETTINGS_H
