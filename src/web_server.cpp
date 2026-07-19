#include "web_server.h"
#include "web_template.h"
#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "ssdp_discovery.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "config.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "timezones.h"
#include "tasmota.h"
#include "clock_mode.h"
#include "clock_pong.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "esp_ota_ops.h"
#ifdef ENABLE_OTA_AUTO
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

// Global WebServer instance. Defined here, referenced from src/web_template.cpp
// via `extern WebServer server;` in include/web_template.h.
WebServer server(80);

// ---------------------------------------------------------------------------
//  Deferred restart — avoids blocking delay() before ESP.restart()
// ---------------------------------------------------------------------------
static unsigned long pendingRestartAt = 0;

static void scheduleRestart(unsigned long delayMs = 1000) {
  pendingRestartAt = millis() + delayMs;
}

// ---------------------------------------------------------------------------
//  PROGMEM page strings (PAGE_HTML, PAGE_AP_HTML) and the template streamer
//  (streamTemplate, resolvePlaceholder) now live in include/web_pages.h and
//  src/web_template.cpp. Route handlers below talk to them through the
//  surface declared in include/web_template.h.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Helper: read gauge colors from form
// ---------------------------------------------------------------------------
static void readGaugeColorsFromForm(const char* prefix, GaugeColors& gc) {
  char key[8];
  snprintf(key, sizeof(key), "%s_a", prefix);
  if (server.hasArg(key)) gc.arc = htmlToRgb565(server.arg(key).c_str());
  snprintf(key, sizeof(key), "%s_l", prefix);
  if (server.hasArg(key)) gc.label = htmlToRgb565(server.arg(key).c_str());
  snprintf(key, sizeof(key), "%s_v", prefix);
  if (server.hasArg(key)) gc.value = htmlToRgb565(server.arg(key).c_str());
}

// Read one custom gauge label from the form, sanitizing while copying (so an
// overlong raw value can't drop valid chars after truncation).
static void readGaugeLabelFromForm(const char* arg, char* dst, size_t len) {
  if (server.hasArg(arg)) sanitizeGaugeLabel(server.arg(arg).c_str(), dst, len);
}

// ---------------------------------------------------------------------------
//  Read display settings from form args
// ---------------------------------------------------------------------------
static void readDisplayFromForm() {
  if (server.hasArg("bright")) brightness = server.arg("bright").toInt();
  // Night mode
  dpSettings.nightModeEnabled = server.hasArg("nighten");
  if (server.hasArg("nstart")) dpSettings.nightStartHour = server.arg("nstart").toInt();
  if (server.hasArg("nend"))   dpSettings.nightEndHour = server.arg("nend").toInt();
  if (server.hasArg("nbright")) dpSettings.nightBrightness = server.arg("nbright").toInt();
  if (server.hasArg("ssbright")) dpSettings.screensaverBrightness = server.arg("ssbright").toInt();
  // Apply brightness after all brightness-related values are parsed
  setBacklight(getEffectiveBrightness());

  if (server.hasArg("rotation")) {
    uint8_t rot = server.arg("rotation").toInt();
    if (rot <= 3) dispSettings.rotation = rot;
  }
  if (server.hasArg("clr_bg"))    dispSettings.bgColor = htmlToRgb565(server.arg("clr_bg").c_str());
  if (server.hasArg("clr_track")) dispSettings.trackColor = htmlToRgb565(server.arg("clr_track").c_str());
  if (server.hasArg("clr_pbar"))  dispSettings.progressBarColor = htmlToRgb565(server.arg("clr_pbar").c_str());
  if (server.hasArg("clk_time"))  dispSettings.clockTimeColor = htmlToRgb565(server.arg("clk_time").c_str());
  if (server.hasArg("clk_date"))  dispSettings.clockDateColor = htmlToRgb565(server.arg("clk_date").c_str());
  if (server.hasArg("clk_size")) {
    int s = server.arg("clk_size").toInt();
    if (s >= 0 && s <= 3) dispSettings.clockTimeSize = (uint8_t)s;
  }
  if (server.hasArg("clk_dsize")) {
    int s = server.arg("clk_dsize").toInt();
    if (s >= 0 && s <= 3) dispSettings.clockDateSize = (uint8_t)s;
  }
  dispSettings.hideClockDate = server.hasArg("clk_hidedate");

  readGaugeColorsFromForm("prg", dispSettings.progress);
  readGaugeColorsFromForm("noz", dispSettings.nozzle);
  readGaugeColorsFromForm("bed", dispSettings.bed);
  readGaugeColorsFromForm("pfn", dispSettings.partFan);
  readGaugeColorsFromForm("afn", dispSettings.auxFan);
  readGaugeColorsFromForm("afr", dispSettings.auxFanRight);
  readGaugeColorsFromForm("cfn", dispSettings.chamberFan);
  readGaugeColorsFromForm("exh", dispSettings.exhaustFan);
  readGaugeColorsFromForm("cht", dispSettings.chamberTemp);
  readGaugeColorsFromForm("hbk", dispSettings.heatbreak);
  readGaugeColorsFromForm("pwr", dispSettings.power);
  readGaugeColorsFromForm("lyr", dispSettings.layer);

  // Custom gauge labels (empty = keep built-in default)
  readGaugeLabelFromForm("prg_lbl", gaugeLabels.progress,    sizeof(gaugeLabels.progress));
  readGaugeLabelFromForm("noz_lbl", gaugeLabels.nozzle,      sizeof(gaugeLabels.nozzle));
  readGaugeLabelFromForm("nzr_lbl", gaugeLabels.nozzleRight, sizeof(gaugeLabels.nozzleRight));
  readGaugeLabelFromForm("nzl_lbl", gaugeLabels.nozzleLeft,  sizeof(gaugeLabels.nozzleLeft));
  readGaugeLabelFromForm("bed_lbl", gaugeLabels.bed,         sizeof(gaugeLabels.bed));
  readGaugeLabelFromForm("pfn_lbl", gaugeLabels.partFan,     sizeof(gaugeLabels.partFan));
  readGaugeLabelFromForm("afn_lbl", gaugeLabels.auxFan,      sizeof(gaugeLabels.auxFan));
  readGaugeLabelFromForm("afr_lbl", gaugeLabels.auxFanRight, sizeof(gaugeLabels.auxFanRight));
  readGaugeLabelFromForm("cfn_lbl", gaugeLabels.chamberFan,  sizeof(gaugeLabels.chamberFan));
  readGaugeLabelFromForm("exh_lbl", gaugeLabels.exhaustFan,  sizeof(gaugeLabels.exhaustFan));
  readGaugeLabelFromForm("cht_lbl", gaugeLabels.chamberTemp, sizeof(gaugeLabels.chamberTemp));
  readGaugeLabelFromForm("hbk_lbl", gaugeLabels.heatbreak,   sizeof(gaugeLabels.heatbreak));
  readGaugeLabelFromForm("pwr_lbl", gaugeLabels.power,       sizeof(gaugeLabels.power));
  readGaugeLabelFromForm("lyr_lbl", gaugeLabels.layer,       sizeof(gaugeLabels.layer));
  readGaugeLabelFromForm("clk_lbl", gaugeLabels.clock,       sizeof(gaugeLabels.clock));
  readGaugeLabelFromForm("ams_lbl", gaugeLabels.amsBase,     sizeof(gaugeLabels.amsBase));
  readGaugeLabelFromForm("dor_lbl", gaugeLabels.door,        sizeof(gaugeLabels.door));

  if (server.hasArg("fmins")) {
    dpSettings.finishDisplayMins = server.arg("fmins").toInt();
  }

  // Gauge full-scale ranges (clamp to safe bounds)
  if (server.hasArg("noz_max"))
    dispSettings.nozzleScaleMax  = constrain(server.arg("noz_max").toInt(), GAUGE_NOZZLE_SCALE_MIN, GAUGE_NOZZLE_SCALE_MAX);
  if (server.hasArg("bed_max"))
    dispSettings.bedScaleMax     = constrain(server.arg("bed_max").toInt(), GAUGE_BED_SCALE_MIN, GAUGE_BED_SCALE_MAX);
  if (server.hasArg("cht_max"))
    dispSettings.chamberScaleMax = constrain(server.arg("cht_max").toInt(), GAUGE_CHAMBER_SCALE_MIN, GAUGE_CHAMBER_SCALE_MAX);
  if (server.hasArg("pwr_max"))
    dispSettings.powerScaleW     = constrain(server.arg("pwr_max").toInt(), GAUGE_POWER_SCALE_MIN, GAUGE_POWER_SCALE_MAX);

  // Gauge behavior: smoothing speed + temp warning color
  if (server.hasArg("gsmooth")) {
    int sm = server.arg("gsmooth").toInt();
    dispSettings.gaugeSmoothing = (sm >= 0 && sm <= 3) ? (uint8_t)sm : 2;
  }
  if (server.hasArg("warn_thr"))
    dispSettings.warnThresholdPct = constrain(server.arg("warn_thr").toInt(), 0, 100);
  if (server.hasArg("warn_clr"))
    dispSettings.warnColor = htmlToRgb565(server.arg("warn_clr").c_str());
  dpSettings.keepDisplayOn = server.hasArg("keepon");
  // "Keep finish screen visible" sends no clock arg - preserve the stored
  // clock-vs-off destination instead of silently resetting it to off.
  if (!dpSettings.keepDisplayOn)
    dpSettings.showClockAfterFinish = server.hasArg("clock");
  dpSettings.doorAckEnabled = server.hasArg("dack");
  dpSettings.keepPrintScreen = server.hasArg("kps");
  dispSettings.animatedBar = server.hasArg("abar");
  dispSettings.pongClock = server.hasArg("pong");
  dispSettings.smallLabels = server.hasArg("slbl");
  dispSettings.showTimeRemaining = server.hasArg("shtire");
  dispSettings.fanMatchPrinter = server.hasArg("fanmp");

  // Clock settings (timezone, 24h)
  if (server.hasArg("tz")) {
    size_t tzCount;
    const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
    int idx = server.arg("tz").toInt();
    if (idx >= 0 && idx < (int)tzCount) {
      netSettings.timezoneIndex = (uint8_t)idx;
      strlcpy(netSettings.timezoneStr, regions[idx].posixString, sizeof(netSettings.timezoneStr));
    }
  }
  netSettings.use24h = server.hasArg("use24h");
  if (server.hasArg("datefmt")) {
    int df = server.arg("datefmt").toInt();
    if (df >= 0 && df <= 5) netSettings.dateFormat = (uint8_t)df;
  }
}

// ---------------------------------------------------------------------------
//  Route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  if (isAPMode()) {
    serveApPage();
  } else {
    serveMainPage();
  }
}

