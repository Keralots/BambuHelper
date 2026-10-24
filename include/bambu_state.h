#include "localization.h"
#ifndef BAMBU_STATE_H
#define BAMBU_STATE_H

#include <Arduino.h>
#include "config.h"

enum ConnMode : uint8_t { CONN_LOCAL = 0, CONN_CLOUD = 1, CONN_CLOUD_ALL = 2 };
enum CloudRegion : uint8_t { REGION_US = 0, REGION_EU = 1, REGION_CN = 2 };
enum PrinterGcodeState : uint8_t {
  GCODE_UNKNOWN = 0,
  GCODE_IDLE,
  GCODE_RUNNING,
  GCODE_PAUSE,
  GCODE_PREPARE,
  GCODE_FINISH,
  GCODE_FAILED,
  GCODE_OTHER
};

inline bool isCloudMode(ConnMode m) { return m == CONN_CLOUD || m == CONN_CLOUD_ALL; }

// ── AMS (Automatic Material System) ──────────────────────────────────────────
// Unit-level data (id / temp / humidity / drying) is kept for more units than
// tray data. An H2 with 3 AMS 2 Pro + 2 AMS HT reports FIVE units (ids 0, 1, 2,
// 128, 129); the fifth used to be dropped whole, so a drying AMS HT #2 was
// invisible - no drying screen, no (x/y) counter, and the display slept through
// it. Tray slots stay at four: the AMS bars, the AMS gauges (GAUGE_AMS_*_1..4)
// and the saved gauge-slot values are all built around four groups of four.
// Units past AMS_TRAY_UNITS still report temp/humidity/drying; their trays are
// not stored, and the feeding one is captured out-of-band in ovTray below.
#define AMS_MAX_UNITS      6
#define AMS_TRAY_UNITS     4
#define AMS_TRAYS_PER_UNIT 4
#define AMS_MAX_TRAYS      (AMS_TRAY_UNITS * AMS_TRAYS_PER_UNIT)

// activeTray sentinel: the feeding tray belongs to an AMS unit that didn't
// fit in units[] (5+ units, e.g. a 2nd AMS HT on H2 series). Tray data is
// captured out-of-band in AmsState.ovTray so the filament swatch stays
// correct even though the unit itself isn't displayed.
#define AMS_TRAY_OVERFLOW  253

struct AmsTray {
  bool     present;        // tray physically present
  uint16_t colorRgb565;    // pre-converted for TFT
  char     type[16];       // "PLA Matte" etc.
  int8_t   remain;         // 0-100%, -1 = unknown/third-party
};

struct AmsUnit {
  bool     present;               // unit detected in MQTT data
  uint8_t  id;                    // raw id from MQTT (0-3 for AMS2, 128 for AMS HT)
  uint8_t  humidity;              // 0-5 scale; direction can vary by AMS model/firmware
  uint8_t  humidityRaw;           // raw RH percent when reported; preferred for display color
  float    temp;                  // current temperature inside AMS
  uint16_t dryRemainMin;          // minutes remaining, 0 = not drying
  uint16_t dryTotalMin;           // captured at drying start (for progress calc)
  uint8_t  trayCount;             // actual trays parsed (4 for AMS2, 1 for AMS HT)
};

struct AmsState {
  bool     present;               // any AMS data received
  uint8_t  unitCount;             // detected AMS units (0..AMS_MAX_UNITS)
  uint8_t  trayUnitCount;         // units that own tray slots (0..AMS_TRAY_UNITS)
  uint8_t  activeTray;            // 0-15, 253 = overflow unit (see ovTray), 254 = external spool, 255 = none
  AmsTray  trays[AMS_MAX_TRAYS];  // indexed by unit*4 + trayId (first AMS_TRAY_UNITS units only)
  AmsUnit  units[AMS_MAX_UNITS];  // unit-level data (indexed sequentially)
  AmsTray  ovTray;                // feeding tray when activeTray == AMS_TRAY_OVERFLOW
  uint8_t  ovUnitId;              // raw AMS unit id ovTray was captured from (255 = none)
  uint8_t  ovTrayId;              // tray id within that unit
  bool     anyDrying;             // true if any unit has dryRemainMin > 0
  bool     vtPresent;             // external spool configured
  uint16_t vtColorRgb565;
  char     vtType[16];
};

// ── Printer errors: print.hms + print.print_error ───────────────────────────
#if HAS_HMS_UI

