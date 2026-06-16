#include "tasmota.h"
#include "settings.h"
#include "wifi_manager.h"
#include "bambu_mqtt.h"
#include "bambu_state.h"
#include "config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define TASMOTA_TIMEOUT_MS              1500
#define TASMOTA_TIMEOUT_FAST_MS          700
#define TASMOTA_STALE_MS               90000UL
#define TASMOTA_DEFAULT_INTERVAL_S       10
#define TASMOTA_FAILS_BEFORE_OFFLINE      3

// Auto-off temperature threshold (Celsius). Hardcoded per design ("Time +
// nozzle only"); bed temp intentionally not checked.
#define TASMOTA_AUTO_OFF_NOZZLE_MAX_C    50.0f

struct TasmotaPlugRuntime {
  float    watts;
  float    todayKwh;
  float    yesterdayKwh;
  float    totalKwh;
  float    printStartTotalKwh;
  float    printUsedKwh;
  uint32_t lastOkMs;        // millis() of last successful poll
  uint32_t nextPollMs;
  uint8_t  failCount;
  bool     plugOffline;
  bool     kwhChanged;
  bool     powerStateKnown;  // true when the plug reports relay state directly (Shelly)
  bool     powerOn;          // valid only when powerStateKnown
  uint32_t finishEnteredMs; // millis() when this plug's printer entered FINISH
  bool     autoOffFired;    // latch: true once Power Off has succeeded for this cycle
};

static TasmotaPlugRuntime g_rt[TASMOTA_PLUG_COUNT];
static TaskHandle_t g_taskHandle = NULL;

// ---------------------------------------------------------------------------
//  Persistence for last-print kWh (tsm{i}_lpk in NVS)
// ---------------------------------------------------------------------------
static void persistLastPrintKwh(uint8_t i, float v) {
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, false)) return;
  char key[12]; snprintf(key, sizeof(key), "tsm%u_lpk", (unsigned)i);
  p.putFloat(key, v);
  p.end();
}

static float loadLastPrintKwh(uint8_t i) {
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, true)) return -1.0f;
  char key[12]; snprintf(key, sizeof(key), "tsm%u_lpk", (unsigned)i);
  float v = p.getFloat(key, -1.0f);
  p.end();
  return v;
}

// ---------------------------------------------------------------------------
//  Plug <-> printer-slot mapping
// ---------------------------------------------------------------------------
uint8_t tasmotaPlugForPrinterSlot(uint8_t slot) {
#if TASMOTA_PLUG_COUNT == 1
  if (!tasmotaSettings[0].enabled) return 0xFF;
  uint8_t a = tasmotaSettings[0].assignedSlot;
  if (a == 255) return (slot == 0) ? 0 : 0xFF;       // "Any" -> canonical slot 0
  return (a == slot) ? 0 : 0xFF;
#else
  if (slot >= TASMOTA_PLUG_COUNT) return 0xFF;
  return tasmotaSettings[slot].enabled ? slot : 0xFF;
#endif
}

uint8_t tasmotaPrinterSlotForPlug(uint8_t plug) {
#if TASMOTA_PLUG_COUNT == 1
  uint8_t a = tasmotaSettings[0].assignedSlot;
  if (a != 255 && a >= MAX_ACTIVE_PRINTERS) return 0;
  return (a == 255) ? 0 : a;
#else
  return plug;
#endif
}

// "Visible" plug for a slot — LOOSE matching. Used by display helpers so the
// "Any" config still shows watts on both printer screens.
static uint8_t visiblePlugForSlot(uint8_t slot) {
#if TASMOTA_PLUG_COUNT == 1
  if (!tasmotaSettings[0].enabled) return 0xFF;
  uint8_t a = tasmotaSettings[0].assignedSlot;
  if (a == 255 || a == slot) return 0;
  return 0xFF;
#else
  if (slot >= TASMOTA_PLUG_COUNT) return 0xFF;
  return tasmotaSettings[slot].enabled ? slot : 0xFF;
#endif
}

// ---------------------------------------------------------------------------
//  Polling + Status 10 parser
// ---------------------------------------------------------------------------
static void markPollFailure(uint8_t i) {
  if (g_rt[i].failCount < 255) g_rt[i].failCount++;
  if (g_rt[i].failCount >= TASMOTA_FAILS_BEFORE_OFFLINE) {
    g_rt[i].plugOffline = true;
  }
}

