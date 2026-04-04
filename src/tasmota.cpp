#include "tasmota.h"
#include "settings.h"
#include "wifi_manager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TASMOTA_TIMEOUT_MS              1500      // HTTP timeout when plug is known-online
#define TASMOTA_TIMEOUT_FAST_MS          700      // shorter timeout once plug is confirmed offline
#define TASMOTA_STALE_MS               90000UL   // consider offline after 90s without data
#define TASMOTA_OFFLINE_RETRY_MS       10000UL   // retry quickly after confirmed offline
#define TASMOTA_DEFAULT_INTERVAL         10      // seconds, used when pollInterval not set
#define TASMOTA_FAILS_BEFORE_OFFLINE      3      // tolerate a few transient misses before hiding watts

static volatile float    g_watts              = -1.0f;
static volatile float    g_todayKwh           = -1.0f;
static volatile float    g_yesterdayKwh       = -1.0f;
static volatile float    g_printStartTodayKwh = -1.0f;
static volatile float    g_printUsedKwh       = -1.0f;
static volatile uint32_t g_lastUpdateMs       = 0;
static volatile bool     g_kwhChanged         = false;
static volatile bool     g_plugOffline        = false;
static volatile uint8_t  g_failCount          = 0;

static TaskHandle_t g_taskHandle = NULL;

static void markPollFailure() {
  if (g_failCount < 255) g_failCount++;
  if (g_failCount >= TASMOTA_FAILS_BEFORE_OFFLINE) {
    g_plugOffline = true;
  }
}

static void doPoll() {
  if (!tasmotaSettings.enabled || tasmotaSettings.ip[0] == '\0') return;

  char url[64];
  snprintf(url, sizeof(url), "http://%s/cm?cmnd=Status%%2010", tasmotaSettings.ip);

  HTTPClient http;
  http.setTimeout(g_plugOffline ? TASMOTA_TIMEOUT_FAST_MS : TASMOTA_TIMEOUT_MS);
  if (!http.begin(url)) {
    Serial.printf("[Tasmota] begin failed: %s\n", url);
    markPollFailure();
    return;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Tasmota] HTTP %d from %s\n", code, tasmotaSettings.ip);
    http.end();
    markPollFailure();
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Tasmota] JSON parse error: %s\n", err.c_str());
    markPollFailure();
    return;
  }

  JsonVariant energy = doc["StatusSNS"]["ENERGY"];
  if (energy.isNull()) energy = doc["ENERGY"];
  if (energy.isNull()) {
    Serial.println("[Tasmota] ENERGY object missing in response");
    markPollFailure();
    return;
  }

  JsonVariant power     = energy["Power"];
  JsonVariant today     = energy["Today"];
  JsonVariant yesterday = energy["Yesterday"];

  if (power.isNull()) {
    Serial.println("[Tasmota] Power field missing");
    markPollFailure();
    return;
  }

  float newWatts = power.as<float>();
  float newToday = today.isNull()     ? -1.0f : today.as<float>();
  float newYest  = yesterday.isNull() ? -1.0f : yesterday.as<float>();

  g_watts        = newWatts;
  g_lastUpdateMs = millis();
  g_failCount    = 0;
  g_plugOffline  = false;

  if (newToday >= 0.0f && newToday != (float)g_todayKwh) {
    g_todayKwh   = newToday;
    g_kwhChanged = true;
  }
  if (newYest >= 0.0f) g_yesterdayKwh = newYest;

  Serial.printf("[Tasmota] Power=%.0fW, Today=%.3fkWh, Yesterday=%.3fkWh\n",
                newWatts, newToday, newYest);
}

static void pollTask(void *) {
  while (true) {
    if (isWiFiConnected()) {
      doPoll();
    }
    uint32_t intervalMs = g_plugOffline
      ? TASMOTA_OFFLINE_RETRY_MS
      : (uint32_t)(tasmotaSettings.pollInterval > 0
                   ? tasmotaSettings.pollInterval
                   : TASMOTA_DEFAULT_INTERVAL) * 1000;
    vTaskDelay(pdMS_TO_TICKS(intervalMs));
  }
}

void tasmotaInit() {
  if (g_taskHandle != NULL) {
    vTaskSuspend(g_taskHandle);
    vTaskDelete(g_taskHandle);
    g_taskHandle = NULL;
  }

  g_watts              = -1.0f;
  g_todayKwh           = -1.0f;
  g_yesterdayKwh       = -1.0f;
  g_printStartTodayKwh = -1.0f;
  g_printUsedKwh       = -1.0f;
  g_lastUpdateMs       = 0;
  g_kwhChanged         = false;
  g_plugOffline        = false;
  g_failCount          = 0;

  if (tasmotaSettings.enabled && tasmotaSettings.ip[0] != '\0') {
    xTaskCreate(pollTask, "tasmota", 6144, NULL, 1, &g_taskHandle);
  }
}

float tasmotaGetWatts() {
  return g_watts;
}

bool tasmotaIsOnline() {
  if (!tasmotaSettings.enabled) return false;
  if (g_lastUpdateMs == 0) return false;
  return (millis() - g_lastUpdateMs) < TASMOTA_STALE_MS;
}

bool tasmotaIsActiveForSlot(uint8_t slot) {
  if (!tasmotaIsOnline()) return false;
  return (tasmotaSettings.assignedSlot == 255 || tasmotaSettings.assignedSlot == slot);
}

void tasmotaMarkPrintStart() {
  if (!tasmotaSettings.enabled) return;
  g_printStartTodayKwh = g_todayKwh;
  g_printUsedKwh       = -1.0f;
  Serial.printf("[Tasmota] Print start marked, Today=%.3fkWh\n", (float)g_printStartTodayKwh);
}

void tasmotaMarkPrintEnd() {
  if (!tasmotaSettings.enabled) return;
  float start = g_printStartTodayKwh;
  float today = g_todayKwh;
  float yest  = g_yesterdayKwh;

  if (start < 0.0f || today < 0.0f) {
    Serial.println("[Tasmota] Print end: no baseline, skipping");
    return;
  }

  if (today >= start) {
    g_printUsedKwh = today - start;
  } else if (yest >= 0.0f) {
    g_printUsedKwh = (yest - start) + today;
  }

  Serial.printf("[Tasmota] Print end marked, used=%.3fkWh\n", (float)g_printUsedKwh);
}

float tasmotaGetPrintKwhUsed() {
  return g_printUsedKwh;
}

bool tasmotaKwhChanged() {
  if (!g_kwhChanged) return false;
  g_kwhChanged = false;
  return true;
}
