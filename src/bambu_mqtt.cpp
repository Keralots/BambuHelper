#include "bambu_mqtt.h"
#include "bambu_state.h"
#include "settings.h"
#include "display_ui.h"
#include "config.h"
#include "bambu_cloud.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

// Built-in CA certificate bundle for cloud TLS verification
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

// ── Per-connection context ──────────────────────────────────────────────────
struct MqttConn {
  uint8_t slotIndex;
  WiFiClientSecure* tls;
  PubSubClient* mqtt;
  MqttDiag diag;
  unsigned long lastReconnectAttempt;
  unsigned long lastPushallRequest;
  uint32_t pushallSeqId;
  unsigned long connectTime;
  bool initialPushallSent;
  bool gotDataSinceConnect;  // true after first message on current connection
  bool active;           // connection slot in use
  uint16_t consecutiveFails;  // for exponential backoff
  unsigned long disconnectSince;  // grace period before showing "connecting" screen
  bool wasConnected;              // track connected->disconnected transitions for logging
  unsigned long stalePushallSentMs;  // when recovery pushall was sent on stale detection
  unsigned long lastRecoveryResolvedMs;  // when last recovery resolved (cooldown timer)
};

static MqttConn conns[MAX_ACTIVE_PRINTERS];

bool mqttDebugLog = false;

static void mqttCallback(char* topic, byte* payload, unsigned int length);

// Detect X1 series printers by serial prefix (per Bambu Lab wiki).
// X1C: "00M", X1E: "03W". These use home_flag bit 23 for the door sensor.
static bool isX1SeriesSerial(const char* serial) {
  if (!serial) return false;
  return strncmp(serial, "00M", 3) == 0 ||
         strncmp(serial, "03W", 3) == 0;
}


// Conditional debug print
#define MQTT_LOG(fmt, ...) do { if (mqttDebugLog) Serial.printf("MQTT: " fmt "\n", ##__VA_ARGS__); } while(0)

// ---------------------------------------------------------------------------
const char* mqttRcToString(int rc) {
  switch (rc) {
    case -4: return "Timeout";
    case -3: return "Lost connection";
    case -2: return "Connect failed";
    case -1: return "Disconnected";
    case  0: return "Connected";
    case  1: return "Bad protocol";
    case  2: return "Bad client ID";
    case  3: return "Unavailable";
    case  4: return "Bad credentials";
    case  5: return "Unauthorized";
    default: return "Unknown";
  }
}

const MqttDiag& getMqttDiag(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;
  conns[slot].diag.freeHeap = ESP.getFreeHeap();
  return conns[slot].diag;
}

const char* pushallReasonToString(uint8_t reason) {
  switch ((PushallReason)reason) {
    case PUSHALL_INITIAL:            return "Initial";
    case PUSHALL_RETRY_NO_DATA:      return "Retry (No Data)";
    case PUSHALL_PERIODIC:           return "Periodic";
    case PUSHALL_RECOVERY_PRINT:     return "Recovery (Print)";
    case PUSHALL_RECOVERY_CONN_DEAD: return "Recovery (Conn Dead)";
    case PUSHALL_RECOVERY_FINISH:    return "Recovery (Finish)";
    case PUSHALL_RECOVERY_IDLE:      return "Recovery (Idle)";
    case PUSHALL_MANUAL:             return "Manual";
    case PUSHALL_NONE:
    default:                         return "Never";
  }
}

// ---------------------------------------------------------------------------
//  Free TLS + MQTT client memory for one connection
// ---------------------------------------------------------------------------
static void releaseClients(MqttConn& c) {
  if (c.mqtt) { delete c.mqtt; c.mqtt = nullptr; }
  if (c.tls)  { delete c.tls;  c.tls  = nullptr; }
}

// ---------------------------------------------------------------------------
//  Lazy allocation of TLS + MQTT clients for one connection
// ---------------------------------------------------------------------------
static bool ensureClients(MqttConn& c) {
  if (c.tls && c.mqtt) return true;

  uint32_t freeHeap = ESP.getFreeHeap();
  MQTT_LOG("[%d] ensureClients() heap=%u min=%u", c.slotIndex, freeHeap, BAMBU_MIN_FREE_HEAP);
  if (freeHeap < BAMBU_MIN_FREE_HEAP) {
    MQTT_LOG("[%d] NOT ENOUGH HEAP!", c.slotIndex);
    return false;
  }

  if (!c.tls) {
    c.tls = new (std::nothrow) WiFiClientSecure();
    if (!c.tls) {
      MQTT_LOG("[%d] Failed to allocate WiFiClientSecure!", c.slotIndex);
      return false;
    }
    MQTT_LOG("[%d] WiFiClientSecure allocated OK", c.slotIndex);
  }
  if (isCloudMode(printers[c.slotIndex].config.mode)) {
    // Cloud: use built-in CA certificate bundle for proper TLS verification
    c.tls->setCACertBundle(rootca_crt_bundle_start);
    c.tls->setTimeout(15);
  } else {
    // LAN: printers use self-signed certs, skip verification
    c.tls->setInsecure();
    c.tls->setTimeout(5);
  }

  if (!c.mqtt) {
    c.mqtt = new (std::nothrow) PubSubClient(*c.tls);
    if (!c.mqtt) {
      MQTT_LOG("[%d] Failed to allocate PubSubClient!", c.slotIndex);
      delete c.tls;
      c.tls = nullptr;
      return false;
    }
    MQTT_LOG("[%d] PubSubClient allocated OK", c.slotIndex);
  }

  PrinterConfig& cfg = printers[c.slotIndex].config;
  if (isCloudMode(cfg.mode)) {
    const char* broker = getBambuBroker(cfg.region);
    MQTT_LOG("[%d] setServer(%s, %d) [CLOUD]", c.slotIndex, broker, BAMBU_PORT);
    c.mqtt->setServer(broker, BAMBU_PORT);
  } else {
    MQTT_LOG("[%d] setServer(%s, %d) [LOCAL]", c.slotIndex, cfg.ip, BAMBU_PORT);
    c.mqtt->setServer(cfg.ip, BAMBU_PORT);
  }
  if (!c.mqtt->setBufferSize(BAMBU_BUFFER_SIZE)) {
    MQTT_LOG("[%d] setBufferSize(%d) FAILED — not enough heap!", c.slotIndex, BAMBU_BUFFER_SIZE);
    delete c.mqtt; c.mqtt = nullptr;
    delete c.tls;  c.tls  = nullptr;
    return false;
  }
  c.mqtt->setCallback(mqttCallback);
  c.mqtt->setKeepAlive(isCloudMode(cfg.mode) ? BAMBU_CLOUD_KEEPALIVE : BAMBU_KEEPALIVE);

  return true;
}