// Display cap, NOT a protocol bound. Every entry the printer reports is scanned
// for worst severity and baseline membership; only this many are kept for
// rendering, sorted by severity then key so a reordered array cannot manufacture
// a change. hmsOverflow records that the printer sent more than we kept.
#define HMS_MAX_ENTRIES    8

// Codes already standing when we connect are the printer's baseline: listed,
// but never alerting. Capped per slot so a pathological printer cannot eat RAM.
#define HMS_BASELINE_MAX  16

// Suppressed codes kept for /debug. Hiding a code the printer really reported
// is now a designed behaviour, so it has to be answerable from a diagnostic
// dump alone - "my printer shows a code and BambuHelper shows none" must not
// need a USB cable and a serial monitor. Four names the culprits; the serial
// log still carries every one of them.
#define HMS_SUPPRESSED_MAX  4

struct HmsEntry {
  uint32_t attr;   // module in the top byte
  uint32_t code;   // severity in the high 16 bits
};

// Severity is the high half of `code`: 1 fatal, 2 serious, 3 common. The live
// feed also carries one severity-0 code, and Bambu may add more, so anything
// outside 1-3 is "unknown": listed, never alerting, and ranked below every
// real severity. Clamped to a byte - real values are single digits.
inline uint8_t hmsSeverityOf(uint32_t code) {
  uint32_t sev = code >> 16;
  return sev > 255 ? 255 : (uint8_t)sev;
}

// Sort/compare key: 0 = fatal (worst) .. 3 = unknown (loses to everything).
inline uint8_t hmsSeverityRank(uint32_t code) {
  uint8_t sev = hmsSeverityOf(code);
  return (sev >= 1 && sev <= 3) ? (uint8_t)(sev - 1) : (uint8_t)3;
}

// Full 64-bit lookup key, matching Bambu's 16-hex-char ecode.
inline uint64_t hmsKeyOf(uint32_t attr, uint32_t code) {
  return ((uint64_t)attr << 32) | (uint64_t)code;
}

#endif  // HAS_HMS_UI

// Chamber-light automation flags (PrinterConfig.lightFlags bitmask)
#define LIGHT_OFF_ON_FINISH 0x01  // turn light off after a successful print
#define LIGHT_OFF_ON_FAILED 0x02  // turn light off after a failed/cancelled print
#define LIGHT_ON_AT_START   0x04  // turn light on when a print starts

#define PRINT_MAP_MAX 8

