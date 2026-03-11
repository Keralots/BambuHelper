#ifndef BAMBU_STATE_H
#define BAMBU_STATE_H

#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
//  Printer model enum
// ---------------------------------------------------------------------------
enum PrinterModel : uint8_t {
  MODEL_P1S = 0,
  MODEL_P1P,
  MODEL_X1C,
  MODEL_A1,
  MODEL_A1_MINI
};

inline bool hasEnclosure(PrinterModel m) {
  return (m == MODEL_P1S || m == MODEL_X1C);
}

inline const char* defaultModelName(PrinterModel m) {
  switch (m) {
    case MODEL_P1S:     return "Bambu P1S";
    case MODEL_P1P:     return "Bambu P1P";
    case MODEL_X1C:     return "Bambu X1C";
    case MODEL_A1:      return "Bambu A1";
    case MODEL_A1_MINI: return "Bambu A1 Mini";
    default:            return "Bambu Printer";
  }
}

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
  unsigned long lastUpdate;   // millis() of last MQTT message
};

struct PrinterConfig {
  bool enabled;
  PrinterModel model;         // printer model (determines fan gauge)
  char ip[16];
  char serial[20];
  char accessCode[12];
  char name[24];              // friendly name
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

#endif // BAMBU_STATE_H