// ---------------------------------------------------------------------------
//  Request pushall for one connection
// ---------------------------------------------------------------------------
static bool requestPushall(MqttConn& c, PushallReason reason) {
  if (!c.mqtt) return false;

  PrinterConfig& cfg = printers[c.slotIndex].config;
  char topic[64];
  snprintf(topic, sizeof(topic), "device/%s/request", cfg.serial);

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"pushing\":{\"sequence_id\":\"%u\",\"command\":\"pushall\","
           "\"version\":1,\"push_target\":1}}",
           c.pushallSeqId++);

  MQTT_LOG("[%d] pushall (%s) -> %s", c.slotIndex, pushallReasonToString(reason), topic);
  if (!c.mqtt->publish(topic, payload)) {
    MQTT_LOG("[%d] pushall publish FAILED", c.slotIndex);
    return false;
  }
  c.lastPushallRequest = millis();
  c.diag.pushallTotal++;
  switch (reason) {
    case PUSHALL_RECOVERY_PRINT:     c.diag.recoveryPrint++;    break;
    case PUSHALL_RECOVERY_CONN_DEAD: c.diag.recoveryConnDead++; break;
    case PUSHALL_RECOVERY_FINISH:    c.diag.recoveryFinish++;   break;
    case PUSHALL_RECOVERY_IDLE:      c.diag.recoveryIdle++;     break;
    default: break;
  }
  c.diag.lastPushallMs = c.lastPushallRequest;
  c.diag.lastPushallReason = (uint8_t)reason;
  return true;
}

static void clearLiveMetrics(BambuState& s) {
  s.nozzleTemp = 0;    s.nozzleTarget = 0;
  s.bedTemp = 0;       s.bedTarget = 0;
  s.chamberTemp = 0;
  s.progress = 0;
  s.remainingMinutes = 0;
  s.layerNum = 0;      s.totalLayers = 0;
  s.coolingFanPct = 0; s.auxFanPct = 0;
  s.chamberFanPct = 0; s.heatbreakFanPct = 0;
  s.speedLevel = 0;
  s.wifiSignal = 0;
  s.doorOpen = false;  s.doorSensorPresent = false;
  s.subtaskName[0] = '\0';
  // Clear AMS drying state so stale drying doesn't block screen sleep
  s.ams.anyDrying = false;
  for (uint8_t i = 0; i < AMS_MAX_UNITS; i++)
    s.ams.units[i].dryRemainMin = 0;
}

// ---------------------------------------------------------------------------
//  Find which MqttConn owns a given serial (for callback routing)
// ---------------------------------------------------------------------------
static MqttConn* findConnBySerial(const char* serial, size_t serialLen) {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!conns[i].active) continue;
    PrinterConfig& cfg = printers[conns[i].slotIndex].config;
    if (strlen(cfg.serial) == serialLen &&
        strncmp(cfg.serial, serial, serialLen) == 0) {
      return &conns[i];
    }
  }
  return nullptr;
}

// Map raw (unitId, trayInUnit) to sequential flat index.
// Raw unit ids may be non-sequential (e.g. 128 for AMS HT).
static uint8_t normalizeTrayIndex(const AmsState& ams,
                                  uint8_t rawUnitId, uint8_t trayInUnit) {
  if (trayInUnit >= AMS_TRAYS_PER_UNIT) return 255;
  for (uint8_t i = 0; i < ams.unitCount; i++) {
    if (ams.units[i].id == rawUnitId)
      return i * AMS_TRAYS_PER_UNIT + trayInUnit;
  }
  // Fallback: AMS2 compat (rawId 0-3 == seqIdx)
  if (rawUnitId < AMS_MAX_UNITS)
    return rawUnitId * AMS_TRAYS_PER_UNIT + trayInUnit;
  return 255;
}

