#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "bambu_state.h"

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
  bool     animatedBar;    // shimmer effect on progress bar
  bool     pongClock;      // Pong/Breakout animated clock
  bool     smallLabels;    // use smaller gauge labels (Font 1 instead of Font 2)
  bool     invertColors;   // invert display colors (fixes white-bg on some panels)
  uint8_t  cydExtraMode;   // CYD extra area: 0=AMS, 1=Extra Gauges
  GaugeColors progress;
  GaugeColors nozzle;
  GaugeColors bed;
  GaugeColors partFan;
  GaugeColors auxFan;
  GaugeColors chamberFan;
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
};

// Display power settings
struct DisplayPowerSettings {
  uint16_t finishDisplayMins;  // minutes to show finish screen (0 = keep on)
  bool keepDisplayOn;          // override: never turn off display
  bool showClockAfterFinish;   // show clock instead of turning display off
  bool doorAckEnabled;         // wait for door open after print finish before timeout
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
};

// Tasmota smart plug power monitoring
struct TasmotaSettings {
  bool    enabled;
  char    ip[16];
  uint8_t displayMode;   // 0=alternate layers/power every 4s, 1=always show power
  uint8_t pollInterval;  // poll interval in seconds (10-30)
  uint8_t assignedSlot;  // printer slot this plug belongs to (0, 1, ... or 255=any)
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
extern TasmotaSettings tasmotaSettings;

void loadSettings();
void saveSettings();
void savePrinterConfig(uint8_t index);
void saveRotationSettings();
void saveButtonSettings();
void saveBuzzerSettings();
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
