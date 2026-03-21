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
  unsigned long idleSince;
  bool active;           // connection slot in use
  uint16_t consecutiveFails;  // for exponential backoff
};

static MqttConn conns[MAX_ACTIVE_PRINTERS];

bool mqttDebugLog = false;

static void mqttCallback(char* topic, byte* payload, unsigned int length);

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
  c.tls->setInsecure();
  c.tls->setTimeout(5);

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
  c.mqtt->setBufferSize(BAMBU_BUFFER_SIZE);
  c.mqtt->setCallback(mqttCallback);
  c.mqtt->setKeepAlive(BAMBU_KEEPALIVE);

  return true;
}

// ---------------------------------------------------------------------------
//  Request pushall for one connection
// ---------------------------------------------------------------------------
static void requestPushall(MqttConn& c) {
  if (!c.mqtt) return;

  PrinterConfig& cfg = printers[c.slotIndex].config;
  char topic[64];
  snprintf(topic, sizeof(topic), "device/%s/request", cfg.serial);

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"pushing\":{\"sequence_id\":\"%u\",\"command\":\"pushall\"}}",
           c.pushallSeqId++);

  c.mqtt->publish(topic, payload);
  c.lastPushallRequest = millis();
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

// ---------------------------------------------------------------------------
//  Parse MQTT payload into a BambuState (extracted for routing)
// ---------------------------------------------------------------------------
static void parseMqttPayload(byte* payload, unsigned int length,
                             BambuState& s, MqttDiag& diag, unsigned long& idleSince) {
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
  pf["subtask_name"] = true;
  pf["layer_num"] = true;
  pf["total_layer_num"] = true;
  pf["cooling_fan_speed"] = true;
  pf["big_fan1_speed"] = true;
  pf["big_fan2_speed"] = true;
  pf["heatbreak_fan_speed"] = true;
  pf["wifi_signal"] = true;
  pf["spd_lvl"] = true;
  // Note: H2D/H2C extruder data is parsed separately from raw payload (see below)

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length,
                                              DeserializationOption::Filter(filter));
  if (err) {
    MQTT_LOG("JSON parse error: %s", err.c_str());
    return;
  }

  // H2D/H2C dual nozzle: parse extruder directly from raw payload
  // (bypasses ArduinoJson filter which strips it due to deep nesting)
  const char* extPos = (const char*)memmem(payload, length, "\"extruder\":", 11);
  if (extPos) {
    const char* objStart = extPos + 11;  // skip past "extruder":
    // Skip whitespace
    while (*objStart == ' ' || *objStart == '\t') objStart++;
    if (*objStart == '{') {
      JsonDocument extDoc;
      if (!deserializeJson(extDoc, objStart)) {
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
            if (id == s.activeNozzle && entry["temp"].is<unsigned int>()) {
              uint32_t packed = entry["temp"].as<unsigned int>();
              s.nozzleTemp   = (float)(packed & 0xFFFF);
              s.nozzleTarget = (float)(packed >> 16);
              s.lastUpdate = millis();
              MQTT_LOG("dual nozzle=%d temp=%.0f target=%.0f", s.activeNozzle, s.nozzleTemp, s.nozzleTarget);
              break;
            }
          }
        }
      }
    }
  }

  JsonObject print = doc["print"];
  if (print.isNull()) {
    return;  // no print data in this message
  }

  if (mqttDebugLog && print["gcode_state"].is<const char*>()) {
    Serial.printf("MQTT: state=%s progress=%d nozzle=%.0f bed=%.0f\n",
                  print["gcode_state"].as<const char*>(),
                  print["mc_percent"] | -1,
                  print["nozzle_temper"] | -1.0f,
                  print["bed_temper"] | -1.0f);
  }

  // Delta merge: only update fields present in this message

  if (print["gcode_state"].is<const char*>()) {
    const char* state = print["gcode_state"];
    strncpy(s.gcodeState, state, 15);
    s.gcodeState[15] = '\0';
    bool wasActive = s.printing;
    s.printing = (strcmp(state, "RUNNING") == 0 ||
                  strcmp(state, "PAUSE") == 0 ||
                  strcmp(state, "PREPARE") == 0);
    if (s.printing) {
      idleSince = 0;
    } else if (wasActive || idleSince == 0) {
      idleSince = millis();
    }
  }

  if (print["mc_percent"].is<int>())
    s.progress = print["mc_percent"].as<int>();

  if (print["mc_remaining_time"].is<int>())
    s.remainingMinutes = print["mc_remaining_time"].as<int>();

  // For dual-nozzle (H2D/H2C): nozzle_temper is the INACTIVE nozzle — skip it
  // Active nozzle temp comes from extruder.info[] parsed below
  if (!s.dualNozzle) {
    if (print["nozzle_temper"].is<float>())
      s.nozzleTemp = print["nozzle_temper"].as<float>();
    else if (print["nozzle_temper"].is<int>())
      s.nozzleTemp = print["nozzle_temper"].as<int>();

    if (print["nozzle_target_temper"].is<float>())
      s.nozzleTarget = print["nozzle_target_temper"].as<float>();
    else if (print["nozzle_target_temper"].is<int>())
      s.nozzleTarget = print["nozzle_target_temper"].as<int>();
  }

  if (print["bed_temper"].is<float>())
    s.bedTemp = print["bed_temper"].as<float>();
  else if (print["bed_temper"].is<int>())
    s.bedTemp = print["bed_temper"].as<int>();

  if (print["bed_target_temper"].is<float>())
    s.bedTarget = print["bed_target_temper"].as<float>();
  else if (print["bed_target_temper"].is<int>())
    s.bedTarget = print["bed_target_temper"].as<int>();

  if (print["chamber_temper"].is<float>())
    s.chamberTemp = print["chamber_temper"].as<float>();
  else if (print["chamber_temper"].is<int>())
    s.chamberTemp = print["chamber_temper"].as<int>();

  if (print["subtask_name"].is<const char*>()) {
    const char* name = print["subtask_name"];
    strncpy(s.subtaskName, name, sizeof(s.subtaskName) - 1);
    s.subtaskName[sizeof(s.subtaskName) - 1] = '\0';
  }

  if (print["layer_num"].is<int>())
    s.layerNum = print["layer_num"].as<int>();

  if (print["total_layer_num"].is<int>())
    s.totalLayers = print["total_layer_num"].as<int>();

  // Fan speeds (Bambu sends 0-15, may arrive as int or string)
  auto parseFan = [](JsonVariant v) -> int {
    if (v.is<int>()) return v.as<int>();
    if (v.is<const char*>()) return atoi(v.as<const char*>());
    return -1;
  };

  int fanVal;
  fanVal = parseFan(print["cooling_fan_speed"]);
  if (fanVal >= 0) s.coolingFanPct = (fanVal * 100) / 15;

  fanVal = parseFan(print["big_fan1_speed"]);
  if (fanVal >= 0) s.auxFanPct = (fanVal * 100) / 15;

  fanVal = parseFan(print["big_fan2_speed"]);
  if (fanVal >= 0) s.chamberFanPct = (fanVal * 100) / 15;

  fanVal = parseFan(print["heatbreak_fan_speed"]);
  if (fanVal >= 0) s.heatbreakFanPct = (fanVal * 100) / 15;

  if (print["wifi_signal"].is<const char*>())
    s.wifiSignal = atoi(print["wifi_signal"].as<const char*>());
  else if (print["wifi_signal"].is<int>())
    s.wifiSignal = print["wifi_signal"].as<int>();

  if (print["spd_lvl"].is<int>())
    s.speedLevel = print["spd_lvl"].as<int>();

  s.lastUpdate = millis();
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

  BambuState& s = printers[c->slotIndex].state;
  parseMqttPayload(payload, length, s, c->diag, c->idleSince);
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
  unsigned long interval = BAMBU_RECONNECT_INTERVAL;
  if (c.consecutiveFails >= BAMBU_BACKOFF_PHASE1 + BAMBU_BACKOFF_PHASE2) {
    interval = BAMBU_BACKOFF_PHASE3_MS;
  } else if (c.consecutiveFails >= BAMBU_BACKOFF_PHASE1) {
    interval = BAMBU_BACKOFF_PHASE2_MS;
  }

  if (now - c.lastReconnectAttempt < interval) return;

  c.diag.attempts++;
  c.diag.lastAttemptMs = now;
  c.lastReconnectAttempt = now;

  MQTT_LOG("[%d] === reconnect attempt #%u (fails=%u, interval=%lus) [%s] ===",
           c.slotIndex, c.diag.attempts, c.consecutiveFails, interval / 1000,
           isCloudMode(cfg.mode) ? "CLOUD" : "LOCAL");
  MQTT_LOG("[%d] serial=%s heap=%u WiFi=%d", c.slotIndex, cfg.serial, ESP.getFreeHeap(), WiFi.status());

  if (!ensureClients(c)) {
    MQTT_LOG("[%d] ensureClients() FAILED", c.slotIndex);
    c.diag.lastRc = -2;
    return;
  }
  if (c.mqtt->connected()) return;

  // TCP reachability test (local mode only)
  if (cfg.mode == CONN_LOCAL) {
    WiFiClient tcp;
    tcp.setTimeout(3);
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

  // Unique client ID per connection slot
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "bambu_%08x_%d",
           (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), c.slotIndex);

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
  } else {
    MQTT_LOG("[%d] connect(id=%s, user=%s) [LOCAL]...", c.slotIndex, clientId, BAMBU_USERNAME);
    connected = c.mqtt->connect(clientId, BAMBU_USERNAME, cfg.accessCode);
  }

  if (connected) {
    char topic[64];
    snprintf(topic, sizeof(topic), "device/%s/report", cfg.serial);
    c.mqtt->subscribe(topic);

    printers[c.slotIndex].state.connected = true;
    c.connectTime = millis();
    c.initialPushallSent = false;
    c.consecutiveFails = 0;  // reset backoff on success
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

  if (!c.mqtt || !c.mqtt->connected()) {
    s.connected = false;
    reconnectConn(c);
  } else {
    c.mqtt->loop();

    bool cloudIdle = isCloudMode(cfg.mode) && c.idleSince > 0;
    bool cloudPassive = cloudIdle && (millis() - c.idleSince > 600000UL);

    unsigned long pushallInterval = isCloudMode(cfg.mode)
      ? BAMBU_PUSHALL_INTERVAL * 4
      : BAMBU_PUSHALL_INTERVAL;
    if (cloudIdle && !cloudPassive) {
      unsigned long idleMs = millis() - c.idleSince;
      if (idleMs > 300000UL) pushallInterval *= 2;
    }

    if (!c.initialPushallSent && c.connectTime > 0 &&
        millis() - c.connectTime > BAMBU_PUSHALL_INITIAL_DELAY) {
      esp_task_wdt_reset();
      requestPushall(c);
      c.initialPushallSent = true;
    }

    if (c.initialPushallSent && c.diag.messagesRx == 0 &&
        millis() - c.lastPushallRequest > 10000) {
      MQTT_LOG("[%d] No data after pushall, retrying...", c.slotIndex);
      esp_task_wdt_reset();
      requestPushall(c);
    }

    if (!cloudPassive &&
        c.initialPushallSent && c.diag.messagesRx > 0 &&
        millis() - c.lastPushallRequest > pushallInterval) {
      esp_task_wdt_reset();
      requestPushall(c);
    }
  }

  unsigned long staleMs = isCloudMode(cfg.mode) ? BAMBU_STALE_TIMEOUT * 5 : BAMBU_STALE_TIMEOUT;
  if (s.lastUpdate > 0 && millis() - s.lastUpdate > staleMs) {
    if (s.printing) {
      s.printing = false;
    }
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

  // First: do all cloud API work (userId extraction) before any MQTT connects
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    PrinterConfig& cfg = printers[i].config;
    if (isPrinterConfigured(i) && isCloudMode(cfg.mode) && strlen(cfg.cloudUserId) == 0) {
      Serial.printf("MQTT: [%d] cloud printer needs userId extraction\n", i);
      // userId extraction uses HTTPClient (TLS) — must complete before MQTT TLS
      char tokenBuf[1200];
      if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
        cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId));
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
    c.idleSince = 0;
    c.consecutiveFails = 0;

    BambuState& s = printers[i].state;
    memset(&s, 0, sizeof(BambuState));
    strcpy(s.gcodeState, "UNKNOWN");

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

void handleBambuMqtt() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!conns[i].active) continue;
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