// ---------------------------------------------------------------------------
//  Parse MQTT payload into a BambuState (extracted for routing)
// ---------------------------------------------------------------------------
static void parseMqttPayload(byte* payload, unsigned int length, BambuState& s, const char* serial) {
  const char* payloadEnd = (const char*)payload + length;

  // Filter document to reduce parse memory
  JsonDocument filter;
  JsonObject pf = filter["print"].to<JsonObject>();
  pf["gcode_state"] = true;
  pf["mc_percent"] = true;
  pf["mc_remaining_time"] = true;
  pf["nozzle_temper"] = true;
  pf["nozzle_target_temper"] = true;
  pf["bed_temper"] = true;
  pf["bed_target_temper"] = true;
  pf["chamber_temper"] = true;
  pf["ctc"]["info"]["temp"] = true;           // legacy/alternate chamber temp path
  pf["device"]["ctc"]["info"]["temp"] = true; // H2C/H2D chamber temp path
  pf["subtask_name"] = true;
  pf["layer_num"] = true;
  pf["total_layer_num"] = true;
  pf["cooling_fan_speed"] = true;
  pf["big_fan1_speed"] = true;
  pf["big_fan2_speed"] = true;
  pf["heatbreak_fan_speed"] = true;
  pf["wifi_signal"] = true;
  pf["spd_lvl"] = true;
  pf["stat"] = true;       // H2 door sensor (hex string, bit 0x00800000 = door open)
  pf["home_flag"] = true;  // X1 series door sensor (int, bit 23 = door open)
  // Note: H2D/H2C extruder data is parsed separately from raw payload (see below)

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length,
                                              DeserializationOption::Filter(filter));
  if (err) {
    MQTT_LOG("JSON parse error: %s", err.c_str());
    return;
  }

  // Deferred activeTray sources (resolved after AMS unit loop)
  uint8_t pendingSnowAmsId = 255;   // raw AMS unit id from snow
  uint8_t pendingSnowTrayIdx = 0;
  bool    hasSnowData = false;       // true when snow was parsed for active nozzle

  // H2D/H2C dual nozzle: parse extruder directly from raw payload
  // (bypasses ArduinoJson filter which strips it due to deep nesting)
  const char* extPos = (const char*)memmem(payload, length, "\"extruder\":", 11);
  if (extPos) {
    const char* objStart = extPos + 11;  // skip past "extruder":
    // Skip whitespace
    while (objStart < payloadEnd && (*objStart == ' ' || *objStart == '\t')) objStart++;
    if (objStart < payloadEnd && *objStart == '{') {
      JsonDocument extDoc;
      if (!deserializeJson(extDoc, objStart, (size_t)(payloadEnd - objStart))) {
        JsonArray info = extDoc["info"];
        if (info.size() >= 2) {
          if (!s.dualNozzle) Serial.println("MQTT: dual nozzle DETECTED (H2D/H2C)");
          s.dualNozzle = true;

          if (extDoc["state"].is<unsigned int>()) {
            uint32_t st = extDoc["state"].as<unsigned int>();
            s.activeNozzle = (st >> 4) & 0x0F;
            if (s.activeNozzle > 1) s.activeNozzle = 0;
          }

          for (JsonObject entry : info) {
            if (!entry["id"].is<int>()) continue;
            uint8_t id = entry["id"].as<int>();

            // Extract snow (per-nozzle active tray) - defer normalization
            // until after AMS units are parsed (need units[].id mapping)
            if (entry["snow"].is<unsigned int>()) {
              uint32_t snow = entry["snow"].as<unsigned int>();
              uint8_t amsIdx  = snow >> 8;
              uint8_t trayIdx = snow & 0x03;
              MQTT_LOG("nozzle[%d] snow=%u -> ams=%d tray=%d", id, snow, amsIdx, trayIdx);
              if (id == s.activeNozzle) {
                pendingSnowAmsId = amsIdx;
                pendingSnowTrayIdx = trayIdx;
                hasSnowData = true;
              }
            }

            if (id == s.activeNozzle && entry["temp"].is<unsigned int>()) {
              uint32_t packed = entry["temp"].as<unsigned int>();
              s.nozzleTemp   = (float)(packed & 0xFFFF);
              s.nozzleTarget = (float)(packed >> 16);
              s.lastUpdate = millis();
              s.lastPrintDataMs = millis();
              MQTT_LOG("dual nozzle=%d temp=%.0f target=%.0f", s.activeNozzle, s.nozzleTemp, s.nozzleTarget);
            }
          }
        }
      }
    }
  }

  // AMS data: parse from raw payload (deeply nested, same approach as extruder)
  // Search for "ams":{ (outer object, not "ams":[ which is the inner array)
  {
    const char* search = (const char*)payload;
    size_t rem = length;
    const char* amsObj = nullptr;

    while (rem > 6) {
      const char* f = (const char*)memmem(search, rem, "\"ams\":", 6);
      if (!f) break;
      const char* v = f + 6;
      while (v < payloadEnd && (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')) v++;
      if (v < payloadEnd && *v == '{') { amsObj = v; break; }
      search = f + 6;
      size_t consumed = (size_t)(search - (const char*)payload);
      rem = (consumed < length) ? (length - consumed) : 0;
    }

    if (amsObj) {
      // Find matching closing brace
      int depth = 0;
      const char* end = amsObj;
      const char* limit = (const char*)payload + length;
      while (end < limit) {
        if (*end == '{') depth++;
        else if (*end == '}') { depth--; if (depth == 0) { end++; break; } }
        end++;
      }
      if (depth == 0) {
        JsonDocument amsDoc;
        if (!deserializeJson(amsDoc, amsObj, (size_t)(end - amsObj))) {
          s.ams.present = true;
          JsonArray units = amsDoc["ams"];
          bool hasTraySnapshot = false;
          for (JsonObject unit : units) {
            if (unit["tray"].is<JsonArray>()) {
              hasTraySnapshot = true;
              break;
            }
          }

          bool hasExplicitTrayNow = amsDoc["tray_now"].is<const char*>() && !s.dualNozzle;
          uint8_t rawTrayNow = 255;
          if (hasExplicitTrayNow) rawTrayNow = atoi(amsDoc["tray_now"].as<const char*>());

          // --- Pass 1: unit-level data (always, even partial updates) ---
          // Clear unit-level visibility for ALL units before rebuilding.
          // Prevents ghost units staying .present with stale data when a
          // previously-visible unit disappears from a partial update.
          // Preserves trayCount (cached until next tray snapshot) and drying totals.
          s.ams.unitCount = 0;
          s.ams.anyDrying = false;
          for (uint8_t i = 0; i < AMS_MAX_UNITS; i++) {
            AmsUnit& u = s.ams.units[i];
            uint8_t savedTrayCount = u.trayCount;
            uint16_t savedDryTotal = u.dryTotalMin;
            uint16_t savedDryRemain = u.dryRemainMin;
            u.present = false;
            u.id = 0;
            u.humidity = 0;
            u.humidityRaw = 0;
            u.temp = 0;
            u.trayCount = savedTrayCount;
            u.dryTotalMin = savedDryTotal;
            u.dryRemainMin = savedDryRemain;
          }

          uint8_t unitIdx = 0;
          for (JsonObject unit : units) {
            if (!unit["id"].is<const char*>()) continue;
            uint8_t uid = atoi(unit["id"].as<const char*>());
            if (unitIdx >= AMS_MAX_UNITS) continue;
            s.ams.unitCount++;

            AmsUnit& u = s.ams.units[unitIdx];
            u.present = true;
            u.id = uid;
            if (!unit["dry_time"].isNull()) {
              uint16_t dt = unit["dry_time"].as<uint16_t>();
              if (dt > 0 && (u.dryRemainMin == 0 || dt > u.dryTotalMin))
                u.dryTotalMin = dt;
              if (dt == 0) u.dryTotalMin = 0;
              u.dryRemainMin = dt;
            }
            if (unit["humidity"].is<const char*>())
              u.humidity = atoi(unit["humidity"].as<const char*>());
            if (unit["humidity_raw"].is<const char*>())
              u.humidityRaw = atoi(unit["humidity_raw"].as<const char*>());
            if (unit["temp"].is<const char*>())
              u.temp = atof(unit["temp"].as<const char*>());
            if (u.dryRemainMin > 0) s.ams.anyDrying = true;
            unitIdx++;
          }
          if (unitIdx > 0) s.lastUpdate = millis();  // AMS data = connection alive

          // --- Pass 2: tray-level data (only full snapshots) ---
          if (hasTraySnapshot) {
            memset(s.ams.trays, 0, sizeof(s.ams.trays));
            for (auto& t : s.ams.trays) t.remain = -1;

            // Reset external spool - re-populated if vt_tray present in this message
            s.ams.vtPresent = false;
            s.ams.vtType[0] = '\0';
            s.ams.vtColorRgb565 = 0;

            unitIdx = 0;
            for (JsonObject unit : units) {
              if (!unit["id"].is<const char*>()) continue;
              if (unitIdx >= AMS_MAX_UNITS) continue;
              uint8_t seqIdx = unitIdx;
              unitIdx++;

              JsonArray trays = unit["tray"];
              uint8_t parsedTrays = 0;
              for (JsonObject tray : trays) {
                if (!tray["id"].is<const char*>()) continue;
                uint8_t tid = atoi(tray["id"].as<const char*>());
                if (tid >= AMS_TRAYS_PER_UNIT) continue;

                uint8_t idx = seqIdx * AMS_TRAYS_PER_UNIT + tid;
                AmsTray& t = s.ams.trays[idx];

                if (tray["tray_type"].is<const char*>()) {
                  t.present = true;
                  if (tray["tray_color"].is<const char*>()) {
                    const char* colorStr = tray["tray_color"].as<const char*>();
                    t.colorRgb565 = bambuColorToRgb565(colorStr);
                    MQTT_LOG("AMS tray %d color: \"%s\" -> 0x%04X", idx, colorStr, t.colorRgb565);
                  }
                  const char* name = nullptr;
                  if (tray["tray_sub_brands"].is<const char*>() &&
                      strlen(tray["tray_sub_brands"].as<const char*>()) > 0)
                    name = tray["tray_sub_brands"].as<const char*>();
                  else
                    name = tray["tray_type"].as<const char*>();
                  if (name) strlcpy(t.type, name, sizeof(t.type));
                  t.remain = tray["remain"].is<int>() ? (int8_t)tray["remain"].as<int>() : -1;
                } else {
                  t.present = false;
                  t.type[0] = '\0';
                }
                parsedTrays++;
              }
              s.ams.units[seqIdx].trayCount = parsedTrays;
            }
          } else if (!units.isNull()) {
            MQTT_LOG("AMS partial update - unit data refreshed, keeping cached trays");
          }

          // --- activeTray normalization (after units are populated) ---
          // Snow special values (from pushall_dump.json):
          //   0xFFFF (65535) -> amsIdx=255 = no active tray
          //   0xFEFF (65279) -> amsIdx=254 = external spool
          // These mirror tray_now sentinels: 255=none, 254=external.
          if (s.dualNozzle) {
            if (pendingSnowAmsId == 254) {
              s.ams.activeTray = 254;  // external spool sentinel
              MQTT_LOG("activeTray: snow external spool (amsId=254)");
            } else if (pendingSnowAmsId != 255) {
              uint8_t normalized = normalizeTrayIndex(s.ams, pendingSnowAmsId, pendingSnowTrayIdx);
              if (normalized != 255) {
                s.ams.activeTray = normalized;
              } else {
                MQTT_LOG("activeTray: snow ams=%d tray=%d unresolved - keeping cached=%d",
                         pendingSnowAmsId, pendingSnowTrayIdx, s.ams.activeTray);
              }
            } else if (hasSnowData) {
              // snow was present but amsIdx==255 means "no active tray"
              s.ams.activeTray = 255;
              MQTT_LOG("activeTray: snow reports no active tray");
            }
            // else: no extruder/snow data in this message - keep cached activeTray
          } else if (hasExplicitTrayNow) {
            if (rawTrayNow == 254) {
              s.ams.activeTray = 254;  // external spool sentinel
            } else if (rawTrayNow == 255) {
              s.ams.activeTray = 255;  // explicit "no active tray" from printer
            } else {
              uint8_t rawUnit = rawTrayNow / AMS_TRAYS_PER_UNIT;
              uint8_t rawTray = rawTrayNow % AMS_TRAYS_PER_UNIT;
              uint8_t normalized = normalizeTrayIndex(s.ams, rawUnit, rawTray);
              if (normalized != 255) {
                s.ams.activeTray = normalized;
              } else {
                MQTT_LOG("activeTray: tray_now=%d unresolved - keeping cached=%d",
                         rawTrayNow, s.ams.activeTray);
              }
            }
            MQTT_LOG("activeTray: tray_now=%d -> normalized=%d", rawTrayNow, s.ams.activeTray);
          }

          MQTT_LOG("AMS: %d units, active tray=%d, drying=%s",
                   s.ams.unitCount, s.ams.activeTray,
                   s.ams.anyDrying ? "YES" : "no");
        }
      }
    }
  }

  // External spool (vt_tray)
  {
    const char* vtPos = (const char*)memmem(payload, length, "\"vt_tray\":", 10);
    if (vtPos) {
      const char* v = vtPos + 10;
      while (v < payloadEnd && (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')) v++;
      if (v < payloadEnd && *v == '{') {
        JsonDocument vtDoc;
        if (!deserializeJson(vtDoc, v, (size_t)(payloadEnd - v))) {
          if (vtDoc["tray_type"].is<const char*>()) {
            s.ams.vtPresent = true;
            if (vtDoc["tray_color"].is<const char*>())
              s.ams.vtColorRgb565 = bambuColorToRgb565(vtDoc["tray_color"].as<const char*>());
            const char* name = nullptr;
            if (vtDoc["tray_sub_brands"].is<const char*>() &&
                strlen(vtDoc["tray_sub_brands"].as<const char*>()) > 0)
              name = vtDoc["tray_sub_brands"].as<const char*>();
            else
              name = vtDoc["tray_type"].as<const char*>();
            if (name) strlcpy(s.ams.vtType, name, sizeof(s.ams.vtType));
          }
        }
      }
    }
  }

  JsonObject print = doc["print"];
  if (print.isNull()) {
    s.lastUpdate = millis();  // message arrived, but no print data
    return;
  }

  if (mqttDebugLog && print["gcode_state"].is<const char*>()) {
    Serial.printf("MQTT: state=%s progress=%d nozzle=%.0f bed=%.0f\n",
                  print["gcode_state"].as<const char*>(),
                  print["mc_percent"] | -1,
                  print["nozzle_temper"] | -1.0f,
                  print["bed_temper"] | -1.0f);
  }

  // Delta merge: only update fields present in this message.
  // Track whether core print-dashboard fields arrived (for stale recovery).
  bool corePrintData = false;

  if (print["gcode_state"].is<const char*>()) {
    corePrintData = true;
    const char* state = print["gcode_state"];
    setPrinterGcodeStateRaw(s, state);
    s.printing = isPrintingGcodeState(s.gcodeStateId);
  }

  if (print["mc_percent"].is<int>()) {
    corePrintData = true;
    s.progress = print["mc_percent"].as<int>();
  }

  if (print["mc_remaining_time"].is<int>()) {
    corePrintData = true;
    s.remainingMinutes = print["mc_remaining_time"].as<int>();
  }

  // For dual-nozzle (H2D/H2C): nozzle_temper is the INACTIVE nozzle - skip it
  // Active nozzle temp comes from extruder.info[] parsed above
  if (!s.dualNozzle) {
    if (print["nozzle_temper"].is<float>()) {
      corePrintData = true;
      s.nozzleTemp = print["nozzle_temper"].as<float>();
    } else if (print["nozzle_temper"].is<int>()) {
      corePrintData = true;
      s.nozzleTemp = print["nozzle_temper"].as<int>();
    }

    if (print["nozzle_target_temper"].is<float>()) {
      corePrintData = true;
      s.nozzleTarget = print["nozzle_target_temper"].as<float>();
    } else if (print["nozzle_target_temper"].is<int>()) {
      corePrintData = true;
      s.nozzleTarget = print["nozzle_target_temper"].as<int>();
    }
  }

  if (print["bed_temper"].is<float>()) {
    corePrintData = true;
    s.bedTemp = print["bed_temper"].as<float>();
  } else if (print["bed_temper"].is<int>()) {
    corePrintData = true;
    s.bedTemp = print["bed_temper"].as<int>();
  }

  if (print["bed_target_temper"].is<float>()) {
    corePrintData = true;
    s.bedTarget = print["bed_target_temper"].as<float>();
  } else if (print["bed_target_temper"].is<int>()) {
    corePrintData = true;
    s.bedTarget = print["bed_target_temper"].as<int>();
  }

  if (print["chamber_temper"].is<float>()) {
    corePrintData = true;
    s.chamberTemp = print["chamber_temper"].as<float>();
  } else if (print["chamber_temper"].is<int>()) {
    corePrintData = true;
    s.chamberTemp = print["chamber_temper"].as<int>();
  } else if (print["ctc"]["info"]["temp"].is<int>()) {
    // ctc.info.temp may be packed: (target << 16) | current — extract current
    corePrintData = true;
    s.chamberTemp = (float)(print["ctc"]["info"]["temp"].as<int>() & 0xFFFF);
  } else if (print["ctc"]["info"]["temp"].is<const char*>()) {
    corePrintData = true;
    s.chamberTemp = (float)((int)atof(print["ctc"]["info"]["temp"].as<const char*>()) & 0xFFFF);
  } else if (print["device"]["ctc"]["info"]["temp"].is<int>()) {
    // H2C/H2D: device.ctc.info.temp — packed: (target << 16) | current
    corePrintData = true;
    s.chamberTemp = (float)(print["device"]["ctc"]["info"]["temp"].as<int>() & 0xFFFF);
  } else if (print["device"]["ctc"]["info"]["temp"].is<const char*>()) {
    corePrintData = true;
    s.chamberTemp = (float)((int)atof(print["device"]["ctc"]["info"]["temp"].as<const char*>()) & 0xFFFF);
  }

  // H2D/H2C fallback: parse ctc.info.temp from raw payload via memmem
  // (ArduinoJson filter may strip device.ctc due to deep nesting in the
  //  large device object — same class of issue as extruder, see line ~284)
  if (s.chamberTemp == 0) {
    const char* ctcPos = (const char*)memmem(payload, length, "\"ctc\":", 6);
    if (ctcPos) {
      size_t remain = (size_t)(payloadEnd - ctcPos);
      size_t searchLen = (remain > 128) ? 128 : remain;
      const char* tempPos = (const char*)memmem(ctcPos, searchLen, "\"temp\":", 7);
      if (tempPos) {
        const char* valStart = tempPos + 7;
        while (valStart < payloadEnd && *valStart == ' ') valStart++;
        int rawVal = atoi(valStart);
        float ctcTemp = (float)(rawVal & 0xFFFF);
        if (ctcTemp > 0 && ctcTemp < 200) {
          s.chamberTemp = ctcTemp;
          corePrintData = true;
          MQTT_LOG("chamber temp from memmem ctc: %.1f", ctcTemp);
        }
      }
    }
  }

  if (print["subtask_name"].is<const char*>()) {
    corePrintData = true;
    const char* name = print["subtask_name"];
    strlcpy(s.subtaskName, name, sizeof(s.subtaskName));
  }

  if (print["layer_num"].is<int>()) {
    corePrintData = true;
    s.layerNum = print["layer_num"].as<int>();
  }

  if (print["total_layer_num"].is<int>()) {
    corePrintData = true;
    s.totalLayers = print["total_layer_num"].as<int>();
  }

  // Fan speeds (Bambu sends 0-15, may arrive as int or string)
  auto parseFan = [](JsonVariant v) -> int {
    if (v.is<int>()) return v.as<int>();
    if (v.is<const char*>()) return atoi(v.as<const char*>());
    return -1;
  };

  int fanVal;
  fanVal = parseFan(print["cooling_fan_speed"]);
  if (fanVal >= 0) { corePrintData = true; s.coolingFanPct = (fanVal * 100) / 15; }

  fanVal = parseFan(print["big_fan1_speed"]);
  if (fanVal >= 0) { corePrintData = true; s.auxFanPct = (fanVal * 100) / 15; }

  fanVal = parseFan(print["big_fan2_speed"]);
  if (fanVal >= 0) { corePrintData = true; s.chamberFanPct = (fanVal * 100) / 15; }

  fanVal = parseFan(print["heatbreak_fan_speed"]);
  if (fanVal >= 0) { corePrintData = true; s.heatbreakFanPct = (fanVal * 100) / 15; }

  // Non-core fields: wifi_signal, spd_lvl, stat - don't count as core print data
  if (print["wifi_signal"].is<const char*>())
    s.wifiSignal = atoi(print["wifi_signal"].as<const char*>());
  else if (print["wifi_signal"].is<int>())
    s.wifiSignal = print["wifi_signal"].as<int>();

  if (print["spd_lvl"].is<int>())
    s.speedLevel = print["spd_lvl"].as<int>();

  // Door sensor: model-specific field and bit layout.
  //   - H2D/H2C (dualNozzle): "stat" hex string, bit 0x00800000 = door open.
  //     H2C sends 9+ hex digits (>32 bit), must use strtoull.
  //     Note: X1C also sends "stat" but with different semantics (bit 23 is
  //     always set there), so gate this path to dual-nozzle H2 printers.
  //   - X1 series (serial prefix 00M = X1C, 00W = X1E): "home_flag" int,
  //     bit 23 = door open. Matches ha-bambulab convention.
  //   - P1/A1: no door sensor - nothing to parse.
  if (s.dualNozzle && print["stat"].is<const char*>()) {
    uint64_t statVal = strtoull(print["stat"].as<const char*>(), nullptr, 16);
    bool wasOpen = s.doorOpen;
    s.doorOpen = (statVal & 0x00800000) != 0;
    if (!s.doorSensorPresent) {
      s.doorSensorPresent = true;
      MQTT_LOG("door sensor detected (H2 stat=0x%llX, door=%s)", statVal, s.doorOpen ? "OPEN" : "CLOSED");
    } else if (s.doorOpen != wasOpen) {
      MQTT_LOG("door %s (H2 stat=0x%llX)", s.doorOpen ? "OPENED" : "CLOSED", statVal);
    }
  } else if (isX1SeriesSerial(serial) && print["home_flag"].is<int32_t>()) {
    uint32_t homeFlag = (uint32_t)print["home_flag"].as<int32_t>();
    bool wasOpen = s.doorOpen;
    s.doorOpen = (homeFlag & (1UL << 23)) != 0;
    if (!s.doorSensorPresent) {
      s.doorSensorPresent = true;
      MQTT_LOG("door sensor detected (X1 home_flag=0x%08X, door=%s)", homeFlag, s.doorOpen ? "OPEN" : "CLOSED");
    } else if (s.doorOpen != wasOpen) {
      MQTT_LOG("door %s (X1 home_flag=0x%08X)", s.doorOpen ? "OPENED" : "CLOSED", homeFlag);
    }
  }

  s.lastUpdate = millis();
  if (corePrintData) s.lastPrintDataMs = millis();
}

// ---------------------------------------------------------------------------
//  MQTT callback — routes to correct printer slot by serial in topic
// ---------------------------------------------------------------------------
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  esp_task_wdt_reset();

  // Extract serial from topic: "device/{serial}/report"
  const char* start = topic + 7;  // skip "device/"
  const char* end = strchr(start, '/');
  if (!end) return;
  size_t serialLen = end - start;

  MqttConn* c = findConnBySerial(start, serialLen);
  if (!c) {
    MQTT_LOG("callback: unknown serial in topic %s", topic);
    return;
  }

  c->diag.messagesRx++;
  MQTT_LOG("[%d] callback #%u topic=%s len=%u", c->slotIndex, c->diag.messagesRx, topic, length);

  c->gotDataSinceConnect = true;
  BambuState& s = printers[c->slotIndex].state;
  const char* cfgSerial = printers[c->slotIndex].config.serial;
  parseMqttPayload(payload, length, s, cfgSerial);
}

// ---------------------------------------------------------------------------
//  Reconnect one connection
// ---------------------------------------------------------------------------
static void reconnectConn(MqttConn& c) {
  PrinterConfig& cfg = printers[c.slotIndex].config;

  if (cfg.mode == CONN_LOCAL && strlen(cfg.ip) == 0) {
    MQTT_LOG("[%d] skip: no IP configured", c.slotIndex);
    return;
  }
  if (cfg.mode == CONN_LOCAL && strlen(cfg.serial) == 0) {
    MQTT_LOG("[%d] skip: no serial configured (needed for MQTT topic)", c.slotIndex);
    return;
  }
  if (isCloudMode(cfg.mode) && strlen(cfg.cloudUserId) == 0) {
    MQTT_LOG("[%d] skip: no cloudUserId — token may be missing or invalid, re-login via web UI", c.slotIndex);
    return;
  }
  if (isCloudMode(cfg.mode) && strlen(cfg.serial) == 0) {
    MQTT_LOG("[%d] skip: no serial configured", c.slotIndex);
    return;
  }

  unsigned long now = millis();

  // Exponential backoff: increase interval after repeated failures
  // Cloud uses longer base interval to avoid triggering broker rate limits
  unsigned long interval = isCloudMode(cfg.mode) ? 30000 : BAMBU_RECONNECT_INTERVAL;
  if (c.consecutiveFails >= BAMBU_BACKOFF_PHASE1 + BAMBU_BACKOFF_PHASE2) {
    interval = BAMBU_BACKOFF_PHASE3_MS;
  } else if (c.consecutiveFails >= BAMBU_BACKOFF_PHASE1) {
    interval = BAMBU_BACKOFF_PHASE2_MS;
  }

  // First attempt is immediate; subsequent attempts respect the interval
  if (c.diag.attempts > 0 && now - c.lastReconnectAttempt < interval) return;

  c.diag.attempts++;
  c.diag.lastAttemptMs = now;
  c.lastReconnectAttempt = now;

  MQTT_LOG("[%d] === reconnect attempt #%u (fails=%u, interval=%lus) [%s] ===",
           c.slotIndex, c.diag.attempts, c.consecutiveFails, interval / 1000,
           isCloudMode(cfg.mode) ? "CLOUD" : "LOCAL");
  MQTT_LOG("[%d] serial=%s heap=%u WiFi=%d", c.slotIndex, cfg.serial, ESP.getFreeHeap(), WiFi.status());

  // For cloud: force-release old TLS/MQTT objects before reconnecting.
  // This ensures a clean TLS handshake with no stale session state.
  if (isCloudMode(cfg.mode) && c.tls) {
    c.tls->stop();
    releaseClients(c);
  }

  if (!ensureClients(c)) {
    MQTT_LOG("[%d] ensureClients() FAILED", c.slotIndex);
    c.diag.lastRc = -2;
    c.consecutiveFails++;
    c.lastReconnectAttempt = millis();  // respect backoff on OOM
    return;
  }
  if (c.mqtt->connected()) return;

  // TCP reachability test (local mode only)
  if (cfg.mode == CONN_LOCAL) {
    WiFiClient tcp;
    tcp.setTimeout(1);
    MQTT_LOG("[%d] TCP test to %s:%d...", c.slotIndex, cfg.ip, BAMBU_PORT);
    unsigned long tcpT0 = millis();
    c.diag.tcpOk = tcp.connect(cfg.ip, BAMBU_PORT);
    MQTT_LOG("[%d] TCP test %s in %lums", c.slotIndex, c.diag.tcpOk ? "OK" : "FAILED", millis() - tcpT0);
    tcp.stop();
    if (!c.diag.tcpOk) {
      MQTT_LOG("[%d] Printer not reachable on network!", c.slotIndex);
      c.diag.lastRc = -2;
      c.consecutiveFails++;
      return;
    }
  } else {
    c.diag.tcpOk = true;
  }

  // Client ID: cloud uses random suffix (like pybambu) to avoid session
  // collision when broker hasn't cleaned up the previous TLS session yet.
  // LAN uses deterministic ID (stable per device).
  char clientId[32];
  if (isCloudMode(cfg.mode)) {
    snprintf(clientId, sizeof(clientId), "bblp_%08x%04x",
             (uint32_t)esp_random(), (uint16_t)(esp_random() & 0xFFFF));
  } else {
    snprintf(clientId, sizeof(clientId), "bambu_%08x_%d",
             (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), c.slotIndex);
  }

  unsigned long t0 = millis();
  esp_task_wdt_reset();

  bool connected = false;
  if (isCloudMode(cfg.mode)) {
    char tokenBuf[1200];
    if (!loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      MQTT_LOG("[%d] No cloud token in NVS!", c.slotIndex);
      c.diag.lastRc = -2;
      return;
    }
    MQTT_LOG("[%d] connect(id=%s, user=%s) [CLOUD]...", c.slotIndex, clientId, cfg.cloudUserId);
    connected = c.mqtt->connect(clientId, cfg.cloudUserId, tokenBuf);
    memset(tokenBuf, 0, sizeof(tokenBuf));
  } else {
    MQTT_LOG("[%d] connect(id=%s, user=%s) [LOCAL]...", c.slotIndex, clientId, BAMBU_USERNAME);
    connected = c.mqtt->connect(clientId, BAMBU_USERNAME, cfg.accessCode);
  }

  if (connected) {
    char topic[64];
    snprintf(topic, sizeof(topic), "device/%s/report", cfg.serial);
    if (!c.mqtt->subscribe(topic)) {
      MQTT_LOG("[%d] subscribe FAILED for %s — disconnecting", c.slotIndex, topic);
      c.mqtt->disconnect();
      c.consecutiveFails++;
      c.diag.lastRc = -2;
      return;
    }

    printers[c.slotIndex].state.connected = true;
    // Detect "quick disconnect" pattern: if last connection lasted < 30s,
    // don't reset backoff — the broker may be rejecting us
    bool quickDisconnect = c.connectTime > 0 &&
                           (millis() - c.connectTime < 30000);
    c.connectTime = millis();
    c.initialPushallSent = false;
    c.gotDataSinceConnect = false;
    if (!quickDisconnect) {
      c.consecutiveFails = 0;  // only reset backoff on stable connections
    } else {
      MQTT_LOG("[%d] previous connection was short-lived, keeping backoff=%u",
               c.slotIndex, c.consecutiveFails);
    }
    c.diag.lastRc = 0;
    c.diag.connectDurMs = millis() - t0;
    MQTT_LOG("[%d] CONNECTED in %lums, subscribed to %s", c.slotIndex, c.diag.connectDurMs, topic);
  } else {
    printers[c.slotIndex].state.connected = false;
    c.consecutiveFails++;
    c.diag.lastRc = c.mqtt->state();
    c.diag.connectDurMs = millis() - t0;
    MQTT_LOG("[%d] CONNECT FAILED rc=%d (%s) took %lums",
             c.slotIndex, c.diag.lastRc, mqttRcToString(c.diag.lastRc), c.diag.connectDurMs);
    if (isCloudMode(cfg.mode)) {
      MQTT_LOG("[%d] config: serial=%s userId=%s region=%d",
               c.slotIndex, cfg.serial, cfg.cloudUserId, cfg.region);
      if (c.diag.lastRc == 4 || c.diag.lastRc == 5) {
        MQTT_LOG("[%d] Cloud token may be expired — re-login via web UI", c.slotIndex);
      }
    } else {
      MQTT_LOG("[%d] config: ip=%s serial=%s code_len=%d",
               c.slotIndex, cfg.ip, cfg.serial, (int)strlen(cfg.accessCode));
    }
  }
}

// ---------------------------------------------------------------------------
//  Handle one connection (loop, pushall, stale)
// ---------------------------------------------------------------------------
static void handleConn(MqttConn& c) {
  PrinterConfig& cfg = printers[c.slotIndex].config;
  BambuState& s = printers[c.slotIndex].state;

  bool isConnected = c.mqtt && c.mqtt->connected();

  // Detect connected->disconnected transition for diagnostics
  if (!isConnected && c.wasConnected) {
    int rc = c.mqtt ? c.mqtt->state() : -99;
    unsigned long alive = c.connectTime > 0 ? millis() - c.connectTime : 0;
    char errBuf[64] = {0};
    int tlsErr = c.tls ? c.tls->lastError(errBuf, sizeof(errBuf)) : 0;
    bool tlsConn = c.tls ? c.tls->connected() : false;
    int tlsAvail = c.tls ? c.tls->available() : -1;
    MQTT_LOG("[%d] *** DISCONNECTED rc=%d (%s) after %lums, msgs=%u pushall=%d",
             c.slotIndex, rc, mqttRcToString(rc), alive,
             c.diag.messagesRx, (int)c.initialPushallSent);
    MQTT_LOG("[%d] *** TLS: connected=%d available=%d lastError=%d [%s]",
             c.slotIndex, tlsConn, tlsAvail, tlsErr, errBuf);
  }
  c.wasConnected = isConnected;

  if (isConnected) {
    c.mqtt->loop();
    isConnected = c.mqtt->connected();  // re-evaluate: loop() can disconnect
  }

  if (!isConnected) {
    // Grace period: don't flag as disconnected until gone for 3s.
    // This prevents screen flicker on momentary cloud drops.
    if (c.disconnectSince == 0) {
      c.disconnectSince = millis();
    }
    if (s.connected && millis() - c.disconnectSince < 3000) {
      return;  // still within grace period, skip reconnect
    }
    s.connected = false;
    reconnectConn(c);
  } else {
    c.disconnectSince = 0;  // reset grace timer on healthy connection

    // Initial pushall: request full status once after connecting.
    // Cloud also gets this — without it, display shows "waiting" until
    // the broker naturally sends a full status (can take minutes).
    if (!c.initialPushallSent && c.connectTime > 0 &&
        millis() - c.connectTime > BAMBU_PUSHALL_INITIAL_DELAY) {
      esp_task_wdt_reset();
      if (requestPushall(c, PUSHALL_INITIAL))
        c.initialPushallSent = true;
    }

    // Periodic pushall and retry: LAN only.
    // Cloud pushes data automatically; repeated publish to request topic
    // may trigger access_denied (TLS alert 49) on the cloud broker.
    if (!isCloudMode(cfg.mode)) {
      if (c.initialPushallSent && !c.gotDataSinceConnect &&
          millis() - c.lastPushallRequest > 10000) {
        MQTT_LOG("[%d] No data after pushall, retrying...", c.slotIndex);
        esp_task_wdt_reset();
        requestPushall(c, PUSHALL_RETRY_NO_DATA);
      }

      if (c.initialPushallSent && c.gotDataSinceConnect &&
          millis() - c.lastPushallRequest > BAMBU_PUSHALL_INTERVAL) {
        esp_task_wdt_reset();
        requestPushall(c, PUSHALL_PERIODIC);
      }
    }
  }

  // Stale detection uses two freshness signals:
  // - lastPrintDataMs: core dashboard fields (temps, fans, progress, state)
  // - lastUpdate: any MQTT message at all (connection alive)
  //
  // Key rule: if lastUpdate is fresh (messages flowing) but lastPrintDataMs
  // is stale, the printer is ALIVE but not sending core data. In that case
  // send a rate-limited recovery pushall but NEVER clear to IDLE/UNKNOWN -
  // showing "Ready" during an active print is worse than showing stale data.
  //
  // Clear to IDLE only when the connection itself is dead (lastUpdate stale).

  bool cloud = isCloudMode(cfg.mode);
  unsigned long connStaleMs = cloud ? BAMBU_STALE_TIMEOUT * 5 : BAMBU_STALE_TIMEOUT;
  bool connAlive = s.lastUpdate > 0 && (millis() - s.lastUpdate <= connStaleMs);

  // Cloud recovery pushall rate limit: at most one per 2 minutes.
  bool cloudPushallThrottled = cloud &&
      c.lastPushallRequest > 0 && millis() - c.lastPushallRequest < 120000;

  // Cloud recovery cooldown: after a recovery resolves, don't re-enter for 5 min.
  // Prevents chatty recovery cycles when cloud has frequent >120s gaps in core data.
  bool recoveryCooldown = cloud &&
      c.lastRecoveryResolvedMs > 0 && millis() - c.lastRecoveryResolvedMs < 300000;

  // --- Active print: core data freshness ---
  if (s.printing) {
    unsigned long printStaleMs = cloud ? BAMBU_PRINT_STALE_TIMEOUT : BAMBU_STALE_TIMEOUT;
    bool coreStale = s.lastPrintDataMs > 0 && millis() - s.lastPrintDataMs > printStaleMs;

    if (coreStale && connAlive) {
      // Messages flowing but no core print data - send recovery pushall (rate-limited).
      // Keep showing printing screen with stale values.
      if (isConnected && c.stalePushallSentMs == 0 && !cloudPushallThrottled && !recoveryCooldown) {
        MQTT_LOG("[%d] core print data stale - sending recovery pushall", c.slotIndex);
        esp_task_wdt_reset();
        if (requestPushall(c, PUSHALL_RECOVERY_PRINT))
          c.stalePushallSentMs = millis();
      }
      // Don't give up - connection is alive, keep printing screen
    } else if (coreStale && !connAlive) {
      // Connection truly dead - no messages at all for connStaleMs.
      if (isConnected && c.stalePushallSentMs == 0) {
        MQTT_LOG("[%d] connection dead during print - sending recovery pushall", c.slotIndex);
        esp_task_wdt_reset();
        if (requestPushall(c, PUSHALL_RECOVERY_CONN_DEAD))
          c.stalePushallSentMs = millis();
      } else if (c.stalePushallSentMs == 0 ||
                 millis() - c.stalePushallSentMs > 30000) {
        // Recovery pushall sent 30s ago with no response - give up
        MQTT_LOG("[%d] no response to recovery pushall - clearing print state", c.slotIndex);
        s.printing = false;
        setPrinterGcodeStateCanonical(s, GCODE_IDLE);
        c.stalePushallSentMs = 0;
      }
    } else {
      if (c.stalePushallSentMs > 0) c.lastRecoveryResolvedMs = millis();
      c.stalePushallSentMs = 0;  // core data is fresh
    }

  // --- FINISH: core data freshness ---
  } else if (s.gcodeStateId == GCODE_FINISH) {
    unsigned long printStaleMs = cloud ? BAMBU_PRINT_STALE_TIMEOUT : BAMBU_STALE_TIMEOUT;
    unsigned long finishHoldMs = printStaleMs;
    if (dpSettings.finishDisplayMins > 0) {
      unsigned long configuredHoldMs = (unsigned long)dpSettings.finishDisplayMins * 60000UL;
      if (configuredHoldMs > finishHoldMs) finishHoldMs = configuredHoldMs;
    }

    if (s.lastPrintDataMs > 0 && millis() - s.lastPrintDataMs > finishHoldMs) {
      if (connAlive && isConnected && c.stalePushallSentMs == 0 && !cloudPushallThrottled && !recoveryCooldown) {
        MQTT_LOG("[%d] stale finish - sending recovery pushall", c.slotIndex);
        esp_task_wdt_reset();
        if (requestPushall(c, PUSHALL_RECOVERY_FINISH))
          c.stalePushallSentMs = millis();
      } else if (!connAlive &&
                 (c.stalePushallSentMs == 0 || millis() - c.stalePushallSentMs > 30000)) {
        MQTT_LOG("[%d] stale finish, connection dead - clearing cached state", c.slotIndex);
        clearLiveMetrics(s);
        setPrinterGcodeStateCanonical(s, GCODE_UNKNOWN);
        c.stalePushallSentMs = 0;
      }
    }
    // Don't reset stalePushallSentMs here - one recovery pushall per
    // FINISH is enough.  Resetting would create an infinite loop every
    // finishHoldMs minutes, risking cloud error 49.
    // stalePushallSentMs resets naturally when state leaves FINISH.

  // --- Idle cloud: connection freshness ---
  } else if (isConnected && cloud && s.gcodeStateId == GCODE_IDLE) {
    if (s.lastUpdate > 0 && millis() - s.lastUpdate > connStaleMs) {
      // Cloud broker connected but idle printer is unresponsive (powered off).
      MQTT_LOG("[%d] stale idle on cloud - clearing cached state", c.slotIndex);
      clearLiveMetrics(s);
      setPrinterGcodeStateCanonical(s, GCODE_UNKNOWN);
      esp_task_wdt_reset();
      if (requestPushall(c, PUSHALL_RECOVERY_IDLE))
        c.stalePushallSentMs = millis();
    } else {
      if (c.stalePushallSentMs > 0) c.lastRecoveryResolvedMs = millis();
      c.stalePushallSentMs = 0;
    }

  // --- UNKNOWN on cloud: printer just came online ---
  // After initial pushall gets no response (printer off), state stays UNKNOWN.
  // When the printer powers on and starts sending partial updates, connAlive
  // becomes true but we have no full status.  Send a bootstrap pushall.
  // First attempt is exempt from cloudPushallThrottled (it's at most the 2nd
  // pushall ever - no error 49 risk).  Retries respect the 2-min throttle.
  } else if (isConnected && cloud && s.gcodeStateId == GCODE_UNKNOWN && connAlive) {
    bool firstAttempt = (c.stalePushallSentMs == 0);
    if (firstAttempt || !cloudPushallThrottled) {
      MQTT_LOG("[%d] UNKNOWN + data flowing - sending %s pushall",
               c.slotIndex, firstAttempt ? "bootstrap" : "retry");
      esp_task_wdt_reset();
      if (requestPushall(c, PUSHALL_RECOVERY_IDLE))
        c.stalePushallSentMs = millis();
    }

  // --- Any other state ---
  } else if (s.gcodeStateId != GCODE_UNKNOWN) {
    c.stalePushallSentMs = 0;
  }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
bool isPrinterConfigured(uint8_t slot) {
  if (slot >= MAX_PRINTERS) return false;
  PrinterConfig& cfg = printers[slot].config;
  if (isCloudMode(cfg.mode))
    return strlen(cfg.serial) > 0 && strlen(cfg.cloudUserId) > 0;
  return strlen(cfg.ip) > 0 && strlen(cfg.serial) > 0;
}

bool isAnyPrinterConfigured() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (isPrinterConfigured(i)) return true;
  }
  return false;
}

uint8_t getActiveConnCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (conns[i].active) count++;
  }
  return count;
}