struct BambuState {
  bool connected;
  bool printing;
  char gcodeState[16];        // RUNNING, PAUSE, FINISH, IDLE, FAILED, PREPARE
  PrinterGcodeState gcodeStateId;
  uint8_t progress;           // 0-100%
  uint16_t remainingMinutes;
  int8_t stgCur;              // print.stg_cur: -1/255 idle, 0 printing, >0 substage (changing filament, leveling...)
  float nozzleTemp;
  float nozzleTarget;
  float nozzleTempN[2];       // per-nozzle temps when dualNozzle (index = extruder id: 0=right, 1=left)
  float nozzleTargetN[2];
  float bedTemp;
  float bedTarget;
  float chamberTemp;
  char subtaskName[128];      // 48 cut before the screen did: a real H2C name measured 47 chars (#187)
  // print.mapping: one entry per slicer filament the job uses, (amsUnitId<<8)|trayId
  // (unused 65535 entries dropped, slicer order kept). Cleared when the job name changes.
  char taskId[24];            // print.task_id of the current/last job ("" = none, "0" = local)
  uint16_t plateIdx;          // print.plate_idx (1-based plate of that job, 0 = unknown)
  uint16_t printMap[PRINT_MAP_MAX];
  uint8_t printMapCount;      // entries stored in printMap
  uint8_t printMapTotal;      // used entries the printer reported (> count = truncated)
  bool caliPrintType;         // print_type == "system" (device-initiated calibration job)
  bool caliSubtask;           // subtask_name ends with "_calib_mode" (Studio calibration wizard job)
  bool caliGcodeFile;         // gcode_file is a built-in calibration gcode (auto_cali_for_user / extrusion_cali)
  uint16_t layerNum;
  uint16_t totalLayers;
  uint8_t coolingFanPct;      // part cooling fan 0-100%
  uint8_t auxFanPct;          // aux fan 0-100% (X2D: left aux)
  uint8_t auxFanRightPct;     // X2D right aux fan 0-100% (from device.airduct.parts[].func==6)
  uint8_t chamberFanPct;      // chamber fan 0-100%
  uint8_t exhaustFanPct;      // X2D exhaust fan 0-100% (from device.airduct.parts[].func==2)
  uint8_t heatbreakFanPct;    // heatbreak fan 0-100%
  bool fanGearSeen;           // true once printer has reported fan_gear (gates legacy *_fan_speed fallback)
  uint32_t airductFuncs;      // bitmask of func codes seen in device.airduct.parts[]: bit N = func N reported.
                              // Used to gate per-fan gauges in the web UI. H2C reports 0/1/2/4;
                              // X2D reports 0/2/5/6 — checking specific bits is more precise than
                              // a single "has airduct" boolean.
  int8_t wifiSignal;          // RSSI in dBm
  char localIp[16];           // printer LAN IP from pushall print.net.info[].ip (LE uint32), "" if unknown
  uint8_t speedLevel;         // 1=silent, 2=standard, 3=sport, 4=ludicrous
  bool dualNozzle;            // H2D/H2C dual extruder detected
  uint8_t activeNozzle;       // 0=right, 1=left (only when dualNozzle)
  bool doorOpen;              // door/enclosure open (H2/P2S: stat bit 0x00800000, X1: home_flag bit 23)
  bool doorSensorPresent;     // true once a door-capable printer has reported its door field
  unsigned long lastUpdate;       // millis() of last MQTT message (any)
  unsigned long lastPrintDataMs;  // millis() of last core print data (temps, fans, progress, state)
  bool finishBuzzerPlayed;    // true after FINISH buzzer played (reset on next print)
  uint32_t finishEpoch;       // wall-clock time the print finished (epoch secs), 0 = unknown
  bool finishTimeLatched;     // true once this print's finish has been stamped. Starts SET
                              // after a state wipe: the first FINISH report from a printer
                              // that was already done reads as an edge, and stamping that
                              // would record boot time. Armed by an observed print start.
  bool doorAcknowledged;      // true after door opened on FINISH screen (print removed)
  bool bedCooldownAlertArmed; // armed on FINISH transition, fired when bedTemp <= threshold
  int8_t lightState;          // chamber_light from lights_report: -1 unknown, 0 off, 1 on
  bool hasSecondLight;        // true if printer reports chamber_light2 (H2C/H2D dual bar)
  unsigned long lightOffDueMs; // millis() deadline for a scheduled light-off, 0 = none pending
  unsigned long ctrlCmdSentMs; // millis() of our last control publish (ledctrl), 0 = never.
                               // Only control commands are authorization-checked (#185)
#if HAS_HMS_UI
  uint32_t printError;        // print.print_error, 0 = none
  bool printErrorSeen;        // a value has been observed on this connection.
                              // First sight initializes without counting as an
                              // edge - booting into an error must not alert.
  HmsEntry hms[HMS_MAX_ENTRIES];  // worst-first, deterministic subset
  uint8_t  hmsCount;          // entries kept in hms[]
  uint8_t  hmsTotal;          // described entries the printer reported. Codes
                              // Bambu ships no wording for are not counted -
                              // they are dropped before they reach any of this.
  uint8_t  hmsSuppressed;     // entries dropped by that rule this report. Not a
                              // display field: it exists so /debug and the
                              // serial log can answer "the printer says a code
                              // and BambuHelper shows nothing". See
                              // hmsIsDescribed() and issue #164.
  HmsEntry hmsSuppressedCodes[HMS_SUPPRESSED_MAX];  // the first few of them, by
                              // name rather than by count, for the same reason
  bool     hmsOverflow;       // hmsTotal > HMS_MAX_ENTRIES
  uint8_t  hmsWorstSeverity;  // severity of hms[0], 0 = none active. The array
                              // is sorted worst-first, so the worst entry is
                              // always hms[0] - no separate index is needed,
                              // and the truncated tail can never hold anything
                              // more severe than what was kept.
  HmsEntry hmsBaseline[HMS_BASELINE_MAX];  // standing codes, never alert
  uint8_t  hmsBaselineCount;
  bool     hmsOwnCmdRejected; // standing 0500-0500-0001-0007 is ours, not another
                              // program's - latched while the code stands (#185)
  bool     hmsBaselineSaturated;  // more than HMS_BASELINE_MAX standing codes.
                                  // We can no longer tell a dropped baseline
                                  // member from a new code, so everything on
                                  // this slot counts as baseline until the next
                                  // connect. Suppressing a real alert beats
                                  // false-alerting on a permanent condition,
                                  // and a printer standing on 17+ codes is
                                  // already broken. Logged when it happens.
#endif
  AmsState ams;               // AMS tray data
};