// Save printer settings only (no restart — reinit MQTT)
static void handleSavePrinter() {
  // Free any active SSDP scan sockets before we reinit networking below.
  ssdpStopScan();

  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

#ifdef BOARD_LOW_RAM
  if (slot > 0 && !dualPrinterUnsafe) {
    server.send(200, "application/json",
      "{\"status\":\"error\",\"message\":\"Enable experimental 2-printer mode in Printer Settings to configure printer 2.\"}");
    return;
  }
#endif
#ifdef BOARD_HAS_PSRAM
  if (slot > 1 && !quadPrinterBeta) {
    server.send(200, "application/json",
      "{\"status\":\"error\",\"message\":\"Enable experimental 4-printer mode in Advanced settings to configure printer 3/4.\"}");
    return;
  }
#endif

  PrinterConfig& cfg = printers[slot].config;
  if (server.hasArg("connmode")) {
    String cm = server.arg("connmode");
    if (cm == "cloud_all") cfg.mode = CONN_CLOUD_ALL;
    else cfg.mode = CONN_LOCAL;
  }

  // Cloud region
  if (server.hasArg("region")) {
    String rg = server.arg("region");
    if (rg == "eu") cfg.region = REGION_EU;
    else if (rg == "cn") cfg.region = REGION_CN;
    else cfg.region = REGION_US;
  }

  if (isCloudMode(cfg.mode)) {
    if (server.hasArg("serial")) strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("pname")) { strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name)); utf8TrimPartial(cfg.name); }
    // Save token if provided
    if (server.hasArg("token") && server.arg("token").length() > 0) {
      saveCloudToken(server.arg("token").c_str());
    }
    // Extract userId from stored token
    char tokenBuf[1200];
    if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      if (!cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId))) {
        cloudFetchUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId), cfg.region);
      }
    }
  } else {
    if (server.hasArg("pname")) { strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name)); utf8TrimPartial(cfg.name); }
    if (server.hasArg("ip"))     strlcpy(cfg.ip, server.arg("ip").c_str(), sizeof(cfg.ip));
    if (server.hasArg("serial")) strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("code") && server.arg("code").length() > 0) strlcpy(cfg.accessCode, server.arg("code").c_str(), sizeof(cfg.accessCode));
  }

  // Serial numbers must be uppercase (Bambu MQTT topics are case-sensitive)
  for (char* p = cfg.serial; *p; p++) *p = toupper(*p);

  // Validate required fields and build warnings
  String warnings = "";
  if (isCloudMode(cfg.mode)) {
    if (strlen(cfg.serial) == 0)
      warnings += "Serial number is required for cloud mode. ";
    if (strlen(cfg.cloudUserId) == 0)
      warnings += "Cloud token is missing or invalid (userId extraction failed). ";
  } else {
    if (strlen(cfg.ip) == 0)
      warnings += "Printer IP address is required. ";
    if (strlen(cfg.serial) == 0)
      warnings += "Serial number is required (used for MQTT topic). ";
    if (strlen(cfg.accessCode) == 0)
      warnings += "Access code is required. ";
    else if (strlen(cfg.accessCode) != 8)
      warnings += "Access code should be 8 characters (check printer LCD). ";
  }

  savePrinterConfig(slot);

  // Reinit only the edited slot - the other printer's connection and live
  // display state stay untouched.
  initBambuMqttSlot(slot);

  if (warnings.length() > 0) {
    String resp = "{\"status\":\"ok\",\"warning\":\"" + warnings + "\"}";
    server.send(200, "application/json", resp);
  } else {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
}

// Clear one printer slot back to factory defaults (connection details +
// gauge layout) so a new printer can be configured from scratch. Without
// this there is no way to fully vacate a slot: the access code and token
// fields are password-style and only saved when non-empty.
static void handleClearPrinter() {
  ssdpStopScan();

  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  clearPrinterConfig(slot);
  initBambuMqttSlot(slot);  // slot now unconfigured -> disconnects + clears state

  if (rotState.displayIndex == slot) {
    rotState.displayIndex = (slot == 0 && isPrinterConfigured(1)) ? 1 : 0;
  }
  // Keep the split second-slot valid and distinct from the deleted slot.
  if (rotState.splitIndexB == slot || rotState.splitIndexB >= MAX_ACTIVE_PRINTERS) {
    rotState.splitIndexB = (rotState.displayIndex == 0) ? 1 : 0;
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Save gauge layout only (no MQTT reinit needed)
static void handleSaveGaugeLayout() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

#ifdef BOARD_LOW_RAM
  if (slot > 0 && !dualPrinterUnsafe) {
    server.send(200, "application/json",
      "{\"status\":\"error\",\"message\":\"Enable experimental 2-printer mode to configure printer 2.\"}");
    return;
  }
#endif
#ifdef BOARD_HAS_PSRAM
  if (slot > 1 && !quadPrinterBeta) {
    server.send(200, "application/json",
      "{\"status\":\"error\",\"message\":\"Enable experimental 4-printer mode to configure printer 3/4.\"}");
    return;
  }
#endif

  PrinterConfig& cfg = printers[slot].config;
  auto readSlotArg = [&](const char* prefix, uint8_t idx, uint8_t& out) {
    char argName[8];
    snprintf(argName, sizeof(argName), "%s%d", prefix, idx);
    if (server.hasArg(argName)) {
      uint8_t val = server.arg(argName).toInt();
      out = (val < GAUGE_TYPE_COUNT) ? val : GAUGE_EMPTY;
    }
  };
  for (uint8_t g = 0; g < GAUGE_SLOT_COUNT;       g++) readSlotArg("gs", g, cfg.gaugeSlots[g]);
  for (uint8_t g = 0; g < LANDSCAPE_EXTRA_COUNT;  g++) readSlotArg("lx", g, cfg.landscapeExtras[g]);
  for (uint8_t g = 0; g < PORTRAIT_EXTRA_COUNT;   g++) readSlotArg("px", g, cfg.portraitExtras[g]);
  cfg.amsView = server.hasArg("amsv");

  savePrinterConfig(slot);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Save WiFi + network settings (requires restart)
static void handleSaveWifi() {
  if (server.hasArg("ssid")) strlcpy(wifiSSID, server.arg("ssid").c_str(), sizeof(wifiSSID));
  if (server.hasArg("pass") && server.arg("pass").length() > 0) strlcpy(wifiPass, server.arg("pass").c_str(), sizeof(wifiPass));

  netSettings.useDHCP = (!server.hasArg("netmode") || server.arg("netmode") == "dhcp");
  if (server.hasArg("net_ip"))  strlcpy(netSettings.staticIP, server.arg("net_ip").c_str(), sizeof(netSettings.staticIP));
  if (server.hasArg("net_gw"))  strlcpy(netSettings.gateway, server.arg("net_gw").c_str(), sizeof(netSettings.gateway));
  if (server.hasArg("net_sn"))  strlcpy(netSettings.subnet, server.arg("net_sn").c_str(), sizeof(netSettings.subnet));
  if (server.hasArg("net_dns")) strlcpy(netSettings.dns, server.arg("net_dns").c_str(), sizeof(netSettings.dns));
  if (server.hasArg("has_showip"))  // full page sends this; AP page doesn't
    netSettings.showIPAtStartup = server.hasArg("showip");

  if (server.hasArg("has_mdns")) {
    netSettings.mdnsEnabled = server.hasArg("mdns_en");
    // Don't trust the client - sanitize server-side too.
    if (server.hasArg("mdns_host"))
      sanitizeHostname(server.arg("mdns_host").c_str(), netSettings.hostname,
                       sizeof(netSettings.hostname));
  }

  saveSettings();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
  scheduleRestart();
}

// Live brightness preview (no save, just PWM update)
// Only applies when the main display is active — during clock/screensaver
// the screensaverBrightness governs the backlight, not the main slider.
static void handleBrightnessPreview() {
  if (server.hasArg("val")) {
    uint8_t val = server.arg("val").toInt();
    ScreenState scr = getScreenState();
    if (scr != SCREEN_CLOCK && scr != SCREEN_OFF) {
      setBacklight(val);
    }
  }
  server.send(200, "text/plain", "OK");
}

// Apply display settings live (no restart)
static void handleApply() {
  // Snapshot timezone before parsing — only re-init NTP if it changes.
  // configTzTime() resets the SNTP sync status, which causes getLocalTime()
  // to return false for up to 60s, blanking the clock screen unnecessarily.
  char prevTz[sizeof(netSettings.timezoneStr)];
  strlcpy(prevTz, netSettings.timezoneStr, sizeof(prevTz));
  readDisplayFromForm();
  saveSettings();
  applyDisplaySettings();
  if (strcmp(netSettings.timezoneStr, prevTz) != 0) {
    configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
  }
  server.send(200, "text/plain", "OK");
}

static void handleStatus() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  BambuState& st = printers[slot].state;

  JsonDocument doc;
  // Per-printer (driven by ?slot=)
  doc["connected"] = st.connected;
  doc["configured"] = isPrinterConfigured(slot);
  doc["state"] = st.gcodeState;
  // "Connected but no data": broker accepted us but zero messages have arrived
  // 20s after connecting. Means a wrong serial (topic never matches) or the
  // printer is powered off - both look identical from the broker's side.
  {
    const MqttDiag& d = getMqttDiag(slot);
    doc["no_data"] = st.connected && d.messagesRx == 0 && d.connectTime > 0 &&
                     (millis() - d.connectTime) > 20000;
  }
  doc["progress"] = st.progress;
  doc["nozzle"] = (int)st.nozzleTemp;
  doc["nozzle_t"] = (int)st.nozzleTarget;
  doc["bed"] = (int)st.bedTemp;
  doc["bed_t"] = (int)st.bedTarget;
  doc["fan"] = st.coolingFanPct;
  doc["layer"] = st.layerNum;
  doc["layers"] = st.totalLayers;
  doc["display_off"] = (getScreenState() == SCREEN_OFF);
  doc["name"] = printers[slot].config.name;
  doc["lightState"] = st.lightState;  // -1 unknown / 0 off / 1 on (chamber light)

  // Device-wide (new design's Detected Hardware + WiFi live KV)
  doc["heap_kb"] = ESP.getFreeHeap() / 1024;
  doc["uptime"] = (uint32_t)(millis() / 1000);
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["flash_kb"] = ESP.getFlashChipSize() / 1024;
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
  doc["psram_kb"] = ESP.getPsramSize() / 1024;
#else
  doc["psram_kb"] = 0;
#endif

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Clean reboot (preserves NVS). Wired to the Danger Zone "Reboot" button.
// Distinct from /reset which wipes settings via resetSettings().
static void handleReboot() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
  scheduleRestart(1000);
}

static void handleTimezones() {
  size_t tzCount;
  const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
  // Stream JSON directly to avoid building large String
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"selected\":");
  server.sendContent(String((int)netSettings.timezoneIndex));
  server.sendContent(",\"zones\":[");
  for (size_t i = 0; i < tzCount; i++) {
    if (i > 0) server.sendContent(",");
    // JSON-escape the label into a stack buffer (defensive - current labels are clean)
    char esc[80];
    size_t j = 0;
    esc[j++] = '"';
    for (const char* p = regions[i].name; *p && j < sizeof(esc) - 2; p++) {
      if (*p == '"' || *p == '\\') esc[j++] = '\\';
      esc[j++] = *p;
    }
    esc[j++] = '"';
    esc[j] = '\0';
    server.sendContent(esc);
  }
  server.sendContent("]}");
  server.sendContent("");  // terminate chunked response
}

static void handleReset() {
  server.send(200, "text/html",
    "<html><body style='background:#0D1117;color:#E6EDF3;text-align:center;padding-top:80px;font-family:sans-serif'>"
    "<h2 style='color:#F85149'>Factory Reset</h2>"
    "<p>Restarting...</p></body></html>");
  resetSettings();  // clears NVS and calls ESP.restart()
}