// Common post-parse update shared by the Tasmota and Shelly pollers. Negative
// values mean "not reported" and leave the corresponding field untouched.
static void applyReadings(uint8_t i, float watts, float todayKwh,
                          float yestKwh, float totalKwh) {
  // Fallback: if Total is missing but Today is present, use Today as a degraded
  // same-day odometer so per-print math still works within a day.
  if (totalKwh < 0.0f && todayKwh >= 0.0f) totalKwh = todayKwh;

  g_rt[i].watts       = watts;
  g_rt[i].lastOkMs    = millis();
  g_rt[i].failCount   = 0;
  g_rt[i].plugOffline = false;

  if (todayKwh >= 0.0f && todayKwh != g_rt[i].todayKwh) {
    g_rt[i].todayKwh   = todayKwh;
    g_rt[i].kwhChanged = true;
  }
  if (yestKwh >= 0.0f)  g_rt[i].yesterdayKwh = yestKwh;
  if (totalKwh >= 0.0f) g_rt[i].totalKwh     = totalKwh;
}

// Tasmota: GET /cm?cmnd=Status 10 -> StatusSNS.ENERGY {Power, Today, Yesterday, Total}
static void pollTasmota(uint8_t i) {
  TasmotaSettings& s = tasmotaSettings[i];

  char url[64];
  snprintf(url, sizeof(url), "http://%s/cm?cmnd=Status%%2010", s.ip);

  HTTPClient http;
  http.setTimeout(g_rt[i].plugOffline ? TASMOTA_TIMEOUT_FAST_MS : TASMOTA_TIMEOUT_MS);
  if (!http.begin(url)) {
    Serial.printf("[Tasmota %u] begin failed: %s\n", i, url);
    markPollFailure(i);
    return;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Tasmota %u] HTTP %d from %s\n", i, code, s.ip);
    http.end();
    markPollFailure(i);
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Tasmota %u] JSON parse error: %s\n", i, err.c_str());
    markPollFailure(i);
    return;
  }

  JsonVariant energy = doc["StatusSNS"]["ENERGY"];
  if (energy.isNull()) energy = doc["ENERGY"];
  if (energy.isNull()) {
    Serial.printf("[Tasmota %u] ENERGY object missing in response\n", i);
    markPollFailure(i);
    return;
  }

  JsonVariant power     = energy["Power"];
  JsonVariant today     = energy["Today"];
  JsonVariant yesterday = energy["Yesterday"];
  JsonVariant total     = energy["Total"];

  if (power.isNull()) {
    Serial.printf("[Tasmota %u] Power field missing\n", i);
    markPollFailure(i);
    return;
  }

  float newWatts = power.as<float>();
  float newToday = today.isNull()     ? -1.0f : today.as<float>();
  float newYest  = yesterday.isNull() ? -1.0f : yesterday.as<float>();
  float newTotal = total.isNull()     ? -1.0f : total.as<float>();

  // Tasmota's Status 10 has no relay state — keep the watt-inference fallback
  // for the on/off buttons.
  g_rt[i].powerStateKnown = false;
  applyReadings(i, newWatts, newToday, newYest, newTotal);

  Serial.printf("[Tasmota %u] Power=%.0fW Today=%.3fkWh Total=%.3fkWh\n",
                i, newWatts, newToday, newTotal);
}

// Shelly Gen2/Gen3 (same RPC API): GET /rpc/Switch.GetStatus?id=0 -> {apower (W),
// aenergy.total (Wh), output (bool)}.
// Issue #115 reporter used /rpc/Shelly.GetStatus -> "switch:0".apower
// and /relay/0?turn=on|off; the Switch RPC is the narrower equivalent.
// Shelly reports no Today/Yesterday odometer, so those stay at their -1 sentinel.
// aenergy.total is a cumulative Wh counter (can be reset) — divide by 1000 for kWh.
static void pollShelly(uint8_t i) {
  TasmotaSettings& s = tasmotaSettings[i];

  char url[64];
  snprintf(url, sizeof(url), "http://%s/rpc/Switch.GetStatus?id=0", s.ip);

  HTTPClient http;
  http.setTimeout(g_rt[i].plugOffline ? TASMOTA_TIMEOUT_FAST_MS : TASMOTA_TIMEOUT_MS);
  if (!http.begin(url)) {
    Serial.printf("[Shelly %u] begin failed: %s\n", i, url);
    markPollFailure(i);
    return;
  }

  int code = http.GET();
  if (code != 200) {
    if (code == 401) {
      Serial.printf("[Shelly %u] HTTP 401 — password-protected Shelly not supported "
                    "(digest auth unavailable)\n", i);
    } else {
      Serial.printf("[Shelly %u] HTTP %d from %s\n", i, code, s.ip);
    }
    http.end();
    markPollFailure(i);
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Shelly %u] JSON parse error: %s\n", i, err.c_str());
    markPollFailure(i);
    return;
  }

  JsonVariant apower = doc["apower"];
  if (apower.isNull()) {
    Serial.printf("[Shelly %u] apower field missing\n", i);
    markPollFailure(i);
    return;
  }

  float newWatts = apower.as<float>();
  JsonVariant aetotal = doc["aenergy"]["total"];          // Wh
  float newTotal = aetotal.isNull() ? -1.0f : (aetotal.as<float>() / 1000.0f);

  JsonVariant output = doc["output"];
  g_rt[i].powerStateKnown = !output.isNull();
  g_rt[i].powerOn         = output.as<bool>();

  // Shelly has no Today/Yesterday — pass -1 so those stay unavailable.
  applyReadings(i, newWatts, -1.0f, -1.0f, newTotal);

  Serial.printf("[Shelly %u] Power=%.0fW Total=%.3fkWh Output=%d\n",
                i, newWatts, newTotal, g_rt[i].powerOn ? 1 : 0);
}