#if HAS_HMS_UI
// True when (attr, code) was already standing during the connect baseline
// window - listed on screen and in the portal, but never raises the badge or
// fires an alert.
inline bool hmsIsBaseline(const BambuState& s, uint32_t attr, uint32_t code) {
  if (s.hmsBaselineSaturated) return true;
  for (uint8_t i = 0; i < s.hmsBaselineCount; i++)
    if (s.hmsBaseline[i].attr == attr && s.hmsBaseline[i].code == code)
      return true;
  return false;
}
#endif

inline PrinterGcodeState parsePrinterGcodeState(const char* state) {
  if (!state || state[0] == '\0') return GCODE_UNKNOWN;
  if (strcmp(state, "UNKNOWN") == 0) return GCODE_UNKNOWN;
  if (strcmp(state, "IDLE") == 0) return GCODE_IDLE;
  if (strcmp(state, "RUNNING") == 0) return GCODE_RUNNING;
  if (strcmp(state, "PAUSE") == 0) return GCODE_PAUSE;
  if (strcmp(state, "PREPARE") == 0) return GCODE_PREPARE;
  if (strcmp(state, "FINISH") == 0) return GCODE_FINISH;
  if (strcmp(state, "FAILED") == 0) return GCODE_FAILED;
  return GCODE_OTHER;
}

inline bool isPrintingGcodeState(PrinterGcodeState state) {
  return state == GCODE_RUNNING ||
         state == GCODE_PAUSE ||
         state == GCODE_PREPARE;
}