static void handleDebug() {
  JsonDocument doc;
  unsigned long now = millis();

  JsonArray arr = doc["printers"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    const MqttDiag& d = getMqttDiag(i);
    BambuState& st = printers[i].state;
    JsonObject p = arr.add<JsonObject>();
    p["slot"] = i;
    p["name"] = printers[i].config.name;
    p["connected"] = st.connected;
    p["attempts"] = d.attempts;
    p["messages"] = d.messagesRx;
    p["last_rc"] = d.lastRc;
    p["rc_text"] = mqttRcToString(d.lastRc);
    if (isCloudMode(printers[i].config.mode) &&
        (d.lastRc == 4 || d.lastRc == 5)) {
      p["rc_hint"] = "Token expired or invalidated (90-day TTL, or 'log out everywhere'/password change). Paste a fresh token in Setup.";
    }
    p["tcp_ok"] = d.tcpOk;
    p["pushall_total"] = d.pushallTotal;
    p["rec_print"] = d.recoveryPrint;
    p["rec_conn_dead"] = d.recoveryConnDead;
    p["rec_finish"] = d.recoveryFinish;
    p["rec_idle"] = d.recoveryIdle;
    p["rec_idle_hot"] = d.recoveryIdleHot;
    p["rec_finish_hot"] = d.recoveryFinishHot;
    p["rec_failed"] = d.recoveryFailed;
    p["last_pushall_reason"] = pushallReasonToString(d.lastPushallReason);
    p["last_pushall_age_s"] = d.lastPushallMs > 0 ? (now - d.lastPushallMs) / 1000UL : 0;
    p["last_update_age_s"] = st.lastUpdate > 0 ? (now - st.lastUpdate) / 1000UL : 0;
    p["last_print_data_age_s"] = st.lastPrintDataMs > 0 ? (now - st.lastPrintDataMs) / 1000UL : 0;
  }

  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["debug_log"] = mqttDebugLog;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleDebugToggle() {
  if (server.hasArg("on")) {
    mqttDebugLog = (server.arg("on") == "1");
  }
  server.send(200, "text/plain", mqttDebugLog ? "ON" : "OFF");
}

static void handleToggleSetting() {
  if (!server.hasArg("key") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Missing key/val");
    return;
  }
  String key = server.arg("key");
  bool on = (server.arg("val") == "1");

  if      (key == "keepon")  dpSettings.keepDisplayOn = on;
  else if (key == "clock")   dpSettings.showClockAfterFinish = on;
  else if (key == "dack")    dpSettings.doorAckEnabled = on;
  else if (key == "kps")     dpSettings.keepPrintScreen = on;
  else if (key == "abar")    dispSettings.animatedBar = on;
  else if (key == "pong")    dispSettings.pongClock = on;
  else if (key == "slbl")    dispSettings.smallLabels = on;
  else if (key == "shtire")  dispSettings.showTimeRemaining = on;
  else if (key == "fanmp")   dispSettings.fanMatchPrinter = on;
  else if (key == "hidelp")  dispSettings.hideStatusReadout = on;
  else if (key == "invcol")  dispSettings.invertColors = on;
  else if (key == "cydcls")  dispSettings.cydPanelClassic = on;
  else if (key == "cyd32e")  dispSettings.cyd32eVariant = on;
  else if (key == "rskin")   dispSettings.roundSkin = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
  else if (key == "l8s")     dispSettings.landscape8Slots = on;
  else if (key == "p9s")     dispSettings.portrait9Slots = on;
  else if (key == "clkinfo") dispSettings.showClockInfo = on;
  else if (key == "amst")    dispSettings.amsTrayTypes = on;
  else if (key == "btnpwr")  dispSettings.buttonPowerControl = on;
  else if (key == "nighten") dpSettings.nightModeEnabled = on;
  else if (key == "use24h")  netSettings.use24h = on;
  else if (key == "rotsplit")  rotState.splitEnabled = on;
  else if (key == "rotsplitf") rotState.splitForce = on;
  else if (key == "clkhd")     dispSettings.hideClockDate = on;
  else if (key == "showip")    netSettings.showIPAtStartup = on;
#ifdef BOARD_LOW_RAM
  else if (key == "dualp")   dualPrinterUnsafe = on;
#endif
#ifdef BOARD_HAS_PSRAM
  else if (key == "quadp")   quadPrinterBeta = on;
#endif
  else {
    server.send(400, "text/plain", "Unknown key");
    return;
  }

  saveSettings();
  if (key == "invcol" || key == "slbl" || key == "abar" || key == "shtire") applyDisplaySettings();
  if (key == "cydcls") scheduleRestart(800);  // panel swap needs a fresh init
  if (key == "cyd32e") scheduleRestart(800);  // re-init amp enable + RGB pins cleanly
  if (key == "rskin") triggerDisplayTransition();  // repaint print dashboard with the new skin
  if (key == "use24h") { resetClock(); resetPongClock(); triggerDisplayTransition(); }
  if (key == "clkinfo") { resetClock(); triggerDisplayTransition(); }
  if (key == "clkhd") { resetClock(); triggerDisplayTransition(); }
  if (key == "hidelp") triggerDisplayTransition();  // repaint status bar with/without readout
  if (key == "rotsplit" || key == "rotsplitf") triggerDisplayTransition();  // flip split layout live
  if (key == "amst") triggerDisplayTransition();  // force AMS-zone repaint
#ifdef BOARD_LOW_RAM
  if (key == "dualp") {
    if (!on && rotState.displayIndex == 1) {
      // User just disabled 2-printer mode - drop slot 1 from rotation/display
      rotState.displayIndex = 0;
    }
    if (!on && rotState.splitIndexB == 1) rotState.splitIndexB = 0;
    // Re-evaluate slot 1 active state without reboot; slot 0 stays connected.
    initBambuMqttSlot(1);
  }
#endif
#ifdef BOARD_HAS_PSRAM
  if (key == "quadp") {
    if (!on) {
      // User just disabled 4-printer beta - drop slots 2/3 from rotation/display
      if (rotState.displayIndex > 1) rotState.displayIndex = 0;
      if (rotState.splitIndexB > 1) rotState.splitIndexB = 0;
    }
    // Re-evaluate slots 2/3 without reboot; slots 0/1 stay connected.
    initBambuMqttSlot(2);
    initBambuMqttSlot(3);
  }
#endif
  if (key == "kps") {
    BambuState& st = printers[rotState.displayIndex].state;
    ScreenState cur = getScreenState();
    if (on && !st.printing && !st.ams.anyDrying &&
        (cur == SCREEN_IDLE || cur == SCREEN_FINISHED)) {
      setScreenState(SCREEN_PRINTING);
    } else if (!on && cur == SCREEN_PRINTING && !st.printing) {
      setScreenState(st.gcodeStateId == GCODE_FINISH
                     ? SCREEN_FINISHED : SCREEN_IDLE);
    }
  }
  server.send(200, "text/plain", "OK");
}

static void handleCloudLogout() {
  clearCloudToken();
  server.send(200, "text/plain", "OK");
}

// Discover Bambu printers on the local network via SSDP so the user can pick the
// exact serial (and IP) instead of typing it. POST starts a scan, GET polls.
static void handleLanScan() {
  if (server.method() == HTTP_POST) {
    int opened = ssdpStartScan();
    if (opened == 0) {
      server.send(200, "application/json",
                  "{\"status\":\"error\",\"msg\":\"Cannot scan: not on a Wi-Fi network, "
                  "or the network blocks multicast.\"}");
      return;
    }
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  // GET: report progress + whatever has been discovered so far.
  String devices;
  ssdpScanResultJson(devices);
  String json = "{\"status\":\"";
  json += ssdpScanActive() ? "scanning" : "done";
  json += "\",\"devices\":";
  json += devices;
  json += "}";
  server.send(200, "application/json", json);
}

// Get printer config for a specific slot (multi-printer tabs)
static void handlePrinterConfig() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  PrinterConfig& cfg = printers[slot].config;
  BambuState& st = printers[slot].state;

  JsonDocument doc;
  doc["mode"] = isCloudMode(cfg.mode) ? "cloud_all" : "local";
  doc["name"] = cfg.name;
  doc["ip"] = cfg.ip;
  doc["serial"] = cfg.serial;
  doc["region"] = cfg.region == REGION_EU ? "eu" : (cfg.region == REGION_CN ? "cn" : "us");
  doc["connected"] = st.connected;
  doc["configured"] = isPrinterConfigured(slot);
  // Per-fan capability flags derived from device.airduct.parts[] (only the
  // funcs actually reported by this printer). H2C reports func 0/1/2/4, X2D
  // reports 0/2/5/6 — so the UI gates each gauge on its specific bit instead
  // of a coarse "has airduct" boolean (which would falsely enable Aux-Right on H2C).
  doc["hasAuxFanRight"] = (st.airductFuncs & (1u << 6)) != 0;  // X2D only
  doc["hasExhaustFan"]  = (st.airductFuncs & (1u << 2)) != 0;  // X2D + H2C
  doc["hasDualNozzle"]  = st.dualNozzle;                       // H2D/H2C/X2D per-nozzle temp gauges
  doc["hasTasmota"]     = tasmotaConfiguredForSlot(slot);      // gates the Power gauge option
#if defined(BOARD_HAS_CAMERA)
  // Camera gauge: only LAN-mode P1/A1 with an access code serve the port-6000
  // chamber image. (P1S=01P, P1P=01S, A1=039, A1mini=030.)
  {
    const char* cs = cfg.serial;
    bool p1a1 = strlen(cs) >= 3 &&
                (strncmp(cs, "01P", 3) == 0 || strncmp(cs, "01S", 3) == 0 ||
                 strncmp(cs, "039", 3) == 0 || strncmp(cs, "030", 3) == 0);
    doc["hasLanCamera"] = (cfg.mode == CONN_LOCAL) && cfg.accessCode[0] && p1a1;
  }
#else
  doc["hasLanCamera"] = false;
#endif
  JsonArray slots = doc["gaugeSlots"].to<JsonArray>();
  for (uint8_t g = 0; g < GAUGE_SLOT_COUNT; g++) slots.add(cfg.gaugeSlots[g]);
  JsonArray lext = doc["landscapeExtras"].to<JsonArray>();
  for (uint8_t g = 0; g < LANDSCAPE_EXTRA_COUNT; g++) lext.add(cfg.landscapeExtras[g]);
  JsonArray pext = doc["portraitExtras"].to<JsonArray>();
  for (uint8_t g = 0; g < PORTRAIT_EXTRA_COUNT;  g++) pext.add(cfg.portraitExtras[g]);
  doc["amsView"] = cfg.amsView;
  doc["lightFlags"] = cfg.lightFlags;       // chamber-light automation bitmask
  doc["lightDelay"] = cfg.lightOffDelayMin; // off delay (minutes)
  doc["lightState"] = st.lightState;        // -1 unknown / 0 off / 1 on

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Save chamber-light automation settings (no MQTT reinit - mirrors gauge layout)
static void handleLightConfig() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  PrinterConfig& cfg = printers[slot].config;
  uint8_t flags = 0;
  if (server.hasArg("loff_fin"))  flags |= LIGHT_OFF_ON_FINISH;
  if (server.hasArg("loff_fail")) flags |= LIGHT_OFF_ON_FAILED;
  if (server.hasArg("lon_start")) flags |= LIGHT_ON_AT_START;
  cfg.lightFlags = flags;
  if (server.hasArg("ldelay"))
    cfg.lightOffDelayMin = constrain(server.arg("ldelay").toInt(), 0, 60);

  // If both off-rules are now disabled, cancel any pending off timer so a stale
  // deadline scheduled under the old rules doesn't still turn the light off later.
  if (!(flags & (LIGHT_OFF_ON_FINISH | LIGHT_OFF_ON_FAILED)))
    printers[slot].state.lightOffDueMs = 0;

  savePrinterConfig(slot);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Turn chamber light on/off now from the web UI
static void handleLightSet() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;
  if (!isPrinterConfigured(slot)) {
    server.send(409, "application/json", "{\"status\":\"error\",\"message\":\"printer not configured\"}");
    return;
  }
  String mode = server.hasArg("mode") ? server.arg("mode") : String();
  if (mode != "on" && mode != "off") {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"mode must be on or off\"}");
    return;
  }
  requestLightCommand(slot, mode == "on");
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Test buzzer from web UI
static void handleBuzzerTest() {
  uint8_t snd = 0;
  if (server.hasArg("sound")) snd = server.arg("sound").toInt();
  if (snd <= 2) buzzerPlay((BuzzerEvent)snd);
  else if (snd == 4) buzzerPlay(BUZZ_BED_COOLDOWN);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Live LED preview from web UI. Validates pin range as int before casting to
// uint8_t (avoids 300 -> 44 wraparound). On invalid pin we shut the preview
// off so the user doesn't see a "ghost" LED still lit on the previous pin.
static void handleLedPreview() {
  bool en = server.hasArg("en") ? (server.arg("en") == "1") : ledSettings.enabled;

  int rawPin = server.hasArg("pin") ? server.arg("pin").toInt() : ledSettings.pin;
  if (rawPin < 1 || rawPin > 48) {
    previewLed(false, 0, 0);
    server.send(400, "application/json", "{\"error\":\"pin out of range\"}");
    return;
  }
  uint8_t pin = (uint8_t)rawPin;

  uint8_t br = ledSettings.brightness;
  if (server.hasArg("br")) {
    int v = server.arg("br").toInt();
    if (v < 0) v = 0; if (v > 255) v = 255;
    br = (uint8_t)v;
  }

  previewLed(en, pin, br);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Trigger the chosen finish effect for a short window without waiting for a
// real print finish. Reads md/sec/br as overrides; falls back to ledSettings.
// Doesn't gate on ledSettings.enabled - a previewed-but-unsaved config also
// has the pin attached and should be testable. ledTriggerTestEffect() reports
// whether it actually started so we can return a meaningful error.
static void handleLedTest() {
  uint8_t md = ledSettings.finishMode;
  if (server.hasArg("md")) {
    int v = server.arg("md").toInt();
    if (v >= 0 && v <= 2) md = (uint8_t)v;
  }
  if (md == LED_FINISH_OFF) {
    server.send(409, "application/json", "{\"status\":\"err\",\"error\":\"mode off\"}");
    return;
  }

  uint16_t sec = LED_TEST_DURATION_S;
  if (server.hasArg("sec")) {
    int v = server.arg("sec").toInt();
    if (v < 5) v = 5; if (v > 600) v = 600;
    // For the test we cap to a sane preview window so the user isn't stuck
    // waiting 10 minutes if they configured a long real-finish duration.
    if (v > 30) v = LED_TEST_DURATION_S;
    sec = (uint16_t)v;
  }

  uint8_t br = ledSettings.finishBrightness;
  if (server.hasArg("br")) {
    int v = server.arg("br").toInt();
    if (v < 0) v = 0; if (v > 255) v = 255;
    br = (uint8_t)v;
  }

  if (!ledTriggerTestEffect(md, sec, br)) {
    server.send(409, "application/json", "{\"status\":\"err\",\"error\":\"LED not attached - enable it first\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Save rotation settings (multi-printer)
static void handleSaveRotation() {
  if (server.hasArg("rotmode")) {
    uint8_t mode = server.arg("rotmode").toInt();
    if (mode <= 2) rotState.mode = (RotateMode)mode;
  }
  if (server.hasArg("rotinterval")) {
    uint32_t sec = server.arg("rotinterval").toInt();
    uint32_t ms = sec * 1000;
    if (ms < ROTATE_MIN_MS) ms = ROTATE_MIN_MS;
    if (ms > ROTATE_MAX_MS) ms = ROTATE_MAX_MS;
    rotState.intervalMs = ms;
  }
  if (server.hasArg("rotsplit")) {
    rotState.splitEnabled = (server.arg("rotsplit") == "1");
  }
  if (server.hasArg("rotsplitf")) {
    rotState.splitForce = (server.arg("rotsplitf") == "1");
  }
  saveRotationSettings();

  // Button settings
  if (server.hasArg("btntype")) {
    uint8_t bt = server.arg("btntype").toInt();
    if (bt <= 3) buttonType = (ButtonType)bt;
  }
  if (server.hasArg("btnpin")) {
    uint8_t bp = server.arg("btnpin").toInt();
    if (bp > 0 && bp <= 48) buttonPin = bp;
  }
  saveButtonSettings();
  initButton();

  // Buzzer settings
  if (server.hasArg("buzzen")) {
    buzzerSettings.enabled = (server.arg("buzzen") == "1");
  }
  if (server.hasArg("buzpin")) {
    uint8_t bp = server.arg("buzpin").toInt();
    if (bp > 0 && bp <= 48) buzzerSettings.pin = bp;
  }
  if (server.hasArg("buzqs")) {
    int qs = server.arg("buzqs").toInt();
    if (qs >= 0 && qs <= 23) buzzerSettings.quietStartHour = qs;
  }
  if (server.hasArg("buzqe")) {
    int qe = server.arg("buzqe").toInt();
    if (qe >= 0 && qe <= 23) buzzerSettings.quietEndHour = qe;
  }
  if (server.hasArg("buzclick")) {
    buzzerSettings.buttonClick = (server.arg("buzclick") == "1");
  }
  if (server.hasArg("buzbeden")) {
    buzzerSettings.bedCooldownAlert = (server.arg("buzbeden") == "1");
  }
  if (server.hasArg("buzbedtemp")) {
    int t = server.arg("buzbedtemp").toInt();
    if (t < 20) t = 20;
    if (t > 80) t = 80;
    buzzerSettings.bedCooldownThresholdC = (uint8_t)t;
  }
  saveBuzzerSettings();
  initBuzzer();

  // Battery indicator visibility (Waveshare boards). Always parse so the
  // form's unchecked state reaches NVS (browsers omit unchecked checkboxes;
  // saveRotation() JS sends an explicit 0/1 to work around that).
  if (server.hasArg("batshow")) {
    dispSettings.showBatteryIndicator = (server.arg("batshow") == "1");
    saveBatteryIndicatorSetting();
  }

  // External LED — must be parsed AFTER button + buzzer so sanitizeLedPin()
  // sees the freshly-applied buttonPin and buzzerSettings.pin when checking
  // for conflicts.
  if (server.hasArg("leden"))  ledSettings.enabled = (server.arg("leden") == "1");
  if (server.hasArg("ledpin")) {
    int lp = server.arg("ledpin").toInt();
    if (lp > 0 && lp <= 48) ledSettings.pin = (uint8_t)lp;
  }
  if (server.hasArg("ledbr")) {
    int br = server.arg("ledbr").toInt();
    if (br < 0) br = 0; if (br > 255) br = 255;
    ledSettings.brightness = (uint8_t)br;
  }
  if (server.hasArg("ledfxmd")) {
    int v = server.arg("ledfxmd").toInt();
    if (v >= 0 && v <= 2) ledSettings.finishMode = (uint8_t)v;
  }
  if (server.hasArg("ledfxsec")) {
    int v = server.arg("ledfxsec").toInt();
    if (v < 5) v = 5; if (v > 600) v = 600;
    ledSettings.finishSeconds = (uint16_t)v;
  }
  if (server.hasArg("ledfxbr")) {
    int v = server.arg("ledfxbr").toInt();
    if (v < 0) v = 0; if (v > 255) v = 255;
    ledSettings.finishBrightness = (uint8_t)v;
  }
  // Checkboxes: present + value "1" = enabled. Form posts "0" when unchecked
  // (saveRotation JS sends both states explicitly).
  if (server.hasArg("ledauto"))  ledSettings.autoOnWhilePrinting = (server.arg("ledauto")  == "1");
  if (server.hasArg("ledpause")) ledSettings.pauseBreathing      = (server.arg("ledpause") == "1");
  if (server.hasArg("lederr"))   ledSettings.errorStrobe         = (server.arg("lederr")   == "1");
  if (server.hasArg("lederrsec")) {
    int v = server.arg("lederrsec").toInt();
    if (v != 0 && v < 5) v = 5; if (v > 600) v = 600;
    ledSettings.errorStrobeSeconds = (uint16_t)v;
  }
  saveLedSettings();
  initLed();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
//  Tasmota power monitoring — config + stats + save
// ---------------------------------------------------------------------------
static void handleGetPowerConfig() {
  int plug = server.hasArg("plug") ? server.arg("plug").toInt() : 0;
  if (plug < 0 || plug >= TASMOTA_PLUG_COUNT) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  TasmotaSettings& s = tasmotaSettings[plug];
  JsonDocument doc;
  doc["enabled"]         = s.enabled;
  doc["plugType"]        = s.plugType;            // 0=Tasmota, 1=Shelly Gen2
  doc["ip"]              = s.ip;
  doc["displayMode"]     = s.displayMode;
  doc["pollInterval"]    = s.pollInterval;
  doc["autoOffEnabled"]      = s.autoOffEnabled;
  doc["autoOffDelayMin"]     = s.autoOffDelayMin;
  doc["autoOffCancelOnDoor"] = s.autoOffCancelOnDoor;
  doc["tariff"]              = tasmotaTariffPerKwh;   // global
  doc["currency"]        = tasmotaCurrency;       // global
#if TASMOTA_PLUG_COUNT == 1
  doc["assignedSlot"]    = s.assignedSlot;
#endif
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleGetPowerStats() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; i++) {
    TasmotaPlugStatsView v;
    tasmotaGetStats(i, &v);
    JsonObject o = arr.add<JsonObject>();
    o["online"]     = v.online;
    o["watts"]      = v.watts;
    o["today"]      = v.todayKwh;
    o["total"]      = v.totalKwh;
    o["thisPrint"]  = v.printUsedKwh;
    o["plugType"]   = tasmotaSettings[i].plugType;  // JS hides Today row for Shelly
    o["stateKnown"] = v.powerStateKnown;            // true => use real on/off below
    o["on"]         = v.powerOn;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleSavePower() {
  // Currency and tariff are global - update if present regardless of plug index
  if (server.hasArg("tsm_cur")) {
    strlcpy(tasmotaCurrency, server.arg("tsm_cur").c_str(), sizeof(tasmotaCurrency));
    utf8TrimPartial(tasmotaCurrency);  // 8B buffer can slice a multi-byte symbol
  }
  if (server.hasArg("tsm_tar")) {
    float t = server.arg("tsm_tar").toFloat();
    if (t < 0.0f) t = 0.0f;
    if (t > 10.0f) t = 10.0f;
    tasmotaTariffPerKwh = t;
  }

  int plug = server.hasArg("plug") ? server.arg("plug").toInt() : 0;
  if (plug < 0 || plug >= TASMOTA_PLUG_COUNT) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid plug index\"}");
    return;
  }
  TasmotaSettings& s = tasmotaSettings[plug];

  // Checkboxes are always submitted as 0/1 from the JS; treat "absent" as no change
  if (server.hasArg("tsm_en"))  s.enabled = (server.arg("tsm_en").toInt() != 0);
  if (server.hasArg("tsm_pt"))  s.plugType = (server.arg("tsm_pt").toInt() == 1) ? 1 : 0;
  if (server.hasArg("tsm_ip"))  strlcpy(s.ip, server.arg("tsm_ip").c_str(), sizeof(s.ip));
  if (server.hasArg("tsm_dm")) {
    int dm = server.arg("tsm_dm").toInt();
    s.displayMode = (dm >= 0 && dm <= 2) ? (uint8_t)dm : 0;  // 0=alt 1=watts 2=layer
  }
  if (server.hasArg("tsm_pi")) {
    int pi = server.arg("tsm_pi").toInt();
    s.pollInterval = (pi >= 10 && pi <= 60) ? (uint8_t)pi : 10;
  }
  if (server.hasArg("tsm_ao"))  s.autoOffEnabled = (server.arg("tsm_ao").toInt() != 0);
  if (server.hasArg("tsm_ad")) {
    int ad = server.arg("tsm_ad").toInt();
    s.autoOffDelayMin = (ad >= 1 && ad <= 240) ? (uint8_t)ad : 10;
  }
  if (server.hasArg("tsm_aod")) s.autoOffCancelOnDoor = (server.arg("tsm_aod").toInt() != 0);
#if TASMOTA_PLUG_COUNT == 1
  if (server.hasArg("tsm_slot")) {
    int slot = server.arg("tsm_slot").toInt();
    s.assignedSlot = (slot >= 0 && slot < MAX_ACTIVE_PRINTERS) ? (uint8_t)slot : 255;
  }
#endif

  saveSettings();
  tasmotaInit();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handlePowerControl() {
  int plug = server.hasArg("plug") ? server.arg("plug").toInt() : 0;
  if (plug < 0 || plug >= TASMOTA_PLUG_COUNT || !tasmotaSettings[plug].enabled) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Plug not enabled\"}");
    return;
  }
  if (!server.hasArg("on")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing on parameter\"}");
    return;
  }
  bool on = (server.arg("on").toInt() != 0);
  if (tasmotaSetPower((uint8_t)plug, on)) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(502, "application/json", "{\"status\":\"error\",\"message\":\"Plug did not respond\"}");
  }
}

// ---------------------------------------------------------------------------
//  Settings export (JSON download)
// ---------------------------------------------------------------------------
static void gaugeColorsToJson(JsonObject& obj, const GaugeColors& gc) {
  char buf[8];
  rgb565ToHtml(gc.arc, buf);   obj["arc"] = String(buf);
  rgb565ToHtml(gc.label, buf); obj["label"] = String(buf);
  rgb565ToHtml(gc.value, buf); obj["value"] = String(buf);
}

static void handleSettingsExport() {
  JsonDocument doc;
  doc["_type"] = "bambuhelper_settings";
  doc["_version"] = FW_VERSION;

  // WiFi
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = wifiSSID;
  wifi["pass"] = wifiPass;

  // Printers
  JsonArray pArr = doc["printers"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    PrinterConfig& cfg = printers[i].config;
    JsonObject p = pArr.add<JsonObject>();
    p["mode"] = (uint8_t)cfg.mode;
    p["name"] = cfg.name;
    p["ip"] = cfg.ip;
    p["serial"] = cfg.serial;
    p["accessCode"] = cfg.accessCode;
    p["cloudUserId"] = cfg.cloudUserId;
    p["region"] = (uint8_t)cfg.region;
    JsonArray slots = p["gaugeSlots"].to<JsonArray>();
    for (uint8_t g = 0; g < GAUGE_SLOT_COUNT; g++) slots.add(cfg.gaugeSlots[g]);
    JsonArray lext = p["landscapeExtras"].to<JsonArray>();
    for (uint8_t g = 0; g < LANDSCAPE_EXTRA_COUNT; g++) lext.add(cfg.landscapeExtras[g]);
    JsonArray pext = p["portraitExtras"].to<JsonArray>();
    for (uint8_t g = 0; g < PORTRAIT_EXTRA_COUNT;  g++) pext.add(cfg.portraitExtras[g]);
    p["amsView"] = cfg.amsView;
    p["lightFlags"] = cfg.lightFlags;       // chamber-light automation bitmask
    p["lightDelay"] = cfg.lightOffDelayMin; // off delay (minutes)
  }

  // Display
  char buf[8];
  JsonObject disp = doc["display"].to<JsonObject>();
  disp["brightness"] = brightness;
  disp["rotation"] = dispSettings.rotation;
  rgb565ToHtml(dispSettings.bgColor, buf);    disp["bgColor"] = String(buf);
  rgb565ToHtml(dispSettings.trackColor, buf); disp["trackColor"] = String(buf);
  rgb565ToHtml(dispSettings.progressBarColor, buf); disp["progressBarColor"] = String(buf);
  rgb565ToHtml(dispSettings.clockTimeColor, buf); disp["clockTimeColor"] = String(buf);
  rgb565ToHtml(dispSettings.clockDateColor, buf); disp["clockDateColor"] = String(buf);
  disp["clockTimeSize"] = dispSettings.clockTimeSize;
  disp["clockDateSize"] = dispSettings.clockDateSize;
  disp["hideClockDate"] = dispSettings.hideClockDate;
  disp["showClockInfo"] = dispSettings.showClockInfo;
  disp["amsTrayTypes"] = dispSettings.amsTrayTypes;
  disp["buttonPowerControl"] = dispSettings.buttonPowerControl;
  disp["animatedBar"] = dispSettings.animatedBar;
  disp["pongClock"] = dispSettings.pongClock;
  disp["smallLabels"] = dispSettings.smallLabels;
  disp["showTimeRemaining"] = dispSettings.showTimeRemaining;
  disp["fanMatchPrinter"] = dispSettings.fanMatchPrinter;
  disp["hideStatusReadout"] = dispSettings.hideStatusReadout;
  disp["showBatteryIndicator"] = dispSettings.showBatteryIndicator;
  disp["nozzleScaleMax"]  = dispSettings.nozzleScaleMax;
  disp["bedScaleMax"]     = dispSettings.bedScaleMax;
  disp["chamberScaleMax"] = dispSettings.chamberScaleMax;
  disp["powerScaleW"]     = dispSettings.powerScaleW;
  disp["gaugeSmoothing"]  = dispSettings.gaugeSmoothing;
  rgb565ToHtml(dispSettings.warnColor, buf); disp["warnColor"] = String(buf);
  disp["warnThresholdPct"] = dispSettings.warnThresholdPct;
  disp["roundSkin"] = dispSettings.roundSkin;

  JsonObject gauges = disp["gauges"].to<JsonObject>();
  JsonObject gPrg = gauges["progress"].to<JsonObject>(); gaugeColorsToJson(gPrg, dispSettings.progress);
  JsonObject gNoz = gauges["nozzle"].to<JsonObject>();   gaugeColorsToJson(gNoz, dispSettings.nozzle);
  JsonObject gBed = gauges["bed"].to<JsonObject>();      gaugeColorsToJson(gBed, dispSettings.bed);
  JsonObject gPfn = gauges["partFan"].to<JsonObject>();  gaugeColorsToJson(gPfn, dispSettings.partFan);
  JsonObject gAfn = gauges["auxFan"].to<JsonObject>();   gaugeColorsToJson(gAfn, dispSettings.auxFan);
  JsonObject gAfr = gauges["auxFanRight"].to<JsonObject>(); gaugeColorsToJson(gAfr, dispSettings.auxFanRight);
  JsonObject gCfn = gauges["chamberFan"].to<JsonObject>(); gaugeColorsToJson(gCfn, dispSettings.chamberFan);
  JsonObject gExh = gauges["exhaustFan"].to<JsonObject>(); gaugeColorsToJson(gExh, dispSettings.exhaustFan);
  JsonObject gCht = gauges["chamberTemp"].to<JsonObject>(); gaugeColorsToJson(gCht, dispSettings.chamberTemp);
  JsonObject gHbk = gauges["heatbreak"].to<JsonObject>(); gaugeColorsToJson(gHbk, dispSettings.heatbreak);
  JsonObject gPwr = gauges["power"].to<JsonObject>();     gaugeColorsToJson(gPwr, dispSettings.power);
  JsonObject gLyr = gauges["layer"].to<JsonObject>();     gaugeColorsToJson(gLyr, dispSettings.layer);

  // Custom gauge labels
  JsonObject glbl = disp["gaugeLabels"].to<JsonObject>();
  glbl["progress"]    = gaugeLabels.progress;
  glbl["nozzle"]      = gaugeLabels.nozzle;
  glbl["nozzleRight"] = gaugeLabels.nozzleRight;
  glbl["nozzleLeft"]  = gaugeLabels.nozzleLeft;
  glbl["bed"]         = gaugeLabels.bed;
  glbl["partFan"]     = gaugeLabels.partFan;
  glbl["auxFan"]      = gaugeLabels.auxFan;
  glbl["auxFanRight"] = gaugeLabels.auxFanRight;
  glbl["chamberFan"]  = gaugeLabels.chamberFan;
  glbl["exhaustFan"]  = gaugeLabels.exhaustFan;
  glbl["chamberTemp"] = gaugeLabels.chamberTemp;
  glbl["heatbreak"]   = gaugeLabels.heatbreak;
  glbl["power"]       = gaugeLabels.power;
  glbl["layer"]       = gaugeLabels.layer;
  glbl["clock"]       = gaugeLabels.clock;
  glbl["amsBase"]     = gaugeLabels.amsBase;
  glbl["door"]        = gaugeLabels.door;

  // Display power
  JsonObject dp = doc["displayPower"].to<JsonObject>();
  dp["finishDisplayMins"] = dpSettings.finishDisplayMins;
  dp["keepDisplayOn"] = dpSettings.keepDisplayOn;
  dp["showClockAfterFinish"] = dpSettings.showClockAfterFinish;
  dp["doorAckEnabled"] = dpSettings.doorAckEnabled;
  dp["nightModeEnabled"] = dpSettings.nightModeEnabled;
  dp["nightStartHour"] = dpSettings.nightStartHour;
  dp["nightEndHour"] = dpSettings.nightEndHour;
  dp["nightBrightness"] = dpSettings.nightBrightness;
  dp["screensaverBrightness"] = dpSettings.screensaverBrightness;

  // Network
  JsonObject net = doc["network"].to<JsonObject>();
  net["useDHCP"] = netSettings.useDHCP;
  net["staticIP"] = netSettings.staticIP;
  net["gateway"] = netSettings.gateway;
  net["subnet"] = netSettings.subnet;
  net["dns"] = netSettings.dns;
  net["timezoneIndex"] = netSettings.timezoneIndex;
  net["timezoneStr"] = netSettings.timezoneStr;
  net["use24h"] = netSettings.use24h;
  net["dateFormat"] = netSettings.dateFormat;
  net["mdnsEnabled"] = netSettings.mdnsEnabled;
  net["hostname"] = netSettings.hostname;

  // Rotation
  JsonObject rot = doc["rotation"].to<JsonObject>();
  rot["mode"] = (uint8_t)rotState.mode;
  rot["intervalMs"] = rotState.intervalMs;
  rot["split"] = rotState.splitEnabled;
  rot["splitForce"] = rotState.splitForce;

  // Button
  JsonObject btn = doc["button"].to<JsonObject>();
  btn["type"] = (uint8_t)buttonType;
  btn["pin"] = buttonPin;

  // Buzzer
  JsonObject buz = doc["buzzer"].to<JsonObject>();
  buz["enabled"] = buzzerSettings.enabled;
  buz["pin"] = buzzerSettings.pin;
  buz["quietStart"] = buzzerSettings.quietStartHour;
  buz["quietEnd"] = buzzerSettings.quietEndHour;
  buz["buttonClick"] = buzzerSettings.buttonClick;
  buz["bedCooldownAlert"] = buzzerSettings.bedCooldownAlert;
  buz["bedCooldownThresholdC"] = buzzerSettings.bedCooldownThresholdC;

  // External LED
  JsonObject led = doc["led"].to<JsonObject>();
  led["enabled"]    = ledSettings.enabled;
  led["pin"]        = ledSettings.pin;
  led["brightness"] = ledSettings.brightness;
  JsonObject ledFx = led["finish"].to<JsonObject>();
  ledFx["mode"]       = ledSettings.finishMode;
  ledFx["seconds"]    = ledSettings.finishSeconds;
  ledFx["brightness"] = ledSettings.finishBrightness;
  led["autoOnWhilePrinting"] = ledSettings.autoOnWhilePrinting;
  led["pauseBreathing"]      = ledSettings.pauseBreathing;
  led["errorStrobe"]         = ledSettings.errorStrobe;
  led["errorStrobeSeconds"]  = ledSettings.errorStrobeSeconds;

  // Tasmota power monitoring
  JsonObject tsm = doc["tasmota"].to<JsonObject>();
  tsm["currency"] = tasmotaCurrency;
  tsm["tariff"]   = tasmotaTariffPerKwh;
  JsonArray plugs = tsm["plugs"].to<JsonArray>();
  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; i++) {
    JsonObject p = plugs.add<JsonObject>();
    p["enabled"]         = tasmotaSettings[i].enabled;
    p["plugType"]        = tasmotaSettings[i].plugType;
    p["ip"]              = tasmotaSettings[i].ip;
    p["displayMode"]     = tasmotaSettings[i].displayMode;
    p["pollInterval"]    = tasmotaSettings[i].pollInterval;
    p["autoOffEnabled"]      = tasmotaSettings[i].autoOffEnabled;
    p["autoOffDelayMin"]     = tasmotaSettings[i].autoOffDelayMin;
    p["autoOffCancelOnDoor"] = tasmotaSettings[i].autoOffCancelOnDoor;
#if TASMOTA_PLUG_COUNT == 1
    p["assignedSlot"]    = tasmotaSettings[i].assignedSlot;
#endif
  }

  String json;
  serializeJsonPretty(doc, json);

  server.sendHeader("Content-Disposition", "attachment; filename=\"bambuhelper_settings.json\"");
  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
//  Settings import (JSON upload)
// ---------------------------------------------------------------------------
static String settingsImportBuf;
static bool   settingsImportOverflow = false;
static bool   otaInProgress  = false;
static bool   otaFirstChunk  = false;
static String otaError       = "";

// A >=16 MB flash chip still running the 4 MB partition table (1.75 MB OTA
// slots). Such a board can be repartitioned with a one-time full web-flasher
// flash, so "firmware too large" OTA failures get actionable guidance appended
// instead of dead-ending the user on a generic error.
static bool isUnderPartitioned() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(NULL);
  return p && p->size <= 0x1C0000 && ESP.getFlashChipSize() >= 16 * 1024 * 1024;
}
static const char REPARTITION_HINT[] =
  " This board has a 16 MB flash chip on the old 4 MB layout: back up settings"
  " (Export), then reflash once via the web flasher to repartition. OTA works"
  " normally afterwards.";
// Every OTA entry point calls disconnectBambuMqtt() up front, and only a
// reboot (success path) re-arms the connections. When an OTA attempt fails
// or is aborted, this flag requests initBambuMqtt() from handleWebServer()
// on the main loop - never directly from otaAutoTaskFn, whose 8KB stack
// cannot host the TLS userId extraction initBambuMqtt() may perform.
static volatile bool otaMqttReinitPending = false;

// Auto-update (device-initiated, HTTPUpdate from GitHub releases)
#ifdef ENABLE_OTA_AUTO
static volatile bool otaAutoInProgress = false;
static volatile int  otaAutoProgress   = 0;
static String        otaAutoStatus     = "";

static bool isExpectedOtaAssetUrl(const String& url) {
  if (url.length() == 0) return false;
  if (!url.startsWith("https://github.com/") &&
      !url.startsWith("https://objects.githubusercontent.com/") &&
      !url.startsWith("https://release-assets.githubusercontent.com/")) {
    return false;
  }

  int q = url.indexOf('?');
  String clean = q >= 0 ? url.substring(0, q) : url;
  int slash = clean.lastIndexOf('/');
  String file = slash >= 0 ? clean.substring(slash + 1) : clean;

  String prefix = "BambuHelper-" BOARD_VARIANT "-";
  return file.startsWith(prefix) && file.endsWith("-ota.bin");
}
#endif

static void gaugeColorsFromJson(JsonObject obj, GaugeColors& gc) {
  if (obj["arc"].is<const char*>())   gc.arc   = htmlToRgb565(obj["arc"]);
  if (obj["label"].is<const char*>()) gc.label = htmlToRgb565(obj["label"]);
  if (obj["value"].is<const char*>()) gc.value = htmlToRgb565(obj["value"]);
}

static void handleSettingsImportUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    settingsImportBuf = "";
    settingsImportBuf.reserve(4096);
    settingsImportOverflow = false;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (settingsImportOverflow) return;
    if (settingsImportBuf.length() + upload.currentSize > 12288) {
      settingsImportOverflow = true;
      settingsImportBuf = "";  // free memory, rest of upload is ignored
      return;
    }
    for (size_t i = 0; i < upload.currentSize; i++)
      settingsImportBuf += (char)upload.buf[i];
  }
}

