#include "web_server.h"
#include "web_template.h"
#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "cloud_login.h"
// Gzipped portal assets, generated at build time from web/app.css and
// web/app.js. Included here and nowhere else - the arrays are static.
#include "web_assets_gz.h"
#include "ssdp_discovery.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "display_edge_glow.h"
#include "config.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "timezones.h"
#include "tasmota.h"
#include "clock_mode.h"
#include "clock_pong.h"
#include "hms_lookup.h"
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
  if (server.hasArg("clr_eta"))   dispSettings.etaColor = htmlToRgb565(server.arg("clr_eta").c_str());
  if (server.hasArg("clr_fin"))   dispSettings.finishColor = htmlToRgb565(server.arg("clr_fin").c_str());
  if (server.hasArg("clr_stok"))  dispSettings.statusOkColor = htmlToRgb565(server.arg("clr_stok").c_str());
  if (server.hasArg("clr_pname")) dispSettings.printerNameColor = htmlToRgb565(server.arg("clr_pname").c_str());
  if (server.hasArg("clr_txt"))   dispSettings.textColor = htmlToRgb565(server.arg("clr_txt").c_str());
  if (server.hasArg("clr_txtd"))  dispSettings.textDimColor = htmlToRgb565(server.arg("clr_txtd").c_str());
  if (server.hasArg("clr_dorc"))  dispSettings.doorClosedColor = htmlToRgb565(server.arg("clr_dorc").c_str());
  if (server.hasArg("clr_doro"))  dispSettings.doorOpenColor = htmlToRgb565(server.arg("clr_doro").c_str());
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
  dpSettings.finishShowTime = server.hasArg("fintm");
  dispSettings.animatedBar = server.hasArg("abar");
  dispSettings.pongClock = server.hasArg("pong");
  dispSettings.smallLabels = server.hasArg("slbl");
  if (server.hasArg("timem")) {
    int tm = server.arg("timem").toInt();
    if (tm >= 0 && tm <= 2) dispSettings.timeDisplayMode = (uint8_t)tm;
  }
  dispSettings.fanMatchPrinter = server.hasArg("fanmp");

  // Edge glow
  if (server.hasArg("glowm")) {
    int gm = server.arg("glowm").toInt();
    if (gm >= 0 && gm <= 2) dispSettings.glowMode = (uint8_t)gm;
    if (dispSettings.glowMode == 0) glowDismiss();  // switched off mid-animation
  }
  if (server.hasArg("glow_clr")) dispSettings.glowColor = htmlToRgb565(server.arg("glow_clr").c_str());
  if (server.hasArg("glows")) {
    int gs = server.arg("glows").toInt();
    if (gs >= 0 && gs <= 2) dispSettings.glowStyle = (uint8_t)gs;
  }
  if (server.hasArg("glowd")) {
    int gd = server.arg("glowd").toInt();
    if (gd >= 0 && gd <= 2) dispSettings.glowDuration = (uint8_t)gd;
  }

  // Printer errors. "hmsauto" is always posted when the section exists, so it
  // doubles as the presence marker: without it the whole block is left alone,
  // which is what a board that never renders the section must do (otherwise the
  // two checkbox reads below would clear settings nobody touched). The four
  // alert checkboxes ride one mask that is always sent, zero included - four
  // unchecked boxes posting nothing would leave stale bits behind.
#if HAS_HMS_WEB_UI
  if (server.hasArg("hmsauto")) {
    int ap = server.arg("hmsauto").toInt();
    if (ap >= 0 && ap <= 2) dispSettings.hmsAutoPresent = (uint8_t)ap;
    dispSettings.hmsEnabled = server.hasArg("hmsen");
    dispSettings.hmsSeverityAll = server.hasArg("hmssev");
    dispSettings.hmsLookupOnline = server.hasArg("hmsonl");
    if (server.hasArg("hmsmask"))
      dispSettings.hmsAlertMask = (uint8_t)(server.arg("hmsmask").toInt() & 0x0F);
  }
