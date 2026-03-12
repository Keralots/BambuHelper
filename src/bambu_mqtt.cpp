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

// MQTT client objects — lazily allocated on first connect
static WiFiClientSecure* tlsClient = nullptr;
static PubSubClient* mqttClient = nullptr;
static bool initialized = false;
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastPushallRequest = 0;
static uint32_t pushallSeqId = 0;
static unsigned long connectTime = 0;
static bool initialPushallSent = false;

bool mqttDebugLog = false;  // toggled via web UI

static MqttDiag diag = {};

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

const MqttDiag& getMqttDiag() {
  diag.freeHeap = ESP.getFreeHeap();
  return diag;
}

// ---------------------------------------------------------------------------
//  Lazy allocation of TLS + MQTT clients
// ---------------------------------------------------------------------------
static bool ensureClients() {
  if (tlsClient && mqttClient) return true;

  uint32_t freeHeap = ESP.getFreeHeap();
  MQTT_LOG("ensureClients() heap=%u min=%u", freeHeap, BAMBU_MIN_FREE_HEAP);
  if (freeHeap < BAMBU_MIN_FREE_HEAP) {
    MQTT_LOG("NOT ENOUGH HEAP!");
    return false;
  }

  if (!tlsClient) {
    tlsClient = new (std::nothrow) WiFiClientSecure();
    if (!tlsClient) {
      MQTT_LOG("Failed to allocate WiFiClientSecure!");
      return false;
    }
    MQTT_LOG("WiFiClientSecure allocated OK");
  }
  tlsClient->setInsecure();  // self-signed cert on local network
  tlsClient->setTimeout(5);  // 5s TLS timeout to avoid blocking loop too long

  if (!mqttClient) {
    mqttClient = new (std::nothrow) PubSubClient(*tlsClient);
    if (!mqttClient) {
      MQTT_LOG("Failed to allocate PubSubClient!");
      delete tlsClient;
      tlsClient = nullptr;
      return false;
    }
    MQTT_LOG("PubSubClient allocated OK");
  }

  PrinterConfig& cfg = activePrinter().config;
  if (isCloudMode(cfg.mode)) {
    MQTT_LOG("setServer(%s, %d) [CLOUD]", BAMBU_CLOUD_BROKER, BAMBU_PORT);
    mqttClient->setServer(BAMBU_CLOUD_BROKER, BAMBU_PORT);
  } else {
    MQTT_LOG("setServer(%s, %d) [LOCAL]", cfg.ip, BAMBU_PORT);
    mqttClient->setServer(cfg.ip, BAMBU_PORT);
  }
  mqttClient->setBufferSize(BAMBU_BUFFER_SIZE);
  mqttClient->setCallback(mqttCallback);
  mqttClient->setKeepAlive(BAMBU_KEEPALIVE);

  initialized = true;
  return true;
}

// ---------------------------------------------------------------------------
//  Request pushall
// ---------------------------------------------------------------------------
static void requestPushall() {
  if (!mqttClient) return;

  PrinterConfig& cfg = activePrinter().config;
  char topic[64];
  snprintf(topic, sizeof(topic), "device/%s/request", cfg.serial);

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"pushing\":{\"sequence_id\":\"%u\",\"command\":\"pushall\"}}",
           pushallSeqId++);

  mqttClient->publish(topic, payload);
  lastPushallRequest = millis();
}