static void handleSettingsImportFinish() {
  if (settingsImportOverflow) {
    settingsImportOverflow = false;
    server.send(400, "application/json",
      "{\"status\":\"error\",\"message\":\"Settings file too large (max 12 KB)\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, settingsImportBuf);
  settingsImportBuf = "";  // free memory

  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  if (!doc["_type"].is<const char*>() || strcmp(doc["_type"], "bambuhelper_settings") != 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not a BambuHelper settings file\"}");
    return;
  }

  // WiFi
  JsonObject wifi = doc["wifi"];
  if (wifi) {
    if (wifi["ssid"].is<const char*>()) strlcpy(wifiSSID, wifi["ssid"], sizeof(wifiSSID));
    if (wifi["pass"].is<const char*>()) strlcpy(wifiPass, wifi["pass"], sizeof(wifiPass));
  }

  // Printers
  // Resolve per-printer amsView with backward compat for legacy backups: per-slot
  // value wins; if absent, fall back to top-level display.amsView (old format);
  // otherwise leave default. An explicit per-printer value must never be
  // overridden by the legacy global (matters for mixed/hand-edited backups).
  JsonObject legacyDisp = doc["display"];
  bool legacyAmsViewPresent = legacyDisp && legacyDisp["amsView"].is<bool>();
  bool legacyAmsView = legacyAmsViewPresent ? legacyDisp["amsView"].as<bool>() : false;

  JsonArray pArr = doc["printers"];
  if (pArr) {
    for (uint8_t i = 0; i < MAX_PRINTERS && i < pArr.size(); i++) {
      JsonObject p = pArr[i];
      PrinterConfig& cfg = printers[i].config;
      if (p["mode"].is<uint8_t>())            cfg.mode = (ConnMode)p["mode"].as<uint8_t>();
      if (p["name"].is<const char*>())      { strlcpy(cfg.name, p["name"], sizeof(cfg.name)); utf8TrimPartial(cfg.name); }
      if (p["ip"].is<const char*>())          strlcpy(cfg.ip, p["ip"], sizeof(cfg.ip));
      if (p["serial"].is<const char*>())      strlcpy(cfg.serial, p["serial"], sizeof(cfg.serial));
      if (p["accessCode"].is<const char*>())  strlcpy(cfg.accessCode, p["accessCode"], sizeof(cfg.accessCode));
      if (p["cloudUserId"].is<const char*>()) strlcpy(cfg.cloudUserId, p["cloudUserId"], sizeof(cfg.cloudUserId));
      if (p["region"].is<uint8_t>())          cfg.region = (CloudRegion)p["region"].as<uint8_t>();
      JsonArray slots = p["gaugeSlots"];
      // Standard 2x3 grid. Accept any export with size >= 6 (legacy 8/9-byte
      // arrays from the in-development shared-extras branch get truncated -
      // their extras moved to dedicated landscapeExtras / portraitExtras
      // fields in the same export, so nothing is lost).
      if (slots && slots.size() >= 6) {
        static const uint8_t defSlots[GAUGE_SLOT_COUNT] = {
          GAUGE_PROGRESS, GAUGE_NOZZLE, GAUGE_BED,
          GAUGE_PART_FAN, GAUGE_AUX_FAN, GAUGE_CHAMBER_FAN
        };
        for (uint8_t g = 0; g < GAUGE_SLOT_COUNT; g++) {
          uint8_t v = slots[g].as<uint8_t>();
          cfg.gaugeSlots[g] = (v < GAUGE_TYPE_COUNT) ? v : defSlots[g];
        }
      }
      auto importExtras = [](JsonArray arr, uint8_t* out, uint8_t count) {
        for (uint8_t g = 0; g < count; g++) {
          if (arr && g < arr.size()) {
            uint8_t v = arr[g].as<uint8_t>();
            out[g] = (v < GAUGE_TYPE_COUNT) ? v : GAUGE_EMPTY;
          } else {
            out[g] = GAUGE_EMPTY;
          }
        }
      };
      importExtras(p["landscapeExtras"].as<JsonArray>(), cfg.landscapeExtras, LANDSCAPE_EXTRA_COUNT);
      importExtras(p["portraitExtras"].as<JsonArray>(),  cfg.portraitExtras,  PORTRAIT_EXTRA_COUNT);
      if (p["amsView"].is<bool>()) {
        cfg.amsView = p["amsView"].as<bool>();
      } else if (legacyAmsViewPresent) {
        cfg.amsView = legacyAmsView;
      }
      if (p["lightFlags"].is<uint8_t>()) cfg.lightFlags = p["lightFlags"].as<uint8_t>();
      if (p["lightDelay"].is<uint8_t>()) cfg.lightOffDelayMin = constrain(p["lightDelay"].as<int>(), 0, 60);
    }
  }

  // Display
  JsonObject disp = doc["display"];
  if (disp) {
    if (disp["brightness"].is<uint8_t>()) brightness = disp["brightness"].as<uint8_t>();
    if (disp["rotation"].is<uint8_t>())   dispSettings.rotation = disp["rotation"].as<uint8_t>();
    if (disp["bgColor"].is<const char*>())    dispSettings.bgColor = htmlToRgb565(disp["bgColor"]);
    if (disp["trackColor"].is<const char*>()) dispSettings.trackColor = htmlToRgb565(disp["trackColor"]);
    if (disp["progressBarColor"].is<const char*>()) dispSettings.progressBarColor = htmlToRgb565(disp["progressBarColor"]);
    if (disp["clockTimeColor"].is<const char*>()) dispSettings.clockTimeColor = htmlToRgb565(disp["clockTimeColor"]);
    if (disp["clockDateColor"].is<const char*>()) dispSettings.clockDateColor = htmlToRgb565(disp["clockDateColor"]);
    if (disp["clockTimeSize"].is<int>()) {
      int s = disp["clockTimeSize"].as<int>();
      dispSettings.clockTimeSize = (s >= 0 && s <= 3) ? (uint8_t)s : 0;
    }
    if (disp["clockDateSize"].is<int>()) {
      int s = disp["clockDateSize"].as<int>();
      dispSettings.clockDateSize = (s >= 0 && s <= 3) ? (uint8_t)s : 0;
    }
    if (disp["hideClockDate"].is<bool>()) dispSettings.hideClockDate = disp["hideClockDate"].as<bool>();
    if (disp["showClockInfo"].is<bool>()) dispSettings.showClockInfo = disp["showClockInfo"].as<bool>();
    if (disp["amsTrayTypes"].is<bool>())  dispSettings.amsTrayTypes = disp["amsTrayTypes"].as<bool>();
    if (disp["buttonPowerControl"].is<bool>()) dispSettings.buttonPowerControl = disp["buttonPowerControl"].as<bool>();
    if (disp["animatedBar"].is<bool>())       dispSettings.animatedBar = disp["animatedBar"].as<bool>();
    if (disp["pongClock"].is<bool>())           dispSettings.pongClock = disp["pongClock"].as<bool>();
    if (disp["smallLabels"].is<bool>())         dispSettings.smallLabels = disp["smallLabels"].as<bool>();
    if (disp["showTimeRemaining"].is<bool>())   dispSettings.showTimeRemaining = disp["showTimeRemaining"].as<bool>();
    if (disp["fanMatchPrinter"].is<bool>())     dispSettings.fanMatchPrinter = disp["fanMatchPrinter"].as<bool>();
    if (disp["hideStatusReadout"].is<bool>())   dispSettings.hideStatusReadout = disp["hideStatusReadout"].as<bool>();
    if (disp["showBatteryIndicator"].is<bool>()) dispSettings.showBatteryIndicator = disp["showBatteryIndicator"].as<bool>();
    // Gauge full-scale ranges: accept any JSON number and clamp (so crafted
    // out-of-range values are corrected, not silently ignored).
    if (disp["nozzleScaleMax"].is<int>())  dispSettings.nozzleScaleMax  = constrain(disp["nozzleScaleMax"].as<int>(),  GAUGE_NOZZLE_SCALE_MIN,  GAUGE_NOZZLE_SCALE_MAX);
    if (disp["bedScaleMax"].is<int>())     dispSettings.bedScaleMax     = constrain(disp["bedScaleMax"].as<int>(),     GAUGE_BED_SCALE_MIN,     GAUGE_BED_SCALE_MAX);
    if (disp["chamberScaleMax"].is<int>()) dispSettings.chamberScaleMax = constrain(disp["chamberScaleMax"].as<int>(), GAUGE_CHAMBER_SCALE_MIN, GAUGE_CHAMBER_SCALE_MAX);
    if (disp["powerScaleW"].is<int>())     dispSettings.powerScaleW     = constrain(disp["powerScaleW"].as<int>(),     GAUGE_POWER_SCALE_MIN,   GAUGE_POWER_SCALE_MAX);
    if (disp["gaugeSmoothing"].is<int>())  { int sm = disp["gaugeSmoothing"].as<int>(); dispSettings.gaugeSmoothing = (sm >= 0 && sm <= 3) ? (uint8_t)sm : 2; }
    if (disp["warnColor"].is<const char*>()) dispSettings.warnColor = htmlToRgb565(disp["warnColor"]);
    if (disp["warnThresholdPct"].is<int>()) dispSettings.warnThresholdPct = constrain(disp["warnThresholdPct"].as<int>(), 0, 100);
    if (disp["roundSkin"].is<int>()) { int rs = disp["roundSkin"].as<int>(); dispSettings.roundSkin = (rs >= 0 && rs <= 2) ? (uint8_t)rs : 0; }
    // Legacy disp["amsView"] is consumed in the printers block above as a fallback
    // for slots that don't have their own per-printer value.

    JsonObject gauges = disp["gauges"];
    if (gauges) {
      if (gauges["progress"].is<JsonObject>()) { JsonObject g = gauges["progress"]; gaugeColorsFromJson(g, dispSettings.progress); }
      if (gauges["nozzle"].is<JsonObject>())   { JsonObject g = gauges["nozzle"];   gaugeColorsFromJson(g, dispSettings.nozzle); }
      if (gauges["bed"].is<JsonObject>())      { JsonObject g = gauges["bed"];      gaugeColorsFromJson(g, dispSettings.bed); }
      if (gauges["partFan"].is<JsonObject>())  { JsonObject g = gauges["partFan"];  gaugeColorsFromJson(g, dispSettings.partFan); }
      if (gauges["auxFan"].is<JsonObject>())   { JsonObject g = gauges["auxFan"];   gaugeColorsFromJson(g, dispSettings.auxFan); }
      if (gauges["auxFanRight"].is<JsonObject>()){ JsonObject g = gauges["auxFanRight"]; gaugeColorsFromJson(g, dispSettings.auxFanRight); }
      if (gauges["chamberFan"].is<JsonObject>()){ JsonObject g = gauges["chamberFan"]; gaugeColorsFromJson(g, dispSettings.chamberFan); }
      if (gauges["exhaustFan"].is<JsonObject>()){ JsonObject g = gauges["exhaustFan"]; gaugeColorsFromJson(g, dispSettings.exhaustFan); }
      if (gauges["chamberTemp"].is<JsonObject>()){ JsonObject g = gauges["chamberTemp"]; gaugeColorsFromJson(g, dispSettings.chamberTemp); }
      if (gauges["heatbreak"].is<JsonObject>()){ JsonObject g = gauges["heatbreak"]; gaugeColorsFromJson(g, dispSettings.heatbreak); }
      if (gauges["power"].is<JsonObject>())    { JsonObject g = gauges["power"];     gaugeColorsFromJson(g, dispSettings.power); }
      if (gauges["layer"].is<JsonObject>())    { JsonObject g = gauges["layer"];     gaugeColorsFromJson(g, dispSettings.layer); }
    }

    JsonObject glbl = disp["gaugeLabels"];
    if (glbl) {
      struct { const char* k; char* dst; size_t len; } LB[] = {
        {"progress",    gaugeLabels.progress,    sizeof(gaugeLabels.progress)},
        {"nozzle",      gaugeLabels.nozzle,      sizeof(gaugeLabels.nozzle)},
        {"nozzleRight", gaugeLabels.nozzleRight, sizeof(gaugeLabels.nozzleRight)},
        {"nozzleLeft",  gaugeLabels.nozzleLeft,  sizeof(gaugeLabels.nozzleLeft)},
        {"bed",         gaugeLabels.bed,         sizeof(gaugeLabels.bed)},
        {"partFan",     gaugeLabels.partFan,     sizeof(gaugeLabels.partFan)},
        {"auxFan",      gaugeLabels.auxFan,      sizeof(gaugeLabels.auxFan)},
        {"auxFanRight", gaugeLabels.auxFanRight, sizeof(gaugeLabels.auxFanRight)},
        {"chamberFan",  gaugeLabels.chamberFan,  sizeof(gaugeLabels.chamberFan)},
        {"exhaustFan",  gaugeLabels.exhaustFan,  sizeof(gaugeLabels.exhaustFan)},
        {"chamberTemp", gaugeLabels.chamberTemp, sizeof(gaugeLabels.chamberTemp)},
        {"heatbreak",   gaugeLabels.heatbreak,   sizeof(gaugeLabels.heatbreak)},
        {"power",       gaugeLabels.power,       sizeof(gaugeLabels.power)},
        {"layer",       gaugeLabels.layer,       sizeof(gaugeLabels.layer)},
        {"clock",       gaugeLabels.clock,       sizeof(gaugeLabels.clock)},
        {"amsBase",     gaugeLabels.amsBase,     sizeof(gaugeLabels.amsBase)},
        {"door",        gaugeLabels.door,        sizeof(gaugeLabels.door)},
      };
      for (auto& e : LB)
        if (glbl[e.k].is<const char*>()) sanitizeGaugeLabel(glbl[e.k].as<const char*>(), e.dst, e.len);
    }
  }

  // Display power
  JsonObject dp = doc["displayPower"];
  if (dp) {
    if (dp["finishDisplayMins"].is<uint16_t>()) dpSettings.finishDisplayMins = dp["finishDisplayMins"].as<uint16_t>();
    if (dp["keepDisplayOn"].is<bool>())         dpSettings.keepDisplayOn = dp["keepDisplayOn"].as<bool>();
    if (dp["showClockAfterFinish"].is<bool>())  dpSettings.showClockAfterFinish = dp["showClockAfterFinish"].as<bool>();
    if (dp["doorAckEnabled"].is<bool>())        dpSettings.doorAckEnabled = dp["doorAckEnabled"].as<bool>();
    if (dp["nightModeEnabled"].is<bool>())      dpSettings.nightModeEnabled = dp["nightModeEnabled"].as<bool>();
    if (dp["nightStartHour"].is<uint8_t>())     dpSettings.nightStartHour = dp["nightStartHour"].as<uint8_t>();
    if (dp["nightEndHour"].is<uint8_t>())       dpSettings.nightEndHour = dp["nightEndHour"].as<uint8_t>();
    if (dp["nightBrightness"].is<uint8_t>())    dpSettings.nightBrightness = dp["nightBrightness"].as<uint8_t>();
    if (dp["screensaverBrightness"].is<uint8_t>()) dpSettings.screensaverBrightness = dp["screensaverBrightness"].as<uint8_t>();
  }

  // Network
  JsonObject net = doc["network"];
  if (net) {
    if (net["useDHCP"].is<bool>())            netSettings.useDHCP = net["useDHCP"].as<bool>();
    if (net["staticIP"].is<const char*>())    strlcpy(netSettings.staticIP, net["staticIP"], sizeof(netSettings.staticIP));
    if (net["gateway"].is<const char*>())     strlcpy(netSettings.gateway, net["gateway"], sizeof(netSettings.gateway));
    if (net["subnet"].is<const char*>())      strlcpy(netSettings.subnet, net["subnet"], sizeof(netSettings.subnet));
    if (net["dns"].is<const char*>())         strlcpy(netSettings.dns, net["dns"], sizeof(netSettings.dns));
    if (net["timezoneStr"].is<const char*>()) {
      strlcpy(netSettings.timezoneStr, net["timezoneStr"], sizeof(netSettings.timezoneStr));
      netSettings.timezoneIndex = net["timezoneIndex"] | (uint8_t)3;
    } else if (net["gmtOffsetMin"].is<int16_t>()) {
      // Backward compat: import from old format
      int16_t oldOffset = net["gmtOffsetMin"].as<int16_t>();
      const char* migrated = getDefaultTimezoneForOffset(oldOffset);
      if (migrated) strlcpy(netSettings.timezoneStr, migrated, sizeof(netSettings.timezoneStr));
    }
    if (net["use24h"].is<bool>())             netSettings.use24h = net["use24h"].as<bool>();
    if (net["dateFormat"].is<uint8_t>())     netSettings.dateFormat = net["dateFormat"].as<uint8_t>();
    if (net["mdnsEnabled"].is<bool>())        netSettings.mdnsEnabled = net["mdnsEnabled"].as<bool>();
    if (net["hostname"].is<const char*>())    sanitizeHostname(net["hostname"], netSettings.hostname, sizeof(netSettings.hostname));
  }

  // Rotation
  JsonObject rot = doc["rotation"];
  if (rot) {
    if (rot["mode"].is<uint8_t>())      rotState.mode = (RotateMode)rot["mode"].as<uint8_t>();
    if (rot["intervalMs"].is<uint32_t>()) rotState.intervalMs = rot["intervalMs"].as<uint32_t>();
    if (rot["split"].is<bool>())        rotState.splitEnabled = rot["split"].as<bool>();
    if (rot["splitForce"].is<bool>())   rotState.splitForce = rot["splitForce"].as<bool>();
  }

  // Button
  JsonObject btn = doc["button"];
  if (btn) {
    if (btn["type"].is<uint8_t>()) buttonType = (ButtonType)btn["type"].as<uint8_t>();
    if (btn["pin"].is<uint8_t>())  buttonPin = btn["pin"].as<uint8_t>();
  }

  // Buzzer
  JsonObject buz = doc["buzzer"];
  if (buz) {
    if (buz["enabled"].is<bool>())    buzzerSettings.enabled = buz["enabled"].as<bool>();
    if (buz["pin"].is<uint8_t>())     buzzerSettings.pin = buz["pin"].as<uint8_t>();
    if (buz["quietStart"].is<uint8_t>()) {
      uint8_t qs = buz["quietStart"].as<uint8_t>();
      if (qs <= 23) buzzerSettings.quietStartHour = qs;
    }
    if (buz["quietEnd"].is<uint8_t>()) {
      uint8_t qe = buz["quietEnd"].as<uint8_t>();
      if (qe <= 23) buzzerSettings.quietEndHour = qe;
    }
    if (buz["buttonClick"].is<bool>()) buzzerSettings.buttonClick = buz["buttonClick"].as<bool>();
    if (buz["bedCooldownAlert"].is<bool>()) buzzerSettings.bedCooldownAlert = buz["bedCooldownAlert"].as<bool>();
    if (buz["bedCooldownThresholdC"].is<uint8_t>()) {
      uint8_t t = buz["bedCooldownThresholdC"].as<uint8_t>();
      if (t >= 20 && t <= 80) buzzerSettings.bedCooldownThresholdC = t;
    }
  }

  // External LED
  JsonObject led = doc["led"];
  if (led) {
    if (led["enabled"].is<bool>())       ledSettings.enabled = led["enabled"].as<bool>();
    if (led["pin"].is<uint8_t>())        ledSettings.pin = led["pin"].as<uint8_t>();
    if (led["brightness"].is<uint8_t>()) ledSettings.brightness = led["brightness"].as<uint8_t>();
    JsonObject ledFx = led["finish"];
    if (ledFx) {
      if (ledFx["mode"].is<uint8_t>()) {
        uint8_t m = ledFx["mode"].as<uint8_t>();
        if (m <= 2) ledSettings.finishMode = m;
      }
      if (ledFx["seconds"].is<uint16_t>()) {
        uint16_t s = ledFx["seconds"].as<uint16_t>();
        if (s < 5) s = 5; if (s > 600) s = 600;
        ledSettings.finishSeconds = s;
      }
      if (ledFx["brightness"].is<uint8_t>()) ledSettings.finishBrightness = ledFx["brightness"].as<uint8_t>();
    }
    if (led["autoOnWhilePrinting"].is<bool>()) ledSettings.autoOnWhilePrinting = led["autoOnWhilePrinting"].as<bool>();
    if (led["pauseBreathing"].is<bool>())      ledSettings.pauseBreathing      = led["pauseBreathing"].as<bool>();
    if (led["errorStrobe"].is<bool>())         ledSettings.errorStrobe         = led["errorStrobe"].as<bool>();
    if (led["errorStrobeSeconds"].is<uint16_t>()) {
      uint16_t s = led["errorStrobeSeconds"].as<uint16_t>();
      if (s != 0 && s < 5) s = 5; if (s > 600) s = 600;
      ledSettings.errorStrobeSeconds = s;
    }
  }

  // Tasmota power monitoring
  // Accept new schema {"tasmota":{"currency":"€","plugs":[{...}, ...]}} and
  // legacy schema where "tasmota" was a single object (treat as plugs[0]).
  JsonVariant tsmRoot = doc["tasmota"];
  if (!tsmRoot.isNull()) {
    JsonArray plugs;
    JsonObject tsmObj = tsmRoot.as<JsonObject>();
    if (tsmObj && tsmObj["plugs"].is<JsonArray>()) {
      plugs = tsmObj["plugs"];
      if (tsmObj["currency"].is<const char*>()) {
        strlcpy(tasmotaCurrency, tsmObj["currency"], sizeof(tasmotaCurrency));
        utf8TrimPartial(tasmotaCurrency);
      }
      if (tsmObj["tariff"].is<float>() || tsmObj["tariff"].is<double>() || tsmObj["tariff"].is<int>()) {
        float t = tsmObj["tariff"].as<float>();
        if (t < 0.0f) t = 0.0f;
        if (t > 10.0f) t = 10.0f;
        tasmotaTariffPerKwh = t;
      } else if (plugs && plugs.size() > 0) {
        // Back-compat: lift tariff from first plug entry if global field absent.
        JsonObject p0 = plugs[0].as<JsonObject>();
        if (p0["tariffPerKwh"].is<float>() || p0["tariffPerKwh"].is<double>() || p0["tariffPerKwh"].is<int>()) {
          float t = p0["tariffPerKwh"].as<float>();
          if (t < 0.0f) t = 0.0f;
          if (t > 10.0f) t = 10.0f;
          tasmotaTariffPerKwh = t;
        }
      }
    }
    // Helper lambda: apply one plug object into tasmotaSettings[idx]
    auto applyPlug = [](uint8_t idx, JsonObject p) {
      if (idx >= TASMOTA_PLUG_COUNT) return;
      TasmotaSettings& s = tasmotaSettings[idx];
      if (p["enabled"].is<bool>())          s.enabled = p["enabled"].as<bool>();
      if (p["plugType"].is<uint8_t>())      s.plugType = (p["plugType"].as<uint8_t>() == 1) ? 1 : 0;
      if (p["ip"].is<const char*>())        strlcpy(s.ip, p["ip"], sizeof(s.ip));
      if (p["displayMode"].is<uint8_t>())   { uint8_t dm = p["displayMode"].as<uint8_t>(); s.displayMode = (dm <= 2) ? dm : 0; }
      if (p["pollInterval"].is<uint8_t>()) {
        uint8_t pi = p["pollInterval"].as<uint8_t>();
        if (pi < 10 || pi > 60) pi = 10;
        s.pollInterval = pi;
      }
      if (p["autoOffEnabled"].is<bool>())   s.autoOffEnabled = p["autoOffEnabled"].as<bool>();
      if (p["autoOffDelayMin"].is<uint8_t>()) {
        uint8_t ad = p["autoOffDelayMin"].as<uint8_t>();
        if (ad < 1 || ad > 240) ad = 10;
        s.autoOffDelayMin = ad;
      }
      if (p["autoOffCancelOnDoor"].is<bool>()) s.autoOffCancelOnDoor = p["autoOffCancelOnDoor"].as<bool>();
#if TASMOTA_PLUG_COUNT == 1
      if (p["assignedSlot"].is<uint8_t>()) {
        uint8_t a = p["assignedSlot"].as<uint8_t>();
        if (a != 255 && a >= MAX_ACTIVE_PRINTERS) a = 255;
        s.assignedSlot = a;
      }
#endif
    };
    if (plugs) {
      uint8_t i = 0;
      for (JsonObject p : plugs) {
        if (i >= TASMOTA_PLUG_COUNT) break;
        applyPlug(i++, p);
      }
    } else if (tsmObj) {
      // Legacy "tasmota" was a single object (or new schema with no "plugs"
      // array). Treat the object itself as plug 0.
      applyPlug(0, tsmObj);
    }
  }

  // Save everything to NVS
  saveSettings();
  saveRotationSettings();
  saveButtonSettings();
  saveBuzzerSettings();
  saveLedSettings();   // sanitizes pin (incl. conflict with freshly-imported buzzer/button)

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings imported. Restarting...\"}");
  scheduleRestart();
}