// Human label for print.stg_cur while the printer is RUNNING but not yet laying
// plastic (changing filament, leveling, heating...). gcode_state stays RUNNING
// through all of these, so the badge alone reads a bland "RUNNING". Returns NULL
// for 0 (printing), <0 / 255 (idle) and any stage we don't surface, so callers
// fall back to the normal ETA line. Covers every non-paused stage: the paused_*
// stages flip gcode_state to PAUSE (shown by that badge), so their labels would
// be unreachable here. IDs from BambuStudio / pybambu CURRENT_STAGE_IDS. Labels
// kept <=18 chars so they fit the 240px ETA line at FONT_BODY without auto-fit.
inline const char* stgCurLabel(int8_t stg) {
  switch (stg) {
    case 1:  return BH_TEXT("Bed Leveling", "热床调平");
    case 2:  return BH_TEXT("Preheating Bed", "热床预热");
    case 3:  return BH_TEXT("Sweeping XY", "XY 扫频");
    case 4:  return BH_TEXT("Changing Filament", "更换耗材");
    case 7:  return BH_TEXT("Heating Nozzle", "喷嘴加热");
    case 8:  return BH_TEXT("Calibrating Flow", "流量校准");
    case 9:  return BH_TEXT("Scanning Bed", "扫描热床");
    case 10: return BH_TEXT("Inspecting Layer 1", "首层检测");
    case 11: return BH_TEXT("Detecting Plate", "检测打印板");
    case 12:
    case 18: return BH_TEXT("Calibrating Lidar", "激光雷达校准");
    case 13: return BH_TEXT("Homing", "归零");
    case 14: return BH_TEXT("Cleaning Nozzle", "清理喷嘴");
    case 15: return BH_TEXT("Checking Temp", "检查温度");
    case 19: return BH_TEXT("Calibrating Flow", "流量校准");
    case 22: return BH_TEXT("Unloading Filament", "退料");
    case 24: return BH_TEXT("Loading Filament", "进料");
    case 25:
    case 31: return BH_TEXT("Motor Noise Cali", "电机降噪校准");
    case 29: return BH_TEXT("Cooling Chamber", "腔体冷却");
    case 36: return BH_TEXT("Accuracy Check", "精度检查");
    case 37: return BH_TEXT("Accuracy Cali", "精度校准");
    case 38: return BH_TEXT("Accuracy Verify", "精度验证");
    case 39: return BH_TEXT("Nozzle Offset Cali", "喷嘴偏移校准");
    case 40: return BH_TEXT("Hot Bed Leveling", "热床高温调平");
    case 41: return BH_TEXT("Quick Release Chk", "快拆检查");
    case 42: return BH_TEXT("Door/Cover Check", "舱门顶盖检查");
    case 43: return BH_TEXT("Laser Cali", "激光校准");
    case 44: return BH_TEXT("Platform Check", "平台检查");
    case 45: return BH_TEXT("Cam Position Chk", "相机位置检查");
    case 46: return BH_TEXT("Camera Cali", "相机校准");
    case 47: return BH_TEXT("Bed Leveling 1", "热床调平 1");
    case 48: return BH_TEXT("Bed Leveling 2", "热床调平 2");
    case 49: return BH_TEXT("Heating Chamber", "腔体加热");
    case 50: return BH_TEXT("Cooling Bed", "热床冷却");
    case 51: return BH_TEXT("Cali Lines", "校准线");
    case 52: return BH_TEXT("Checking Material", "耗材检查");
    case 53: return BH_TEXT("Live Cam Cali", "实时相机校准");
    case 54: return BH_TEXT("Waiting for Bed", "等待热床");
    case 55: return BH_TEXT("Material Pos Chk", "耗材位置检查");
    case 56: return BH_TEXT("Cutter Offset Cali", "切刀偏移校准");
    case 57: return BH_TEXT("Measuring Surface", "测量表面");
    case 58: return BH_TEXT("Thermal Precond", "温度预处理");
    case 59: return BH_TEXT("Homing Blade", "刀片归零");
    case 60: return BH_TEXT("Camera Offset Cali", "相机偏移校准");
    case 61: return BH_TEXT("Blade Pos Cali", "刀片位置校准");
    case 62: return BH_TEXT("Hotend Test", "热端测试");
    case 63: return BH_TEXT("Chamber Temp Wait", "等待腔温");
    case 64: return BH_TEXT("Preparing Hotend", "准备热端");
    case 65: return BH_TEXT("Nozzle Clump Cali", "喷嘴积料校准");
    case 66: return BH_TEXT("Purifying Air", "空气净化");
    case 67: return BH_TEXT("Rotary Measure", "旋转测量");
    case 68: return BH_TEXT("Moving to Purge", "移至冲刷位置");
    case 69: return BH_TEXT("Cooling Nozzle", "喷嘴冷却");
    case 70: return BH_TEXT("Centering Head", "打印头居中");
    case 71: return BH_TEXT("Arc Fitting", "圆弧拟合");
    case 72: return BH_TEXT("Hotend Detect", "热端检测");
    case 73: return BH_TEXT("Plate Align Chk", "打印板对齐检查");
    case 74: return BH_TEXT("Foreign Object Chk", "异物检查");
    case 75: return BH_TEXT("Bed Underside Chk", "热床底部检查");
    case 76: return BH_TEXT("Pre-Extruding", "预挤出");
    case 77: return BH_TEXT("Preparing AMS", "准备 AMS");
    default: return nullptr;
  }
}

// Substage label to show in place of the ETA line: only while RUNNING (prep and
// mid-print filament swaps), NULL otherwise so the ETA/PAUSE/ERROR ladder wins.
inline const char* runningStageLabel(const BambuState& s) {
  if (s.gcodeStateId != GCODE_RUNNING) return nullptr;
  return stgCurLabel(s.stgCur);
}


// True when the current/last job is a calibration print - either a Bambu
// Studio calibration wizard job or a device-initiated system calibration.
// Studio still needs the printer after these finish (to read back / save
// results), so plug auto-off must not power it down (issue #149).
inline bool isCalibrationPrint(const BambuState& s) {
  return s.caliPrintType || s.caliSubtask || s.caliGcodeFile;
}

// Friendly label for calibration jobs, mapped from the Studio wizard job
// names (BambuStudio get_calib_mode_name). Returns NULL for normal prints.
// "retration" is Bambu's own typo in the wizard job name, not ours.
inline const char* calibrationPrintLabel(const BambuState& s) {
  if (s.caliSubtask) {
    const char* n = s.subtaskName;
    if (strncmp(n, "pa_", 3) == 0 || strncmp(n, "auto_pa_", 8) == 0)
      return "Flow Dynamics Calibration";
    if (strncmp(n, "flow_rate_coarse", 16) == 0) return "Flow Rate Calibration (1/2)";
    if (strncmp(n, "flow_rate_fine", 14) == 0)   return "Flow Rate Calibration (2/2)";
    if (strncmp(n, "temp_tower", 10) == 0)       return "Temp Tower Calibration";
    if (strncmp(n, "vol_speed", 9) == 0)         return "Volumetric Speed Calibration";
    if (strncmp(n, "vfa_", 4) == 0)              return "VFA Calibration";
    if (strncmp(n, "retration_", 10) == 0)       return "Retraction Calibration";
    return "Calibration";
  }
  if (s.caliPrintType || s.caliGcodeFile) return "Calibration";
  return NULL;
}