static void pollOne(uint8_t i) {
  TasmotaSettings& s = tasmotaSettings[i];
  if (!s.enabled || s.ip[0] == '\0') return;
  if (s.plugType == 1) pollShelly(i);
  else                 pollTasmota(i);
}

// ---------------------------------------------------------------------------
//  Auto power-off
// ---------------------------------------------------------------------------
static bool sendPowerCommand(uint8_t i, bool on) {
  TasmotaSettings& s = tasmotaSettings[i];
  if (s.ip[0] == '\0') return false;

  const char* tag = (s.plugType == 1) ? "Shelly" : "Tasmota";
  char url[64];
  if (s.plugType == 1) {
    // Shelly Gen2: GET /rpc/Switch.Set?id=0&on=true|false (issue #115 Gen1
    // equivalent was /relay/0?turn=on|off).
    snprintf(url, sizeof(url), "http://%s/rpc/Switch.Set?id=0&on=%s", s.ip, on ? "true" : "false");
  } else {
    snprintf(url, sizeof(url), "http://%s/cm?cmnd=Power%%20%s", s.ip, on ? "On" : "Off");
  }

  HTTPClient http;
  http.setTimeout(TASMOTA_TIMEOUT_MS);
  if (!http.begin(url)) return false;
  int code = http.GET();
  http.end();

  if (code == 200) {
    Serial.printf("[%s %u] Power %s sent successfully\n", tag, i, on ? "On" : "Off");
    return true;
  }
  Serial.printf("[%s %u] Power %s HTTP %d\n", tag, i, on ? "On" : "Off", code);
  return false;
}

static bool sendPowerOff(uint8_t i) { return sendPowerCommand(i, false); }

bool tasmotaSetPower(uint8_t plug, bool on) {
  if (plug >= TASMOTA_PLUG_COUNT) return false;
  if (!tasmotaSettings[plug].enabled) return false;
  bool ok = sendPowerCommand(plug, on);
  if (ok) {
    // Force a fresh poll soon so stats reflect the new state.
    g_rt[plug].nextPollMs = millis() + 500;
  }
  return ok;
}