// ---------------------------------------------------------------------------
//  OTA firmware update
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Auto-update: FreeRTOS task that runs HTTPUpdate from a GitHub release URL
// ---------------------------------------------------------------------------
#ifdef ENABLE_OTA_AUTO
static void otaAutoTaskFn(void* param) {
  String* urlPtr = (String*)param;
  String url = *urlPtr;
  delete urlPtr;

  otaAutoStatus = "downloading";

  // Pause IDLE0 watchdog: a sustained TLS download keeps the WiFi task on
  // core 0 hot enough that IDLE0 can be starved past the 5 s TWDT window,
  // especially on CYD (original ESP32 without S3's crypto accelerator).
  // The OTA task itself runs pinned to core 1 (see handleOtaAuto).
  disableCore0WDT();

  WiFiClientSecure client;
  client.setCACertBundle(rootca_crt_bundle_start);

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) otaAutoProgress = (int)((cur / (float)total) * 100);
  });

  t_httpUpdate_return ret = httpUpdate.update(client, url);

  switch (ret) {
    case HTTP_UPDATE_OK:
      otaAutoProgress = 100;
      otaAutoStatus = "done";
      Serial.println("OTA auto: success, scheduling restart");
      scheduleRestart(4000);  // let JS poller detect "done" before reboot
      break;
    case HTTP_UPDATE_NO_UPDATES:
      otaAutoStatus = "already_current";
      otaMqttReinitPending = true;  // no reboot coming - restore MQTT
      break;
    case HTTP_UPDATE_FAILED:
    default: {
      int lastErr = httpUpdate.getLastError();
      String err = httpUpdate.getLastErrorString();
      Serial.printf("OTA auto: failed (%d) %s\n", lastErr, err.c_str());
      // Retry once with setInsecure() in case CA bundle fails. Deterministic
      // failures are excluded: HTTP_UE_TOO_LESS_SPACE (firmware exceeds the
      // OTA slot) and HTTP_UE_BIN_FOR_WRONG_FLASH (image header mismatch)
      // fail identically on any transport.
      if (lastErr != HTTP_UE_TOO_LESS_SPACE && lastErr != HTTP_UE_BIN_FOR_WRONG_FLASH) {
        client.setInsecure();
        ret = httpUpdate.update(client, url);
        if (ret == HTTP_UPDATE_OK) {
          otaAutoProgress = 100;
          otaAutoStatus = "done";
          scheduleRestart(4000);
          break;
        }
      }
      otaAutoStatus = "failed: " + err;
      if (lastErr == HTTP_UE_TOO_LESS_SPACE && isUnderPartitioned())
        otaAutoStatus += REPARTITION_HINT;
      otaMqttReinitPending = true;  // no reboot coming - restore MQTT
      break;
    }
  }

  enableCore0WDT();
  otaAutoInProgress = false;
  vTaskDelete(nullptr);
}