#endif

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

  bool tokenStoreFailed = false;
  if (isCloudMode(cfg.mode)) {
    if (server.hasArg("serial")) strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("pname")) { strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name)); utf8TrimPartial(cfg.name); }
    // Save token if provided
    if (server.hasArg("token") && server.arg("token").length() > 0) {
      tokenStoreFailed = !saveCloudToken(server.arg("token").c_str());
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
  if (tokenStoreFailed) {
    // The write itself failed (full or fragmented NVS) - without this line the
    // portal keeps saying "No token set" however often the user pastes, with
    // nothing to explain why.
    uint16_t u, f, t;
    getNvsUsage(u, f, t);
    warnings += "The cloud token could not be stored - settings storage is full (" +
                String(u) + " of " + String(t) +
                " entries used). Export settings, factory-reset, re-import, then paste the token again. ";
  }
  if (isCloudMode(cfg.mode)) {
    if (strlen(cfg.serial) == 0)
      warnings += "Serial number is required for cloud mode. ";
    if (strlen(cfg.cloudUserId) == 0 && !tokenStoreFailed)
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
  for (uint8_t g = 0; g < IDLE_SLOT_COUNT;        g++) {
    readSlotArg("is", g, cfg.idleSlots[g]);
    // The Ready / Print Complete screens redraw only on change, while the
    // camera tile paints on its own cadence and camera_client only streams for
    // a type parked in gaugeSlots - it would sit there as a stale frame.
    if (cfg.idleSlots[g] == GAUGE_CAMERA) cfg.idleSlots[g] = GAUGE_EMPTY;
  }
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
  // Bound by the config array, not by MAX_ACTIVE_PRINTERS: the two differ on a
  // low-RAM board, which still keeps MAX_PRINTERS NVS slots but only runs two
  // MQTT connections. Clamping to 0 made /status?slot=2 there answer about slot
  // 0, and the portal's error card - which polls 0..3 blind, having no way to
  // see a board-specific limit - listed the same printer three times.
  if (slot >= MAX_PRINTERS) slot = 0;

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
  doc["cali"] = isCalibrationPrint(st);  // current/last job is a calibration print (issue #149)
#if HAS_HMS_UI
  // Printer errors. Emitted only when there is something to report, so the 3 s
  // poll stays small on a healthy printer. The portal card consumes these -
  // keep the names stable.
  //
  // Baseline membership rides each entry rather than a second array: it is the
  // same information in fewer bytes, and the card wants it per row anyway.
  if (dispSettings.hmsEnabled && (st.printError != 0 || st.hmsCount > 0)) {
    char codeBuf[HMS_CODE_FULL_STR_LEN];
    if (st.printError != 0) {
      printErrorFormatCode(st.printError, codeBuf, sizeof(codeBuf));
      doc["printError"] = codeBuf;
      const char* peText = printErrorLookupText(st.printError);
      if (peText) doc["printErrorText"] = peText;
      // The card says "Canceled", not "error", for a stop the user asked for.
      if (printErrorIsCancel(st.printError)) doc["printErrorCancel"] = true;
    }
    if (st.hmsCount > 0) {
      JsonArray arr = doc["hms"].to<JsonArray>();
      for (uint8_t i = 0; i < st.hmsCount; i++) {
        JsonObject e = arr.add<JsonObject>();
        // Bambu's own full notation - this is the string a user copies into a
        // search box, and "0500_0100_0002_000B" matched nothing (issue #164).
        hmsFormatCodeFull(st.hms[i].attr, st.hms[i].code, codeBuf, sizeof(codeBuf));
        e["code"] = codeBuf;
        e["sev"] = hmsSeverityOf(st.hms[i].code);
        e["module"] = hmsModuleLabel(st.hms[i].attr);
        if (hmsIsBaseline(st, st.hms[i].attr, st.hms[i].code)) e["baseline"] = true;
        // Only on boards carrying the table. Elsewhere the card falls back to
        // the published mirror, which the browser can reach and we cannot.
        const char* text = hmsLookupText(st.hms[i].attr, st.hms[i].code);
        if (text) e["text"] = text;
      }
      if (st.hmsOverflow) doc["hmsOverflow"] = (uint8_t)(st.hmsTotal - st.hmsCount);
    }
    const ErrorBadge badge = errorBadgeFor(st);
    if (badge.active) doc["errSev"] = badge.severity;
  }
#endif

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
#if HAS_HMS_UI
    // Printer errors. The portal card and /status get their own shaped fields
    // later; this is the raw diagnostic view.
    char codeBuf[HMS_CODE_FULL_STR_LEN];
    if (st.printError != 0) {
      printErrorFormatCode(st.printError, codeBuf, sizeof(codeBuf));
      p["print_error"] = codeBuf;
      const char* peText = printErrorLookupText(st.printError);
      if (peText) p["print_error_text"] = peText;
    }
    if (st.hmsCount > 0) {
      JsonArray hmsArr = p["hms"].to<JsonArray>();
      for (uint8_t k = 0; k < st.hmsCount; k++) {
        JsonObject e = hmsArr.add<JsonObject>();
        hmsFormatCodeFull(st.hms[k].attr, st.hms[k].code, codeBuf, sizeof(codeBuf));
        e["code"] = codeBuf;
        e["sev"] = hmsSeverityOf(st.hms[k].code);
        e["module"] = hmsModuleLabel(st.hms[k].attr);
        e["baseline"] = hmsIsBaseline(st, st.hms[k].attr, st.hms[k].code);
        const char* text = hmsLookupText(st.hms[k].attr, st.hms[k].code);
        if (text) e["text"] = text;
      }
      p["hms_total"] = st.hmsTotal;
      p["hms_overflow"] = st.hmsOverflow;
      p["hms_worst_sev"] = st.hmsWorstSeverity;
    }
    // Emitted outside the hmsCount guard on purpose: a report where every code
    // was undescribed leaves hms[] empty, and that is precisely the case someone
    // asks about ("the printer shows a code, BambuHelper shows none"). The codes
    // themselves ride along, not just the count - a diagnostic dump is the only
    // way to answer that question without a USB cable, so it has to name them.
    p["hms_suppressed"] = st.hmsSuppressed;
    if (st.hmsSuppressed > 0) {
      JsonArray sup = p["hms_suppressed_codes"].to<JsonArray>();
      const uint8_t n = st.hmsSuppressed < HMS_SUPPRESSED_MAX
                          ? st.hmsSuppressed : HMS_SUPPRESSED_MAX;
      for (uint8_t k = 0; k < n; k++) {
        hmsFormatCodeFull(st.hmsSuppressedCodes[k].attr,
                          st.hmsSuppressedCodes[k].code, codeBuf, sizeof(codeBuf));
        sup.add(codeBuf);
      }
    }
    p["hms_baseline_n"] = st.hmsBaselineCount;
    if (st.hmsBaselineSaturated) p["hms_baseline_saturated"] = true;
    // What the on-screen badge resolves to, so a "why is nothing showing"
    // report can be answered without a panel in front of you.
    {
      const ErrorBadge eb = errorBadgeFor(st);
      p["badge_active"] = eb.active;
      if (eb.active) p["badge_sev"] = eb.severity;
      if (printerWasCanceled(st)) p["canceled"] = true;
    }
#endif
  }

  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["debug_log"] = mqttDebugLog;
  {
    // Settings-storage pressure. free_entries overcounts (erased-but-not-yet-
    // collected entries ride along), so used/total is the number a support
    // thread should quote.
    uint16_t u, f, t;
    getNvsUsage(u, f, t);
    doc["nvs_used"]  = u;
    doc["nvs_free"]  = f;
    doc["nvs_total"] = t;
  }
#if HAS_HMS_UI
  {
    const char* tv = hmsTableVersion();
    doc["hms_table_ver"] = tv ? tv : "none";
    // The key set behind the suppression rule. Present even where hms_table_ver
    // is "none", which is the whole point of it being a separate field.
    const char* kv = hmsKnownVersion();
    doc["hms_known_ver"] = kv ? kv : "none";
  }
#endif

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
  else if (key == "fintm")   dpSettings.finishShowTime = on;
  else if (key == "abar")    dispSettings.animatedBar = on;
  else if (key == "pong")    dispSettings.pongClock = on;
  else if (key == "slbl")    dispSettings.smallLabels = on;
  else if (key == "timem")   dispSettings.timeDisplayMode = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
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
  else if (key == "glowm") {
    dispSettings.glowMode = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
    if (dispSettings.glowMode == 0) glowDismiss();  // switched off mid-animation
  }
  else if (key == "glows")   dispSettings.glowStyle = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
  else if (key == "glowd")   dispSettings.glowDuration = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
#if HAS_HMS_WEB_UI
  else if (key == "hmsen")   dispSettings.hmsEnabled = on;
  else if (key == "hmssev")  dispSettings.hmsSeverityAll = on;
  else if (key == "hmsauto") dispSettings.hmsAutoPresent = (uint8_t)constrain(server.arg("val").toInt(), 0, 2);
  else if (key == "hmsmask") dispSettings.hmsAlertMask = (uint8_t)(constrain(server.arg("val").toInt(), 0, 15));
  // Browser-side only - the device never fetches the list, so nothing on the
  // panel has to be repainted for this one.
  else if (key == "hmsonl")  dispSettings.hmsLookupOnline = on;
#endif
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
  // "fintm" joins these because it rewrites text already on screen: the finish
  // headline is painted only under forceRedraw, so without a re-render the
  // toggle would appear to do nothing until the next print.
  // "hmsen" / "hmssev" join these because they change what the state badge
  // says without any printer state moving, and every badge site is cached.
  if (key == "invcol" || key == "slbl" || key == "abar" || key == "timem" ||
      key == "fintm" || key == "hmsen" || key == "hmssev") applyDisplaySettings();
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

// ---------------------------------------------------------------------------
//  Static portal assets
//
//  Served straight from PROGMEM, still gzipped - the browser inflates them. The
//  URLs carry a hash of the file's own bytes, so `immutable` is safe: any change
//  to the asset changes its URL and the cached copy is simply never asked for
//  again.
// ---------------------------------------------------------------------------
const char* webAssetCssVersion() { return WEB_APP_CSS_VER; }
const char* webAssetJsVersion()  { return WEB_APP_JS_VER; }

static void sendGzipAsset(const char* contentType, const uint8_t* data, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.send_P(200, contentType, (PGM_P)data, len);
}

static void handleAppCss() { sendGzipAsset("text/css", WEB_APP_CSS_GZ, WEB_APP_CSS_GZ_LEN); }
static void handleAppJs()  { sendGzipAsset("application/javascript", WEB_APP_JS_GZ, WEB_APP_JS_GZ_LEN); }

static void handleCloudLogout() {
  clearCloudToken();
  // NVS is only half of it: the sign-in module still holds the live site
  // session cookie, the 2FA challenge and a state that answers "Signed in."
  // until the next reboot.
  cloudLoginReset();
  server.send(200, "text/plain", "OK");
}

// List the printers bound to the stored token's account, so the portal can
// offer a picker instead of a serial the user has to transcribe. Available with
// a pasted token too, not only after an on-device sign-in.
static void handleCloudPrinters() {
  char token[1200];
  if (!loadCloudToken(token, sizeof(token))) {
    server.send(200, "application/json",
                "{\"printers\":[],\"message\":\"Sign in or paste a token first.\"}");
    return;
  }

  CloudRegion region = printers[0].config.region;
  if (server.hasArg("region")) {
    String r = server.arg("region");
    region = (r == "cn") ? REGION_CN : (r == "eu" ? REGION_EU : REGION_US);
  }

  String response;
  if (!cloudFetchDeviceList(token, region, response)) {
    server.send(200, "application/json",
                "{\"printers\":[],\"message\":\"Bambu refused the request - the token may have expired.\"}");
    return;
  }

  // The bind payload carries far more per device than the picker needs, and a
  // busy account can make it big, so only the four fields are parsed out.
  JsonDocument filter;
  filter["devices"][0]["dev_id"] = true;
  filter["devices"][0]["name"] = true;
  filter["devices"][0]["dev_product_name"] = true;
  filter["devices"][0]["dev_model_name"] = true;
  filter["devices"][0]["online"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) {
    server.send(200, "application/json",
                "{\"printers\":[],\"message\":\"Could not read the account's printer list.\"}");
    return;
  }

  JsonDocument out;
  JsonArray arr = out["printers"].to<JsonArray>();
  for (JsonObject dev : doc["devices"].as<JsonArray>()) {
    const char* serial = dev["dev_id"];
    if (!serial || strlen(serial) == 0) continue;
    JsonObject p = arr.add<JsonObject>();
    p["serial"] = serial;
    p["name"]   = dev["name"].is<const char*>() ? (const char*)dev["name"] : serial;
    const char* model = dev["dev_product_name"].is<const char*>()
                          ? (const char*)dev["dev_product_name"]
                          : (const char*)dev["dev_model_name"];
    p["model"]  = model ? model : "";
    p["online"] = dev["online"].as<bool>();
  }

  String body;
  serializeJson(out, body);
  server.send(200, "application/json", body);
}

#if HAS_CLOUD_LOGIN

// Signing in mints a fresh token, so every cloud slot needs its userId derived
// again - it is part of the MQTT topic.
static void refreshCloudUserIds() {
  char tokenBuf[1200];
  if (!loadCloudToken(tokenBuf, sizeof(tokenBuf))) return;

  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    PrinterConfig& cfg = printers[i].config;
    if (!isCloudMode(cfg.mode)) continue;
    if (!cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId))) {
      cloudFetchUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId), cfg.region);
    }
  }
  saveSettings();
}