static void evaluateAutoOff(uint8_t i) {
  TasmotaSettings& s = tasmotaSettings[i];
  if (!s.enabled) return;

  uint8_t slot = tasmotaPrinterSlotForPlug(i);
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  if (!isPrinterConfigured(slot)) return;

  BambuState& ps = printers[slot].state;
  uint32_t now = millis();

  bool mqttFresh = ps.connected
                && ps.lastPrintDataMs > 0
                && (now - ps.lastPrintDataMs) < BAMBU_STALE_TIMEOUT;

  if (ps.gcodeStateId == GCODE_FINISH && mqttFresh) {
    if (g_rt[i].finishEnteredMs == 0) {
      g_rt[i].finishEnteredMs = now;
      Serial.printf("[Tasmota %u] FINISH detected on slot %u, auto-off timer armed\n", i, slot);
    }
    // Door-open cancel: user is at the printer, abort this auto-off cycle.
    // Latch via autoOffFired so we don't re-evaluate; new print resets both.
    if (s.autoOffEnabled
        && s.autoOffCancelOnDoor
        && !g_rt[i].autoOffFired
        && ps.doorSensorPresent
        && ps.doorOpen) {
      Serial.printf("[Tasmota %u] Auto-off cancelled: door opened on slot %u\n", i, slot);
      g_rt[i].autoOffFired = true;
    }
    uint32_t elapsedMin = (now - g_rt[i].finishEnteredMs) / 60000UL;
    if (s.autoOffEnabled
        && !g_rt[i].autoOffFired
        && elapsedMin >= s.autoOffDelayMin
        && ps.nozzleTemp > 0.0f
        && ps.nozzleTemp < TASMOTA_AUTO_OFF_NOZZLE_MAX_C
        && !ps.ams.anyDrying
        && !g_rt[i].plugOffline
        && g_rt[i].lastOkMs > 0) {
      Serial.printf("[Tasmota %u] Auto-off conditions met (elapsed=%u min, nozzle=%.1fC)\n",
                    i, (unsigned)elapsedMin, ps.nozzleTemp);
      if (sendPowerOff(i)) {
        g_rt[i].autoOffFired = true;
      }
    }
  } else if (isPrintingGcodeState(ps.gcodeStateId)) {
    // New print -> reset timer and latch
    if (g_rt[i].finishEnteredMs != 0 || g_rt[i].autoOffFired) {
      Serial.printf("[Tasmota %u] New print detected on slot %u, auto-off reset\n", i, slot);
    }
    g_rt[i].finishEnteredMs = 0;
    g_rt[i].autoOffFired = false;
  }
  // else (!mqttFresh or other states): hold finishEnteredMs as-is, do not fire
}

// ---------------------------------------------------------------------------
//  FreeRTOS task — per-plug scheduling
// ---------------------------------------------------------------------------
static void pollTask(void*) {
  for (;;) {
    if (!isWiFiConnected()) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    uint32_t now = millis();
    uint32_t earliestNext = now + 30000;
    bool anyEnabled = false;
    for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; ++i) {
      if (!tasmotaSettings[i].enabled) continue;
      anyEnabled = true;
      if ((int32_t)(now - g_rt[i].nextPollMs) >= 0) {
        pollOne(i);
        evaluateAutoOff(i);
        uint8_t pi = tasmotaSettings[i].pollInterval;
        if (pi < 10) pi = 10;
        if (pi > 60) pi = 60;
        g_rt[i].nextPollMs = millis() + (uint32_t)pi * 1000UL;
      }
      if ((int32_t)(g_rt[i].nextPollMs - earliestNext) < 0) {
        earliestNext = g_rt[i].nextPollMs;
      }
    }
    if (!anyEnabled) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    int32_t sleepMs = (int32_t)(earliestNext - millis());
    if (sleepMs < 200) sleepMs = 200;
    if (sleepMs > 30000) sleepMs = 30000;
    vTaskDelay(pdMS_TO_TICKS(sleepMs));
  }
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
void tasmotaInit() {
  if (g_taskHandle != NULL) {
    vTaskSuspend(g_taskHandle);
    vTaskDelete(g_taskHandle);
    g_taskHandle = NULL;
  }

  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; ++i) {
    g_rt[i] = {};
    // Explicit "no data yet" sentinels. Zero would look like valid 0 W / 0 kWh
    // readings before the first poll, and could let an early markEnd() persist
    // a bogus 0-kWh print.
    g_rt[i].watts              = -1.0f;
    g_rt[i].todayKwh           = -1.0f;
    g_rt[i].yesterdayKwh       = -1.0f;
    g_rt[i].totalKwh           = -1.0f;
    g_rt[i].printStartTotalKwh = -1.0f;
    g_rt[i].printUsedKwh       = loadLastPrintKwh(i);   // -1.0f if never recorded
    g_rt[i].lastOkMs           = 0;
    g_rt[i].nextPollMs         = 0;
  }

  bool anyEnabled = false;
  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; ++i) {
    if (tasmotaSettings[i].enabled && tasmotaSettings[i].ip[0] != '\0') {
      anyEnabled = true; break;
    }
  }
  if (anyEnabled) {
    xTaskCreate(pollTask, "tasmota", 6144, NULL, 1, &g_taskHandle);
  }
}