static void handleOtaAuto() {
  if (otaAutoInProgress) {
    server.send(409, "application/json", "{\"error\":\"Update already in progress\"}");
    return;
  }

  String url = server.arg("url");
  if (!isExpectedOtaAssetUrl(url)) {
    server.send(400, "application/json", "{\"error\":\"Missing or invalid url\"}");
    return;
  }

  disconnectBambuMqtt();

  otaAutoInProgress = true;
  otaAutoProgress   = 0;
  otaAutoStatus     = "starting";

  String* urlHeap = new String(url);
  // Pin to core 1: the WiFi task lives on core 0 and a concurrent TLS
  // download here would compete for the same core, starving IDLE0 and
  // tripping the task watchdog mid-flash on slower boards (CYD).
  xTaskCreatePinnedToCore(otaAutoTaskFn, "otaAuto", 8192, (void*)urlHeap, 5, nullptr, 1);

  server.send(200, "application/json", "{\"status\":\"started\"}");
}

bool        isOtaAutoInProgress() { return otaAutoInProgress; }
int         getOtaAutoProgress()  { return otaAutoProgress; }
const char* getOtaAutoStatus()    { return otaAutoStatus.c_str(); }

static void handleOtaStatus() {
  JsonDocument doc;
  doc["inProgress"] = (bool)otaAutoInProgress;
  doc["progress"]   = (int)otaAutoProgress;
  doc["status"]     = otaAutoStatus;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
#endif // ENABLE_OTA_AUTO

static void handleOtaUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaError = "";
    otaInProgress = true;
    otaFirstChunk = true;
    Serial.printf("OTA: start, file=%s\n", upload.filename.c_str());

    disconnectBambuMqtt();

    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
      otaError = "No OTA partition found";
      Serial.println("OTA: no OTA partition found");
      otaInProgress = false;
      return;
    }
    Serial.printf("OTA: firmware upload started, partition size: %u bytes\n", partition->size);

    if (!Update.begin(partition->size)) {
      otaError = "OTA begin failed: " + String(Update.errorString());
      Serial.printf("OTA: begin failed: %s\n", otaError.c_str());
      otaInProgress = false;
      return;
    }

    if (server.hasHeader("X-MD5")) {
      String md5 = server.header("X-MD5");
      if (md5.length() == 32) {
        Update.setMD5(md5.c_str());
        Serial.printf("OTA: MD5 checksum set: %s\n", md5.c_str());
      }
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaInProgress) return;

    // Validate ESP32 magic byte on first chunk
    if (otaFirstChunk && upload.currentSize > 0) {
      otaFirstChunk = false;
      if (upload.buf[0] != 0xE9) {
        otaError = "Invalid firmware file";
        Update.abort();
        otaInProgress = false;
        return;
      }
    }

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError = Update.errorString();
      if (Update.getError() == UPDATE_ERROR_SPACE && isUnderPartitioned())
        otaError += REPARTITION_HINT;
      Update.abort();
      otaInProgress = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaInProgress) return;

    if (Update.end(true)) {
      Serial.printf("OTA: success, %u bytes\n", upload.totalSize);
    } else {
      otaError = Update.errorString();
      if (Update.getError() == UPDATE_ERROR_SPACE && isUnderPartitioned())
        otaError += REPARTITION_HINT;
      Serial.printf("OTA: end failed: %s\n", otaError.c_str());
    }
    otaInProgress = false;

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaInProgress = false;
    // Client dropped the connection: handleOtaFinish() never runs for this
    // request, so request the MQTT re-init here.
    otaMqttReinitPending = true;
    Serial.println("OTA: aborted");
  }
}