static const char* cloudLoginStateName() {
  switch (cloudLoginState()) {
    case CLOUD_LOGIN_NEED_TFA:        return "need_tfa";
    case CLOUD_LOGIN_NEED_EMAIL_CODE: return "need_email_code";
    case CLOUD_LOGIN_OK:              return "ok";
    case CLOUD_LOGIN_FAILED:          return "failed";
    default:                          return "idle";
  }
}

static void sendCloudLoginState() {
  JsonDocument doc;
  doc["state"]   = cloudLoginStateName();
  doc["message"] = cloudLoginMessage();
  doc["failed"]  = cloudLoginLastFailed();

  char email[96];
  doc["email"] = loadCloudEmail(email, sizeof(email)) ? email : "";
  doc["saved_password"] = cloudLoginCanAutoRefresh();

  char token[1200];
  doc["has_token"] = loadCloudToken(token, sizeof(token));

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Step 1. `mode=code` mails a code and never sees a password; `mode=password`
// posts the password once. It is only persisted when the caller asked for it
// AND the account signed in without a second factor - a stored password is
// useless for silent refresh otherwise.
static void sendCloudLoginError(const char* message) {
  JsonDocument doc;
  doc["state"]   = "failed";
  doc["failed"]  = true;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleCloudLoginStart() {
  String email = server.arg("email");
  email.trim();
  if (email.length() == 0) {
    sendCloudLoginError("Enter your Bambu account email.");
    return;
  }
  // Both credentials are read back out of fixed buffers. Storing something
  // longer would truncate on load and then fail every silent renewal for good,
  // reported as a wrong password rather than as the length problem it is.
  if (email.length() > CLOUD_EMAIL_MAX) {
    sendCloudLoginError("That email address is too long for this device to store.");
    return;
  }

  if (server.arg("mode") == "code") {
    cloudLoginRequestEmailCode(email.c_str());
    sendCloudLoginState();
    return;
  }

  String password = server.arg("password");
  if (password.length() == 0) {
    sendCloudLoginError("Enter your password.");
    return;
  }

  const bool save = (server.arg("save") == "1");
  if (save && password.length() > CLOUD_PASSWORD_MAX) {
    sendCloudLoginError("That password is too long for this device to remember. "
                        "Sign in without saving it, or use an emailed code.");
    return;
  }

  cloudLoginWithPassword(email.c_str(), password.c_str());

  if (cloudLoginState() == CLOUD_LOGIN_OK) {
    // Not saving means not keeping: without this, signing into a second account
    // leaves the first account's password behind, now paired with the new
    // email, and the renewal path posts that mismatched pair every 15 minutes.
    if (save) saveCloudPassword(password.c_str());
    else      clearCloudPassword();
    refreshCloudUserIds();
  }
  sendCloudLoginState();
}

// Step 2: the authenticator code or the emailed one, whichever is pending.
static void handleCloudLoginCode() {
  String code = server.arg("code");
  code.trim();
  if (code.length() == 0) {
    sendCloudLoginState();
    return;
  }

  cloudLoginSubmitCode(code.c_str());
  if (cloudLoginState() == CLOUD_LOGIN_OK) refreshCloudUserIds();
  sendCloudLoginState();
}

static void handleCloudLoginStatus() {
  sendCloudLoginState();
}

// Reachability check for support: proves whether this device can talk to the
// Bambu sign-in service, using throwaway credentials only.
static void handleCloudSelfTest() {
  String result;
  cloudLoginSelfTest(result);
  Serial.print("CLOUD selftest: ");
  Serial.println(result);
  server.send(200, "application/json", result);
}

#endif // HAS_CLOUD_LOGIN

// "Test" button next to the edge-glow settings: preview the configured effect
// for ~5 s on whatever screen is up. Accepts the picker's current color so the
// preview matches even before the user hits Save (applied live, not persisted).
static void handleGlowTest() {
  if (server.hasArg("clr")) dispSettings.glowColor = htmlToRgb565(server.arg("clr").c_str());
  glowStartTest(rotState.displayIndex);
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
#if BOARD_HAS_CAMERA
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
  JsonArray islot = doc["idleSlots"].to<JsonArray>();
  for (uint8_t g = 0; g < IDLE_SLOT_COUNT;       g++) islot.add(cfg.idleSlots[g]);
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

// Parse an "#rrggbb" or "rrggbb" form value into 0xRRGGBB, falling back to the
// saved value when the argument is missing or malformed.
static uint32_t ledColorArg(const char *arg, uint32_t fallback) {
  if (!server.hasArg(arg)) return fallback;
  String s = server.arg(arg);
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.length() != 6) return fallback;
  for (uint8_t i = 0; i < 6; i++) if (!isHexadecimalDigit(s[i])) return fallback;
  return (uint32_t)strtoul(s.c_str(), NULL, 16) & 0xFFFFFF;
}

// Live LED preview from web UI. Validates pin range as int before casting to
// uint8_t (avoids 300 -> 44 wraparound). On invalid pin we shut the preview
// off so the user doesn't see a "ghost" LED still lit on the previous pin.
static void handleLedPreview() {
  bool en = server.hasArg("en") ? (server.arg("en") == "1") : ledSettings.enabled;

  uint8_t drv = ledSettings.driver;
  if (server.hasArg("drv")) {
    int v = server.arg("drv").toInt();
    if (v >= 0 && v <= LED_DRV_PIXEL) drv = (uint8_t)v;
  }

  // Red / green / blue pins. Green and blue only matter for LED_DRV_RGB, but
  // they are range-checked unconditionally so a stray value can never reach
  // previewLed() as a wrapped uint8_t.
  int rawPin = server.hasArg("pin") ? server.arg("pin").toInt() : ledSettings.pin;
  int rawG   = server.hasArg("ping") ? server.arg("ping").toInt() : ledSettings.pinG;
  int rawB   = server.hasArg("pinb") ? server.arg("pinb").toInt() : ledSettings.pinB;
  const bool rgb = (drv == LED_DRV_RGB);
  if (rawPin < 1 || rawPin > 48 ||
      (rgb && (rawG < 1 || rawG > 48 || rawB < 1 || rawB > 48))) {
    previewLed(false, LED_DRV_SINGLE, 0, 0, 0, false, 0, 0);
    server.send(400, "application/json", "{\"error\":\"pin out of range\"}");
    return;
  }

  uint8_t br = ledSettings.brightness;
  if (server.hasArg("br")) {
    int v = server.arg("br").toInt();
    if (v < 0) v = 0; if (v > 255) v = 255;
    br = (uint8_t)v;
  }

  const bool anode = server.hasArg("anode") ? (server.arg("anode") == "1")
                                            : ledSettings.commonAnode;
  const uint32_t col = ledColorArg("col", ledSettings.colorIdle);

  previewLed(en, drv, (uint8_t)rawPin, (uint8_t)rawG, (uint8_t)rawB, anode, br, col);
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

  if (!ledTriggerTestEffect(md, sec, br, ledColorArg("col", ledSettings.colorFinished))) {
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

  // Status LED — must be parsed AFTER button + buzzer so sanitizeLedPin()
  // sees the freshly-applied buttonPin and buzzerSettings.pin when checking
  // for conflicts.
  if (server.hasArg("leden"))  ledSettings.enabled = (server.arg("leden") == "1");
  if (server.hasArg("leddrv")) {
    int v = server.arg("leddrv").toInt();
    if (v >= 0 && v <= LED_DRV_PIXEL) ledSettings.driver = (uint8_t)v;
  }
  if (server.hasArg("ledpin")) {
    int lp = server.arg("ledpin").toInt();
    if (lp > 0 && lp <= 48) ledSettings.pin = (uint8_t)lp;
  }
  if (server.hasArg("ledping")) {
    int lp = server.arg("ledping").toInt();
    if (lp > 0 && lp <= 48) ledSettings.pinG = (uint8_t)lp;
  }
  if (server.hasArg("ledpinb")) {
    int lp = server.arg("ledpinb").toInt();
    if (lp > 0 && lp <= 48) ledSettings.pinB = (uint8_t)lp;
  }
  if (server.hasArg("ledanode")) ledSettings.commonAnode = (server.arg("ledanode") == "1");
  ledSettings.colorIdle     = ledColorArg("ledcidl", ledSettings.colorIdle);
  ledSettings.colorPrinting = ledColorArg("ledcprn", ledSettings.colorPrinting);
  ledSettings.colorPaused   = ledColorArg("ledcpau", ledSettings.colorPaused);
  ledSettings.colorFinished = ledColorArg("ledcfin", ledSettings.colorFinished);
  ledSettings.colorError    = ledColorArg("ledcerr", ledSettings.colorError);
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
  doc["plugType"]        = s.plugType;            // 0=Tasmota, 1=Shelly Gen2/3, 2=Kasa legacy, 3=Shelly Power Strip Gen4
  doc["plugOutlet"]      = s.plugOutlet;          // plugType 3 only: which outlet (0-3)
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
    o["plugType"]   = tasmotaSettings[i].plugType;  // JS hides Today row when unavailable
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
  if (server.hasArg("tsm_pt")) {
    int pt = server.arg("tsm_pt").toInt();
    s.plugType = (pt >= 0 && pt <= 3) ? (uint8_t)pt : 0;
  }
  if (server.hasArg("tsm_po")) {
    int po = server.arg("tsm_po").toInt();
    s.plugOutlet = (po >= 0 && po <= 3) ? (uint8_t)po : 0;
  }
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
    JsonArray islot = p["idleSlots"].to<JsonArray>();
    for (uint8_t g = 0; g < IDLE_SLOT_COUNT;       g++) islot.add(cfg.idleSlots[g]);
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
  disp["timeDisplayMode"] = dispSettings.timeDisplayMode;
  // Legacy key, still emitted so an export imported by pre-3.7.7 firmware keeps
  // the closest behaviour ("Both" degrades to the ETA form).
  disp["showTimeRemaining"] = (dispSettings.timeDisplayMode == 1);
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
  rgb565ToHtml(dispSettings.etaColor, buf);      disp["etaColor"] = String(buf);
  rgb565ToHtml(dispSettings.finishColor, buf);   disp["finishColor"] = String(buf);
  rgb565ToHtml(dispSettings.statusOkColor, buf); disp["statusOkColor"] = String(buf);
  rgb565ToHtml(dispSettings.printerNameColor, buf); disp["printerNameColor"] = String(buf);
  rgb565ToHtml(dispSettings.textColor, buf);     disp["textColor"] = String(buf);
  rgb565ToHtml(dispSettings.textDimColor, buf);  disp["textDimColor"] = String(buf);
  rgb565ToHtml(dispSettings.doorClosedColor, buf); disp["doorClosedColor"] = String(buf);
  rgb565ToHtml(dispSettings.doorOpenColor, buf);   disp["doorOpenColor"] = String(buf);
  disp["roundSkin"] = dispSettings.roundSkin;
  disp["glowMode"] = dispSettings.glowMode;
  rgb565ToHtml(dispSettings.glowColor, buf); disp["glowColor"] = String(buf);
  disp["glowStyle"] = dispSettings.glowStyle;
  disp["glowDuration"] = dispSettings.glowDuration;
#if HAS_HMS_UI
  disp["hmsEnabled"] = dispSettings.hmsEnabled;
  disp["hmsSeverityAll"] = dispSettings.hmsSeverityAll;
  disp["hmsAlertMask"] = dispSettings.hmsAlertMask;
  disp["hmsAutoPresent"] = dispSettings.hmsAutoPresent;
  disp["hmsLookupOnline"] = dispSettings.hmsLookupOnline;
#endif

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
  dp["keepPrintScreen"] = dpSettings.keepPrintScreen;
  dp["finishShowTime"] = dpSettings.finishShowTime;
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

  // Status LED
  JsonObject led = doc["led"].to<JsonObject>();
  led["enabled"]    = ledSettings.enabled;
  led["pin"]        = ledSettings.pin;
  led["brightness"] = ledSettings.brightness;
  led["driver"]      = ledSettings.driver;
  led["pinG"]        = ledSettings.pinG;
  led["pinB"]        = ledSettings.pinB;
  led["commonAnode"] = ledSettings.commonAnode;
  JsonObject ledCol = led["colors"].to<JsonObject>();
  ledCol["idle"]     = ledSettings.colorIdle;
  ledCol["printing"] = ledSettings.colorPrinting;
  ledCol["paused"]   = ledSettings.colorPaused;
  ledCol["finished"] = ledSettings.colorFinished;
  ledCol["error"]    = ledSettings.colorError;
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
    p["plugOutlet"]      = tasmotaSettings[i].plugOutlet;
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
      // Ready / Print Complete pair. Backups predating it have no field, so
      // restore the nozzle+bed those screens used to draw rather than blanking
      // them. Camera is refused here too - see handleSaveGaugeLayout.
      {
        JsonArray islot = p["idleSlots"].as<JsonArray>();
        static const uint8_t defIdle[IDLE_SLOT_COUNT] = { GAUGE_NOZZLE, GAUGE_BED };
        for (uint8_t g = 0; g < IDLE_SLOT_COUNT; g++) {
          if (islot && g < islot.size()) {
            uint8_t v = islot[g].as<uint8_t>();
            cfg.idleSlots[g] = (v < GAUGE_TYPE_COUNT && v != GAUGE_CAMERA) ? v : GAUGE_EMPTY;
          } else {
            cfg.idleSlots[g] = defIdle[g];
          }
        }
      }
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
    // New tri-state key wins; fall back to the legacy boolean for old exports.
    if (disp["timeDisplayMode"].is<int>()) {
      int tm = disp["timeDisplayMode"].as<int>();
      dispSettings.timeDisplayMode = (tm >= 0 && tm <= 2) ? (uint8_t)tm : 0;
    } else if (disp["showTimeRemaining"].is<bool>()) {
      dispSettings.timeDisplayMode = disp["showTimeRemaining"].as<bool>() ? 1 : 0;
    }
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
    if (disp["etaColor"].is<const char*>())      dispSettings.etaColor = htmlToRgb565(disp["etaColor"]);
    if (disp["finishColor"].is<const char*>())   dispSettings.finishColor = htmlToRgb565(disp["finishColor"]);
    if (disp["statusOkColor"].is<const char*>()) dispSettings.statusOkColor = htmlToRgb565(disp["statusOkColor"]);
    if (disp["printerNameColor"].is<const char*>()) dispSettings.printerNameColor = htmlToRgb565(disp["printerNameColor"]);
    if (disp["textColor"].is<const char*>())     dispSettings.textColor = htmlToRgb565(disp["textColor"]);
    if (disp["textDimColor"].is<const char*>())  dispSettings.textDimColor = htmlToRgb565(disp["textDimColor"]);
    if (disp["doorClosedColor"].is<const char*>()) dispSettings.doorClosedColor = htmlToRgb565(disp["doorClosedColor"]);
    if (disp["doorOpenColor"].is<const char*>())   dispSettings.doorOpenColor = htmlToRgb565(disp["doorOpenColor"]);
    if (disp["roundSkin"].is<int>()) { int rs = disp["roundSkin"].as<int>(); dispSettings.roundSkin = (rs >= 0 && rs <= 2) ? (uint8_t)rs : 0; }
    if (disp["glowMode"].is<int>())  { int gm = disp["glowMode"].as<int>();  dispSettings.glowMode = (gm >= 0 && gm <= 2) ? (uint8_t)gm : 0; }
    if (disp["glowColor"].is<const char*>()) dispSettings.glowColor = htmlToRgb565(disp["glowColor"]);
    if (disp["glowStyle"].is<int>()) { int gs = disp["glowStyle"].as<int>(); dispSettings.glowStyle = (gs >= 0 && gs <= 2) ? (uint8_t)gs : 0; }
    if (disp["glowDuration"].is<int>()) { int gd = disp["glowDuration"].as<int>(); dispSettings.glowDuration = (gd >= 0 && gd <= 2) ? (uint8_t)gd : 0; }
#if HAS_HMS_UI
    if (disp["hmsEnabled"].is<bool>())     dispSettings.hmsEnabled = disp["hmsEnabled"].as<bool>();
    if (disp["hmsSeverityAll"].is<bool>()) dispSettings.hmsSeverityAll = disp["hmsSeverityAll"].as<bool>();
    if (disp["hmsAlertMask"].is<int>())    dispSettings.hmsAlertMask = (uint8_t)(disp["hmsAlertMask"].as<int>() & 0x0F);
    if (disp["hmsAutoPresent"].is<int>())  { int ap = disp["hmsAutoPresent"].as<int>(); dispSettings.hmsAutoPresent = (ap >= 0 && ap <= 2) ? (uint8_t)ap : 0; }
    if (disp["hmsLookupOnline"].is<bool>()) dispSettings.hmsLookupOnline = disp["hmsLookupOnline"].as<bool>();
#endif
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
    if (dp["keepPrintScreen"].is<bool>())       dpSettings.keepPrintScreen = dp["keepPrintScreen"].as<bool>();
    if (dp["finishShowTime"].is<bool>())        dpSettings.finishShowTime = dp["finishShowTime"].as<bool>();
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
      // Derive the index from the string, never from the backup's stored index:
      // a backup taken on an older firmware numbered the database differently.
      netSettings.timezoneIndex = resolveTimezoneIndex(netSettings.timezoneStr);
    } else if (net["gmtOffsetMin"].is<int16_t>()) {
      // Backward compat: import from old format
      int16_t oldOffset = net["gmtOffsetMin"].as<int16_t>();
      const char* migrated = getDefaultTimezoneForOffset(oldOffset);
      if (migrated) {
        strlcpy(netSettings.timezoneStr, migrated, sizeof(netSettings.timezoneStr));
        netSettings.timezoneIndex = resolveTimezoneIndex(netSettings.timezoneStr);
      }
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

  // Status LED
  JsonObject led = doc["led"];
  if (led) {
    if (led["enabled"].is<bool>())       ledSettings.enabled = led["enabled"].as<bool>();
    if (led["pin"].is<uint8_t>())        ledSettings.pin = led["pin"].as<uint8_t>();
    if (led["brightness"].is<uint8_t>()) ledSettings.brightness = led["brightness"].as<uint8_t>();
    if (led["driver"].is<uint8_t>()) {
      uint8_t d = led["driver"].as<uint8_t>();
      if (d <= LED_DRV_PIXEL) ledSettings.driver = d;
    }
    if (led["pinG"].is<uint8_t>())        ledSettings.pinG = led["pinG"].as<uint8_t>();
    if (led["pinB"].is<uint8_t>())        ledSettings.pinB = led["pinB"].as<uint8_t>();
    if (led["commonAnode"].is<bool>())    ledSettings.commonAnode = led["commonAnode"].as<bool>();
    JsonObject ledCol = led["colors"];
    if (ledCol) {
      if (ledCol["idle"].is<uint32_t>())     ledSettings.colorIdle     = ledCol["idle"].as<uint32_t>() & 0xFFFFFF;
      if (ledCol["printing"].is<uint32_t>()) ledSettings.colorPrinting = ledCol["printing"].as<uint32_t>() & 0xFFFFFF;
      if (ledCol["paused"].is<uint32_t>())   ledSettings.colorPaused   = ledCol["paused"].as<uint32_t>() & 0xFFFFFF;
      if (ledCol["finished"].is<uint32_t>()) ledSettings.colorFinished = ledCol["finished"].as<uint32_t>() & 0xFFFFFF;
      if (ledCol["error"].is<uint32_t>())    ledSettings.colorError    = ledCol["error"].as<uint32_t>() & 0xFFFFFF;
    }
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
      if (p["plugType"].is<uint8_t>())      { uint8_t pt = p["plugType"].as<uint8_t>(); s.plugType = (pt <= 3) ? pt : 0; }
      if (p["plugOutlet"].is<uint8_t>())    { uint8_t po = p["plugOutlet"].as<uint8_t>(); s.plugOutlet = (po <= 3) ? po : 0; }
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

// ---------------------------------------------------------------------------
//  OTA rollback - boot the firmware still sitting in the inactive app slot,
//  no re-download involved. Escape hatch for a regressing update.
//
//  Every Arduino-core image carries the lib-builder's app descriptor
//  ("arduino-lib-builder" / "esp-idf: v4.4.7" for every build), so builds are
//  told apart only by the per-build ELF sha256 esptool patches in. The
//  human-readable version comes from the ver_<label> note that
//  recordBootSlotVersion() maintains, trusted only while its sha tag still
//  matches the image actually in the slot.
// ---------------------------------------------------------------------------
static const esp_partition_t* rollbackCandidate(String& fwVersion, const char** reason) {
  *reason = "";
  const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
  if (!other) { *reason = "this device has no second app slot"; return nullptr; }

  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
    *reason = "the other app slot is empty"; return nullptr;
  }
  const esp_app_desc_t* run = esp_ota_get_app_description();
  if (strncmp(desc.project_name, run->project_name, sizeof(desc.project_name)) != 0) {
    *reason = "the other app slot holds something else"; return nullptr;
  }
  if (memcmp(desc.app_elf_sha256, run->app_elf_sha256, sizeof(desc.app_elf_sha256)) == 0) {
    *reason = "the other app slot holds this same build"; return nullptr;
  }
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(other, &st) != ESP_OK) st = ESP_OTA_IMG_UNDEFINED;
  if (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED) {
    *reason = "the firmware in the other slot previously failed to boot"; return nullptr;
  }

  char note[48];
  if (loadSlotVersionNote(other->label, note, sizeof(note))) {
    char* bar = strchr(note, '|');
    if (bar) {
      char sha8[9];
      snprintf(sha8, sizeof(sha8), "%02x%02x%02x%02x",
               desc.app_elf_sha256[0], desc.app_elf_sha256[1],
               desc.app_elf_sha256[2], desc.app_elf_sha256[3]);
      if (strcmp(bar + 1, sha8) == 0) { *bar = '\0'; fwVersion = note; }
    }
  }
  return other;
}

static void handleOtaSlots() {
  String fw;
  const char* reason;
  const esp_partition_t* other = rollbackCandidate(fw, &reason);
  JsonDocument doc;
  doc["can"] = other != nullptr;
  if (other) { if (fw.length() > 0) doc["fw"] = fw; }
  else doc["reason"] = reason;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleOtaRollback() {
  String fw;
  const char* reason;
  const esp_partition_t* other = rollbackCandidate(fw, &reason);
  if (!other) {
    String resp = String("{\"status\":\"error\",\"message\":\"Rollback unavailable: ") + reason + ".\"}";
    server.send(200, "application/json", resp);
    return;
  }
  // esp_ota_set_boot_partition() runs a full esp_image_verify() over the
  // target slot before touching otadata, so a damaged or half-written image
  // is refused right here instead of bricking the boot. (Update.canRollBack()
  // only checks the first byte - do not swap this for it.)
  esp_err_t err = esp_ota_set_boot_partition(other);
  if (err != ESP_OK) {
    String resp = String("{\"status\":\"error\",\"message\":\"The other slot's firmware failed verification (") + esp_err_to_name(err) + ").\"}";
    server.send(200, "application/json", resp);
    return;
  }
  Serial.printf("OTA: rollback -> %s (%s)\n", other->label,
                fw.length() > 0 ? fw.c_str() : "unknown version");
  disconnectBambuMqtt();
  server.send(200, "application/json",
    "{\"status\":\"ok\",\"message\":\"Booting the other firmware slot...\"}");
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
  server.on("/glow/test", HTTP_POST, handleGlowTest);
  server.on("/app.css", HTTP_GET, handleAppCss);
  server.on("/app.js", HTTP_GET, handleAppJs);
  server.on("/cloud/logout", HTTP_POST, handleCloudLogout);
  server.on("/cloud/printers", HTTP_GET, handleCloudPrinters);
#if HAS_CLOUD_LOGIN
  server.on("/cloud/login", HTTP_POST, handleCloudLoginStart);
  server.on("/cloud/login/code", HTTP_POST, handleCloudLoginCode);
  server.on("/cloud/login/status", HTTP_GET, handleCloudLoginStatus);
  server.on("/cloud/login/selftest", HTTP_GET, handleCloudSelfTest);
#endif
  server.on("/lan/scan", HTTP_POST, handleLanScan);
  server.on("/lan/scan", HTTP_GET, handleLanScan);
  server.on("/settings/export", HTTP_GET, handleSettingsExport);
  server.on("/settings/import", HTTP_POST, handleSettingsImportFinish, handleSettingsImportUpload);
  server.on("/ota/upload", HTTP_POST, handleOtaFinish, handleOtaUpload);
  // Deliberately outside ENABLE_OTA_AUTO: rolling back must work exactly on
  // the boards too constrained for the online-update path.
  server.on("/ota/slots",    HTTP_GET,  handleOtaSlots);
  server.on("/ota/rollback", HTTP_POST, handleOtaRollback);
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