// ---------------------------------------------------------------------------
//  Print start/end marks (per-plug)
// ---------------------------------------------------------------------------
void tasmotaMarkPrintStart(uint8_t i) {
  if (i >= TASMOTA_PLUG_COUNT) return;
  if (!tasmotaSettings[i].enabled) return;
  if (g_rt[i].totalKwh < 0.0f) {
    // No successful poll yet — refuse to baseline. markEnd will see start<0
    // and skip persisting a bogus 0-kWh print.
    g_rt[i].printStartTotalKwh = -1.0f;
    g_rt[i].printUsedKwh = -1.0f;
    Serial.printf("[Tasmota %u] Print start: no poll yet, baseline unknown\n", i);
    return;
  }
  g_rt[i].printStartTotalKwh = g_rt[i].totalKwh;
  g_rt[i].printUsedKwh = -1.0f;
  Serial.printf("[Tasmota %u] Print start marked, Total=%.3fkWh\n", i, g_rt[i].totalKwh);
}

void tasmotaMarkPrintEnd(uint8_t i) {
  if (i >= TASMOTA_PLUG_COUNT) return;
  if (!tasmotaSettings[i].enabled) return;
  float start = g_rt[i].printStartTotalKwh;
  float total = g_rt[i].totalKwh;
  if (start < 0.0f || total < 0.0f) {
    Serial.printf("[Tasmota %u] Print end: no baseline, keeping previous lpk\n", i);
    return;
  }
  if (total >= start) {
    g_rt[i].printUsedKwh = total - start;
    g_rt[i].kwhChanged   = true;
    persistLastPrintKwh(i, g_rt[i].printUsedKwh);
    Serial.printf("[Tasmota %u] Print end marked, used=%.3fkWh\n", i, g_rt[i].printUsedKwh);
  }
}

// ---------------------------------------------------------------------------
//  Display-side accessors (slot-keyed)
// ---------------------------------------------------------------------------
bool tasmotaIsActiveForSlot(uint8_t slot) {
  uint8_t p = visiblePlugForSlot(slot);
  if (p == 0xFF) return false;
  if (g_rt[p].lastOkMs == 0) return false;
  return (millis() - g_rt[p].lastOkMs) < TASMOTA_STALE_MS;
}

bool tasmotaConfiguredForSlot(uint8_t slot) {
  return visiblePlugForSlot(slot) != 0xFF;
}

float tasmotaGetWattsForSlot(uint8_t slot) {
  uint8_t p = visiblePlugForSlot(slot);
  if (p == 0xFF || g_rt[p].lastOkMs == 0) return 0.0f;
  return g_rt[p].watts;
}

uint8_t tasmotaDisplayModeForSlot(uint8_t slot) {
  uint8_t p = visiblePlugForSlot(slot);
  if (p == 0xFF) return 0;
  return tasmotaSettings[p].displayMode;
}

float tasmotaGetPrintKwhUsedForSlot(uint8_t slot) {
  uint8_t p = tasmotaPlugForPrinterSlot(slot);  // STRICT
  if (p == 0xFF) return -1.0f;
  return g_rt[p].printUsedKwh;
}

float tasmotaTariffForSlot(uint8_t /*slot*/) {
  // Tariff is global (one electricity provider, one rate). Slot parameter
  // kept for callsite compatibility but no longer routes through the plug
  // mapping — the kWh check upstream already gates whether cost is drawn.
  return tasmotaTariffPerKwh;
}

const char* tasmotaCurrencySymbol() {
  return tasmotaCurrency;
}

bool tasmotaKwhChangedForSlot(uint8_t slot) {
  uint8_t p = tasmotaPlugForPrinterSlot(slot);  // STRICT
  if (p == 0xFF) return false;
  if (!g_rt[p].kwhChanged) return false;
  g_rt[p].kwhChanged = false;
  return true;
}

void tasmotaGetStats(uint8_t plug, TasmotaPlugStatsView* out) {
  if (!out) return;
  if (plug >= TASMOTA_PLUG_COUNT) {
    out->online = false;
    out->watts = -1.0f;
    out->todayKwh = -1.0f;
    out->totalKwh = -1.0f;
    out->printUsedKwh = -1.0f;
    out->powerStateKnown = false;
    out->powerOn = false;
    return;
  }
  bool online = tasmotaSettings[plug].enabled
             && g_rt[plug].lastOkMs > 0
             && (millis() - g_rt[plug].lastOkMs) < TASMOTA_STALE_MS;
  out->online          = online;
  out->watts           = g_rt[plug].watts;
  out->todayKwh        = g_rt[plug].todayKwh;
  out->totalKwh        = g_rt[plug].totalKwh;
  out->printUsedKwh    = g_rt[plug].printUsedKwh;
  out->powerStateKnown = online && g_rt[plug].powerStateKnown;
  out->powerOn         = g_rt[plug].powerOn;
}