static void handleOtaFinish() {
  if (otaError.length() > 0) {
    String msg = "{\"status\":\"error\",\"message\":\"" + otaError + "\"}";
    server.send(400, "application/json", msg);
    otaError = "";
    otaMqttReinitPending = true;  // failed update means no reboot - restore MQTT
    return;
  }
  server.send(200, "application/json",
    "{\"status\":\"ok\",\"message\":\"Update successful. Restarting...\"}");
  scheduleRestart(1500);
}

// Captive portal: redirect any unknown request to root
// Android/Samsung check /generate_204 expecting 204 — returning 302 triggers popup.
// Apple checks /hotspot-detect.html — non-"Success" body triggers popup.
// Using 302 + no-cache for all unknown paths ensures popup on all platforms.
static void handleNotFound() {
  if (isAPMode()) {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not Found");
  }
}

// ---------------------------------------------------------------------------
//  Init & handle
// ---------------------------------------------------------------------------
static void handleCaptiveDetect() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302, "text/plain", "");
}

void initWebServer() {
  // Captive portal detection endpoints (must be before onNotFound)
  server.on("/generate_204", HTTP_GET, handleCaptiveDetect);        // Android/Samsung
  server.on("/gen_204", HTTP_GET, handleCaptiveDetect);              // Android alt
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveDetect);      // Windows
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveDetect);  // Apple
  server.on("/canonical.html", HTTP_GET, handleCaptiveDetect);       // Firefox

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save/wifi", HTTP_POST, handleSaveWifi);
  server.on("/save/printer", HTTP_POST, handleSavePrinter);
  server.on("/save/gaugelayout", HTTP_POST, handleSaveGaugeLayout);
  server.on("/save/rotation", HTTP_POST, handleSaveRotation);
  server.on("/save/power", HTTP_POST, handleSavePower);
  server.on("/power/config", HTTP_GET, handleGetPowerConfig);
  server.on("/power/stats",  HTTP_GET, handleGetPowerStats);
  server.on("/power/control", HTTP_POST, handlePowerControl);
  server.on("/buzzer/test", HTTP_POST, handleBuzzerTest);
  server.on("/led/preview", HTTP_POST, handleLedPreview);
  server.on("/led/test",    HTTP_POST, handleLedTest);
  server.on("/printer/config", HTTP_GET, handlePrinterConfig);
  server.on("/printer/clear", HTTP_POST, handleClearPrinter);
  server.on("/light/config", HTTP_POST, handleLightConfig);
  server.on("/light/set", HTTP_POST, handleLightSet);
  server.on("/apply", HTTP_POST, handleApply);
  server.on("/brightness", HTTP_GET, handleBrightnessPreview);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/api/timezones", HTTP_GET, handleTimezones);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/debug/toggle", HTTP_POST, handleDebugToggle);
  server.on("/save/toggle", HTTP_POST, handleToggleSetting);
  server.on("/cloud/logout", HTTP_POST, handleCloudLogout);
  server.on("/lan/scan", HTTP_POST, handleLanScan);
  server.on("/lan/scan", HTTP_GET, handleLanScan);
  server.on("/settings/export", HTTP_GET, handleSettingsExport);
  server.on("/settings/import", HTTP_POST, handleSettingsImportFinish, handleSettingsImportUpload);
  server.on("/ota/upload", HTTP_POST, handleOtaFinish, handleOtaUpload);
#ifdef ENABLE_OTA_AUTO
  server.on("/ota/auto",   HTTP_POST, handleOtaAuto);
  server.on("/ota/status", HTTP_GET,  handleOtaStatus);
#endif
  server.onNotFound(handleNotFound);
  const char* otaHeaders[] = {"X-MD5"};
  server.collectHeaders(otaHeaders, 1);
  server.begin();
  Serial.println("Web server started on port 80");
}

void handleWebServer() {
  server.handleClient();

  if (otaMqttReinitPending) {
    bool otaBusy = otaInProgress;
#ifdef ENABLE_OTA_AUTO
    otaBusy = otaBusy || otaAutoInProgress;
#endif
    if (!otaBusy) {
      otaMqttReinitPending = false;
      Serial.println("OTA: update failed/aborted, re-initializing MQTT");
      initBambuMqtt();
    }
  }

  if (pendingRestartAt && millis() >= pendingRestartAt) {
    Serial.println("Deferred restart triggered");
    ESP.restart();
  }
}