void initBambuMqtt() {
  Serial.println("MQTT: initBambuMqtt() — multi-printer");

  // Clean up any existing connections before reinitializing
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (conns[i].mqtt && conns[i].mqtt->connected()) {
      conns[i].mqtt->disconnect();
    }
    releaseClients(conns[i]);
    conns[i].active = false;
  }

  // First: do all cloud API work (userId extraction) before any MQTT connects.
  // Note: isPrinterConfigured() requires cloudUserId for cloud slots, so we check
  // the prerequisites (serial + cloud mode) directly to allow self-healing slots
  // that have a token but are missing cloudUserId.
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    PrinterConfig& cfg = printers[i].config;
    if (isCloudMode(cfg.mode) && strlen(cfg.serial) > 0 && strlen(cfg.cloudUserId) == 0) {
      Serial.printf("MQTT: [%d] cloud printer needs userId extraction\n", i);
      // userId extraction uses HTTPClient (TLS) — must complete before MQTT TLS
      char tokenBuf[1200];
      if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
        if (!cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId))) {
          Serial.printf("MQTT: [%d] JWT extract failed, trying API\n", i);
          cloudFetchUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId), cfg.region);
        }
        if (strlen(cfg.cloudUserId) > 0) {
          Serial.printf("MQTT: [%d] userId=%s\n", i, cfg.cloudUserId);
          savePrinterConfig(i);
        }
      }
    }
  }

  // Initialize connection slots
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    MqttConn& c = conns[i];
    c.slotIndex = i;
    memset(&c.diag, 0, sizeof(MqttDiag));
    c.lastReconnectAttempt = 0;
    c.lastPushallRequest = 0;
    c.pushallSeqId = 0;
    c.connectTime = 0;
    c.initialPushallSent = false;
    c.gotDataSinceConnect = false;
    c.consecutiveFails = 0;
    c.disconnectSince = 0;
    c.wasConnected = false;
    c.stalePushallSentMs = 0;
    c.lastRecoveryResolvedMs = 0;

    BambuState& s = printers[i].state;
    memset(&s, 0, sizeof(BambuState));
    setPrinterGcodeStateCanonical(s, GCODE_UNKNOWN);

    if (isPrinterConfigured(i)) {
      c.active = true;
      PrinterConfig& cfg = printers[i].config;
      Serial.printf("MQTT: [%d] '%s' serial=%s mode=%s — ready\n",
                    i, cfg.name, cfg.serial,
                    isCloudMode(cfg.mode) ? "CLOUD" : "LOCAL");
    } else {
      c.active = false;
      releaseClients(c);
      Serial.printf("MQTT: [%d] not configured, skipping\n", i);
    }
  }
}

