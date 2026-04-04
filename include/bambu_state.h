#ifndef BAMBU_STATE_H
#define BAMBU_STATE_H

#include <Arduino.h>
#include "config.h"

enum ConnMode : uint8_t { CONN_LOCAL = 0, CONN_CLOUD = 1, CONN_CLOUD_ALL = 2 };
enum CloudRegion : uint8_t { REGION_US = 0, REGION_EU = 1, REGION_CN = 2 };

inline bool isCloudMode(ConnMode m) { return m == CONN_CLOUD || m == CONN_CLOUD_ALL; }

// ── AMS (Automatic Material System) ──────────────────────────────────────────
#define AMS_MAX_UNITS      4
#define AMS_TRAYS_PER_UNIT 4
#define AMS_MAX_TRAYS      (AMS_MAX_UNITS * AMS_TRAYS_PER_UNIT)

struct AmsTray {
  bool     present;        // tray physically present
  uint16_t colorRgb565;    // pre-converted for TFT
  char     type[16];       // "PLA Matte" etc.
};

struct AmsState {
  bool     present;               // any AMS data received
  uint8_t  unitCount;             // detected AMS units (0-4)
  uint8_t  activeTray;            // tray_now (0-15), 255 = none
  AmsTray  trays[AMS_MAX_TRAYS];  // indexed by unit*4 + trayId
  bool     vtPresent;             // external spool configured
  uint16_t vtColorRgb565;
  char     vtType[16];
};

struct BambuState {
  bool connected;
  bool printing;
  char gcodeState[16];        // RUNNING, PAUSE, FINISH, IDLE, FAILED, PREPARE
  uint8_t progress;           // 0-100%
  uint16_t remainingMinutes;
  float nozzleTemp;
  float nozzleTarget;
  float bedTemp;
  float bedTarget;
  float chamberTemp;
  char subtaskName[48];
  uint16_t layerNum;
  uint16_t totalLayers;
  uint8_t coolingFanPct;      // part cooling fan 0-100%
  uint8_t auxFanPct;          // aux fan 0-100%
  uint8_t chamberFanPct;      // chamber fan 0-100%
  uint8_t heatbreakFanPct;    // heatbreak fan 0-100%
  int8_t wifiSignal;          // RSSI in dBm
  uint8_t speedLevel;         // 1=silent, 2=standard, 3=sport, 4=ludicrous
  bool dualNozzle;            // H2D/H2C dual extruder detected
  uint8_t activeNozzle;       // 0=right, 1=left (only when dualNozzle)
  bool doorOpen;              // door/enclosure open (H2 series: parsed from stat field)
  bool doorSensorPresent;     // true if stat field with door bit has been received
  unsigned long lastUpdate;   // millis() of last MQTT message
  bool finishBuzzerPlayed;    // true after FINISH buzzer played (reset on next print)
  bool doorAcknowledged;      // true after door opened on FINISH screen (print removed)
  AmsState ams;               // AMS tray data
};

struct PrinterConfig {
  ConnMode mode;              // CONN_LOCAL, CONN_CLOUD, or CONN_CLOUD_ALL
  char ip[16];                // local mode only
  char serial[20];            // both modes
  char accessCode[12];        // local mode only
  char name[24];              // friendly name
  char cloudUserId[32];       // cloud mode: "u_{uid}" for MQTT username
  CloudRegion region;          // cloud mode: US, EU, or CN server region
  uint8_t gaugeSlots[6];       // configurable gauge layout (GaugeType values, see settings.h)
};

struct PrinterSlot {
  PrinterConfig config;
  BambuState state;
};

extern PrinterSlot printers[MAX_PRINTERS];
extern uint8_t activePrinterIndex;

inline PrinterSlot& activePrinter() {
  return printers[activePrinterIndex];
}

// ── Display rotation (multi-printer) ────────────────────────────────────────
enum RotateMode : uint8_t {
  ROTATE_OFF   = 0,   // show only activePrinterIndex
  ROTATE_AUTO  = 1,   // cycle all connected printers
  ROTATE_SMART = 2    // prioritize printing printer, rotate if both printing
};

struct RotationState {
  RotateMode mode;
  uint32_t intervalMs;
  uint8_t displayIndex;           // which printer slot is currently shown
  unsigned long lastRotateMs;
};

extern RotationState rotState;

inline PrinterSlot& displayedPrinter() {
  uint8_t idx = rotState.displayIndex < MAX_PRINTERS ? rotState.displayIndex : 0;
  return printers[idx];
}

#endif // BAMBU_STATE_H