// ---------------------------------------------------------------------------
//  Reconnect
// ---------------------------------------------------------------------------
static void reconnect() {
  PrinterConfig& cfg = activePrinter().config;

  // Validate config based on mode
  if (!cfg.enabled) return;
  if (cfg.mode == CONN_LOCAL && strlen(cfg.ip) == 0) return;
  if (isCloudMode(cfg.mode) && (strlen(cfg.cloudUserId) == 0 || strlen(cfg.serial) == 0)) return;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < BAMBU_RECONNECT_INTERVAL) return;

  diag.attempts++;
  diag.lastAttemptMs = now;
  lastReconnectAttempt = now;

  MQTT_LOG("=== reconnect attempt #%u [%s] ===", diag.attempts,
           isCloudMode(cfg.mode) ? "CLOUD" : "LOCAL");
  MQTT_LOG("serial=%s heap=%u WiFi=%d", cfg.serial, ESP.getFreeHeap(), WiFi.status());

  if (!ensureClients()) {
    MQTT_LOG("ensureClients() FAILED");
    diag.lastRc = -2;
    return;
  }
  if (mqttClient->connected()) return;

  // TCP reachability test (local mode only — cloud broker is on the internet)
  if (cfg.mode == CONN_LOCAL) {
    WiFiClient tcp;
    tcp.setTimeout(3);
    MQTT_LOG("TCP test to %s:%d...", cfg.ip, BAMBU_PORT);
    unsigned long tcpT0 = millis();
    diag.tcpOk = tcp.connect(cfg.ip, BAMBU_PORT);
    MQTT_LOG("TCP test %s in %lums", diag.tcpOk ? "OK" : "FAILED", millis() - tcpT0);
    tcp.stop();
    if (!diag.tcpOk) {
      MQTT_LOG("Printer not reachable on network!");
      diag.lastRc = -2;
      return;
    }
  } else {
    diag.tcpOk = true;  // skip for cloud
  }

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "bambu_%08x",
           (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF));

  unsigned long t0 = millis();
  esp_task_wdt_reset();

  bool connected = false;
  if (isCloudMode(cfg.mode)) {
    // Cloud: load token from NVS, use cloudUserId as username
    char tokenBuf[1200];
    if (!loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      MQTT_LOG("No cloud token in NVS!");
      diag.lastRc = -2;
      return;
    }
    MQTT_LOG("Calling connect(id=%s, user=%s) [CLOUD]...", clientId, cfg.cloudUserId);
    connected = mqttClient->connect(clientId, cfg.cloudUserId, tokenBuf);
  } else {
    // Local: use bblp + access code
    MQTT_LOG("Calling connect(id=%s, user=%s) [LOCAL]...", clientId, BAMBU_USERNAME);
    connected = mqttClient->connect(clientId, BAMBU_USERNAME, cfg.accessCode);
  }

  if (connected) {
    char topic[64];
    snprintf(topic, sizeof(topic), "device/%s/report", cfg.serial);
    mqttClient->subscribe(topic);

    activePrinter().state.connected = true;
    connectTime = millis();
    initialPushallSent = false;
    diag.lastRc = 0;
    diag.connectDurMs = millis() - t0;
    MQTT_LOG("CONNECTED in %lums, subscribed to %s", diag.connectDurMs, topic);
  } else {
    activePrinter().state.connected = false;
    diag.lastRc = mqttClient->state();
    diag.connectDurMs = millis() - t0;
    MQTT_LOG("CONNECT FAILED rc=%d (%s) took %lums",
             diag.lastRc, mqttRcToString(diag.lastRc), diag.connectDurMs);
    if (isCloudMode(cfg.mode) && (diag.lastRc == 4 || diag.lastRc == 5)) {
      MQTT_LOG("Cloud token may be expired — re-login via web UI");
    }
  }
}