static bool skipReconnectOnce = false;

void deferMqttReconnect() { skipReconnectOnce = true; }

void handleBambuMqtt() {
  bool skip = skipReconnectOnce;
  skipReconnectOnce = false;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!conns[i].active) continue;
    // When waking from sleep, skip reconnect attempts this iteration
    // so the display update is not blocked by TLS/TCP timeouts.
    // Already-connected sessions still get mqtt->loop().
    if (skip && !(conns[i].mqtt && conns[i].mqtt->connected())) continue;
    handleConn(conns[i]);
  }
}

void resetMqttBackoff() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    conns[i].consecutiveFails = 0;
    conns[i].lastReconnectAttempt = 0;  // force immediate retry
  }
  Serial.println("MQTT: backoff reset, reconnecting immediately");
}

void requestCloudRefresh(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  MqttConn& c = conns[slot];
  PrinterConfig& cfg = printers[slot].config;
  BambuState& s = printers[slot].state;
  if (!isCloudMode(cfg.mode)) return;
  if (!c.mqtt || !c.mqtt->connected()) return;
  if (s.gcodeStateId != GCODE_UNKNOWN) return;
  // Debounce: at most once per 5 seconds
  static unsigned long lastRefreshMs = 0;
  if (lastRefreshMs > 0 && millis() - lastRefreshMs < 5000) return;
  lastRefreshMs = millis();
  MQTT_LOG("[%d] manual cloud refresh (pushall)", c.slotIndex);
  esp_task_wdt_reset();
  requestPushall(c, PUSHALL_MANUAL);
}

void disconnectBambuMqtt() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    disconnectBambuMqtt(i);
  }
}

void disconnectBambuMqtt(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  MqttConn& c = conns[slot];
  if (c.mqtt && c.mqtt->connected()) {
    c.mqtt->disconnect();
  }
  releaseClients(c);
  c.active = false;
  printers[slot].state.connected = false;
}