// Job name for display: friendly calibration label when the job is a
// calibration print, raw subtask name otherwise.
inline const char* jobDisplayName(const BambuState& s) {
  const char* cali = calibrationPrintLabel(s);
  return cali ? cali : s.subtaskName;
}

inline void setPrinterGcodeStateRaw(BambuState& state, const char* rawState) {
  strlcpy(state.gcodeState, rawState ? rawState : "", sizeof(state.gcodeState));
  state.gcodeStateId = parsePrinterGcodeState(state.gcodeState);
}

inline void setPrinterGcodeStateCanonical(BambuState& state, PrinterGcodeState gcodeState) {
  state.gcodeStateId = gcodeState;
  switch (gcodeState) {
    case GCODE_IDLE:
      strlcpy(state.gcodeState, "IDLE", sizeof(state.gcodeState));
      break;
    case GCODE_RUNNING:
      strlcpy(state.gcodeState, "RUNNING", sizeof(state.gcodeState));
      break;
    case GCODE_PAUSE:
      strlcpy(state.gcodeState, "PAUSE", sizeof(state.gcodeState));
      break;
    case GCODE_PREPARE:
      strlcpy(state.gcodeState, "PREPARE", sizeof(state.gcodeState));
      break;
    case GCODE_FINISH:
      strlcpy(state.gcodeState, "FINISH", sizeof(state.gcodeState));
      break;
    case GCODE_FAILED:
      strlcpy(state.gcodeState, "FAILED", sizeof(state.gcodeState));
      break;
    case GCODE_OTHER:
      if (state.gcodeState[0] == '\0') {
        strlcpy(state.gcodeState, "UNKNOWN", sizeof(state.gcodeState));
        state.gcodeStateId = GCODE_UNKNOWN;
      }
      break;
    case GCODE_UNKNOWN:
    default:
      strlcpy(state.gcodeState, "UNKNOWN", sizeof(state.gcodeState));
      state.gcodeStateId = GCODE_UNKNOWN;
      break;
  }
}

struct PrinterConfig {
  ConnMode mode;              // CONN_LOCAL, CONN_CLOUD, or CONN_CLOUD_ALL
  char ip[16];                // local mode only
  char serial[20];            // both modes
  char accessCode[12];        // local mode only
  char name[24];              // friendly name
  char cloudUserId[32];       // cloud mode: "u_{uid}" for MQTT username
  CloudRegion region;          // cloud mode: US, EU, or CN server region
  uint8_t gaugeSlots[6];       // Standard 2x3 grid - used in every mode.
  uint8_t landscapeExtras[2];  // Col 4 (top, bot) - landscape 8-slot mode only.
  uint8_t portraitExtras[3];   // Row 3 (left, mid, right) - portrait 9-slot mode only.
  bool    amsView;             // 240x240: replace gauge row 2 with AMS strip (per-printer)
  uint8_t idleSlots[2];        // Ready + Print Complete screens (left, right).
  uint8_t lightFlags;          // chamber-light automation bitmask (LIGHT_* flags), 0 = all off
  uint8_t lightOffDelayMin;    // minutes to wait before turning light off (0-60), default 5
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
  ROTATE_SMART = 2    // prioritize active printers (printing or AMS drying)
};

struct RotationState {
  RotateMode mode;
  uint32_t intervalMs;
  uint8_t displayIndex;           // which printer slot is currently shown
  unsigned long lastRotateMs;
  unsigned long displayHoldUntilMs;  // suppresses Smart snap/rotation so a manually-peeked or freshly-finished slot stays on screen for one interval
  bool splitEnabled;              // checkbox: show two active printers together (composes with mode)
  bool splitForce;                // testing checkbox: always split the first two configured slots, ignoring activity
  uint8_t splitIndexB;            // second printer slot shown in split view (displayIndex is the first)
};

extern RotationState rotState;

inline PrinterSlot& displayedPrinter() {
  uint8_t idx = rotState.displayIndex < MAX_PRINTERS ? rotState.displayIndex : 0;
  return printers[idx];
}

#endif // BAMBU_STATE_H