// ---------------------------------------------------------------------------
//  MQTT callback — delta merge
// ---------------------------------------------------------------------------
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  esp_task_wdt_reset();
  diag.messagesRx++;
  MQTT_LOG("callback #%u topic=%s len=%u", diag.messagesRx, topic, length);

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

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length,
                                              DeserializationOption::Filter(filter));
  if (err) {
    MQTT_LOG("JSON parse error: %s", err.c_str());
    return;
  }

  JsonObject print = doc["print"];
  if (print.isNull()) {
    MQTT_LOG("no 'print' key (info/system msg)");
    return;
  }

  BambuState& s = activePrinter().state;
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
    s.printing = (strcmp(state, "RUNNING") == 0 ||
                  strcmp(state, "PAUSE") == 0 ||
                  strcmp(state, "PREPARE") == 0);
  }

  if (print["mc_percent"].is<int>())
    s.progress = print["mc_percent"].as<int>();

  if (print["mc_remaining_time"].is<int>())
    s.remainingMinutes = print["mc_remaining_time"].as<int>();

  // Temperature fields (can arrive as int or float)
  if (print["nozzle_temper"].is<float>())
    s.nozzleTemp = print["nozzle_temper"].as<float>();
  else if (print["nozzle_temper"].is<int>())
    s.nozzleTemp = print["nozzle_temper"].as<int>();

  if (print["nozzle_target_temper"].is<float>())
    s.nozzleTarget = print["nozzle_target_temper"].as<float>();
  else if (print["nozzle_target_temper"].is<int>())
    s.nozzleTarget = print["nozzle_target_temper"].as<int>();

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
    return -1;  // not present
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

  // WiFi signal (Bambu sends as string like "-45dBm" or as int)
  if (print["wifi_signal"].is<const char*>())
    s.wifiSignal = atoi(print["wifi_signal"].as<const char*>());
  else if (print["wifi_signal"].is<int>())
    s.wifiSignal = print["wifi_signal"].as<int>();

  // Speed level (1-4)
  if (print["spd_lvl"].is<int>())
    s.speedLevel = print["spd_lvl"].as<int>();

  s.lastUpdate = millis();
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
void initBambuMqtt() {
  PrinterConfig& cfg = activePrinter().config;
  Serial.println("MQTT: initBambuMqtt()");
  Serial.printf("MQTT: enabled=%d ip='%s' serial='%s'\n",
                cfg.enabled, cfg.ip, cfg.serial);

  memset(&diag, 0, sizeof(diag));

  BambuState& s = activePrinter().state;
  memset(&s, 0, sizeof(BambuState));
  s.connected = false;
  s.printing = false;
  strcpy(s.gcodeState, "UNKNOWN");

  bool notConfigured = isCloudMode(cfg.mode)
    ? (strlen(cfg.cloudUserId) == 0 || strlen(cfg.serial) == 0)
    : (strlen(cfg.ip) == 0);

  if (!cfg.enabled || notConfigured) {
    Serial.println("MQTT: Printer not configured, skipping");
    if (mqttClient) { delete mqttClient; mqttClient = nullptr; }
    if (tlsClient) { delete tlsClient; tlsClient = nullptr; }
    initialized = false;
    return;
  }

  initialPushallSent = false;
  connectTime = 0;
  lastReconnectAttempt = 0;
  Serial.println("MQTT: Ready, will connect on next handle()");
}

void handleBambuMqtt() {
  PrinterConfig& cfg = activePrinter().config;
  if (!cfg.enabled) return;

  BambuState& s = activePrinter().state;

  if (!mqttClient || !mqttClient->connected()) {
    s.connected = false;
    reconnect();
  } else {
    mqttClient->loop();

    // Pushall only for local mode (cloud broker pushes status automatically)
    if (cfg.mode == CONN_LOCAL) {
      // Delayed initial pushall
      if (!initialPushallSent && connectTime > 0 &&
          millis() - connectTime > BAMBU_PUSHALL_INITIAL_DELAY) {
        esp_task_wdt_reset();
        requestPushall();
        initialPushallSent = true;
      }

      // Retry pushall if no data received within 10s of sending it
      if (initialPushallSent && diag.messagesRx == 0 &&
          millis() - lastPushallRequest > 10000) {
        MQTT_LOG("No data after pushall, retrying...");
        esp_task_wdt_reset();
        requestPushall();
      }

      // Periodic pushall
      if (initialPushallSent && diag.messagesRx > 0 &&
          millis() - lastPushallRequest > BAMBU_PUSHALL_INTERVAL) {
        esp_task_wdt_reset();
        requestPushall();
      }
    }
  }

  // Stale timeout
  if (s.lastUpdate > 0 && millis() - s.lastUpdate > BAMBU_STALE_TIMEOUT) {
    if (s.printing) {
      s.printing = false;
    }
  }
}

bool isPrinterConfigured() {
  PrinterConfig& cfg = activePrinter().config;
  if (!cfg.enabled) return false;
  if (isCloudMode(cfg.mode))
    return strlen(cfg.serial) > 0 && strlen(cfg.cloudUserId) > 0;
  return strlen(cfg.ip) > 0;
}

void disconnectBambuMqtt() {
  if (mqttClient && mqttClient->connected()) {
    mqttClient->disconnect();
  }
  activePrinter().state.connected = false;
}
