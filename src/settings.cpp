#include "settings.h"
#include "config.h"
#include <Preferences.h>

// Global state
PrinterSlot printers[MAX_PRINTERS];
uint8_t activePrinterIndex = 0;
RotationState rotState = { ROTATE_SMART, ROTATE_INTERVAL_MS, 0, 0 };
char wifiSSID[33] = {0};
char wifiPass[65] = {0};
uint8_t brightness = 200;
DisplaySettings dispSettings;
NetworkSettings netSettings;
DisplayPowerSettings dpSettings;
char cloudEmail[64] = {0};
ButtonType buttonType = BTN_DISABLED;
uint8_t buttonPin = BUTTON_DEFAULT_PIN;

static Preferences prefs;

// ---------------------------------------------------------------------------
//  RGB565 <-> HTML hex conversion
// ---------------------------------------------------------------------------
uint16_t htmlToRgb565(const char* hex) {
  // Skip '#' if present
  if (hex[0] == '#') hex++;
  uint32_t rgb = strtoul(hex, nullptr, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void rgb565ToHtml(uint16_t c, char* buf) {
  uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
  uint8_t b = (c & 0x1F) * 255 / 31;
  snprintf(buf, 8, "#%02X%02X%02X", r, g, b);
}

// ---------------------------------------------------------------------------
//  Default display settings (matches original config.h colors)
// ---------------------------------------------------------------------------
void defaultDisplaySettings(DisplaySettings& ds) {
  ds.rotation = 0;
  ds.bgColor = CLR_BG;
  ds.trackColor = CLR_TRACK;

  // Progress: green arc, green label, white value
  ds.progress = { CLR_GREEN, CLR_GREEN, CLR_TEXT };
  // Nozzle: orange arc, orange label, white value
  ds.nozzle = { CLR_ORANGE, CLR_ORANGE, CLR_TEXT };
  // Bed: cyan arc, cyan label, white value
  ds.bed = { CLR_CYAN, CLR_CYAN, CLR_TEXT };
  // Part fan: cyan arc, cyan label, white value
  ds.partFan = { CLR_CYAN, CLR_CYAN, CLR_TEXT };
  // Aux fan: orange arc, orange label, white value
  ds.auxFan = { CLR_ORANGE, CLR_ORANGE, CLR_TEXT };
  // Chamber fan: green arc, green label, white value
  ds.chamberFan = { CLR_GREEN, CLR_GREEN, CLR_TEXT };
}

// ---------------------------------------------------------------------------
//  Save/load a single GaugeColors struct
// ---------------------------------------------------------------------------
static void saveGaugeColors(const char* prefix, const GaugeColors& gc) {
  char key[16];
  snprintf(key, sizeof(key), "%s_a", prefix);
  prefs.putUShort(key, gc.arc);
  snprintf(key, sizeof(key), "%s_l", prefix);
  prefs.putUShort(key, gc.label);
  snprintf(key, sizeof(key), "%s_v", prefix);
  prefs.putUShort(key, gc.value);
}

static void loadGaugeColors(const char* prefix, GaugeColors& gc, const GaugeColors& def) {
  char key[16];
  snprintf(key, sizeof(key), "%s_a", prefix);
  gc.arc = prefs.getUShort(key, def.arc);
  snprintf(key, sizeof(key), "%s_l", prefix);
  gc.label = prefs.getUShort(key, def.label);
  snprintf(key, sizeof(key), "%s_v", prefix);
  gc.value = prefs.getUShort(key, def.value);
}

// ---------------------------------------------------------------------------
//  Load settings
// ---------------------------------------------------------------------------
void loadSettings() {
  prefs.begin(NVS_NAMESPACE, true);  // read-only

  // WiFi credentials
  strlcpy(wifiSSID, prefs.getString("wifiSSID", "").c_str(), sizeof(wifiSSID));
  strlcpy(wifiPass, prefs.getString("wifiPass", "").c_str(), sizeof(wifiPass));

  brightness = prefs.getUChar("bright", 200);
  activePrinterIndex = prefs.getUChar("activePrt", 0);
  if (activePrinterIndex >= MAX_PRINTERS) activePrinterIndex = 0;

  // Load each printer slot
  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    char key[16];
    PrinterConfig& cfg = printers[i].config;

    snprintf(key, sizeof(key), "p%d_ip", i);
    strlcpy(cfg.ip, prefs.getString(key, "").c_str(), sizeof(cfg.ip));

    snprintf(key, sizeof(key), "p%d_serial", i);
    strlcpy(cfg.serial, prefs.getString(key, "").c_str(), sizeof(cfg.serial));

    snprintf(key, sizeof(key), "p%d_code", i);
    strlcpy(cfg.accessCode, prefs.getString(key, "").c_str(), sizeof(cfg.accessCode));

    snprintf(key, sizeof(key), "p%d_name", i);
    strlcpy(cfg.name, prefs.getString(key, "").c_str(), sizeof(cfg.name));

    snprintf(key, sizeof(key), "p%d_mode", i);
    cfg.mode = (ConnMode)prefs.getUChar(key, CONN_LOCAL);
    if (cfg.mode == CONN_CLOUD) cfg.mode = CONN_CLOUD_ALL;  // migrate legacy value

    snprintf(key, sizeof(key), "p%d_cuid", i);
    strlcpy(cfg.cloudUserId, prefs.getString(key, "").c_str(), sizeof(cfg.cloudUserId));

    snprintf(key, sizeof(key), "p%d_region", i);
    cfg.region = (CloudRegion)prefs.getUChar(key, REGION_US);

    // Zero out state
    memset(&printers[i].state, 0, sizeof(BambuState));
    strcpy(printers[i].state.gcodeState, "UNKNOWN");
  }

  // Display settings
  DisplaySettings def;
  defaultDisplaySettings(def);

  dispSettings.rotation = prefs.getUChar("dsp_rot", def.rotation);
  dispSettings.bgColor = prefs.getUShort("dsp_bg", def.bgColor);
  dispSettings.trackColor = prefs.getUShort("dsp_trk", def.trackColor);

  loadGaugeColors("gc_prg", dispSettings.progress, def.progress);
  loadGaugeColors("gc_noz", dispSettings.nozzle, def.nozzle);
  loadGaugeColors("gc_bed", dispSettings.bed, def.bed);
  loadGaugeColors("gc_pfn", dispSettings.partFan, def.partFan);
  loadGaugeColors("gc_afn", dispSettings.auxFan, def.auxFan);
  loadGaugeColors("gc_cfn", dispSettings.chamberFan, def.chamberFan);

  // Network settings
  netSettings.useDHCP = prefs.getBool("net_dhcp", true);
  strlcpy(netSettings.staticIP, prefs.getString("net_ip", "").c_str(), sizeof(netSettings.staticIP));
  strlcpy(netSettings.gateway, prefs.getString("net_gw", "").c_str(), sizeof(netSettings.gateway));
  strlcpy(netSettings.subnet, prefs.getString("net_sn", "255.255.255.0").c_str(), sizeof(netSettings.subnet));
  strlcpy(netSettings.dns, prefs.getString("net_dns", "").c_str(), sizeof(netSettings.dns));
  netSettings.showIPAtStartup = prefs.getBool("net_showip", true);
  netSettings.gmtOffsetMin = prefs.getShort("net_tz", 60);  // default UTC+1 (CET)
  netSettings.dstEnabled = prefs.getBool("net_dst", false);
  netSettings.use24h = prefs.getBool("net_24h", true);

  // Display power settings
  dpSettings.finishDisplayMins = prefs.getUShort("dp_fmins", 3);
  dpSettings.keepDisplayOn = prefs.getBool("dp_keepon", false);
  dpSettings.showClockAfterFinish = prefs.getBool("dp_clock", true);

  // Rotation settings (multi-printer)
  rotState.mode = (RotateMode)prefs.getUChar("rot_mode", ROTATE_SMART);
  rotState.intervalMs = prefs.getULong("rot_intv", ROTATE_INTERVAL_MS);
  if (rotState.intervalMs < ROTATE_MIN_MS) rotState.intervalMs = ROTATE_MIN_MS;
  if (rotState.intervalMs > ROTATE_MAX_MS) rotState.intervalMs = ROTATE_MAX_MS;
  rotState.displayIndex = 0;
  rotState.lastRotateMs = 0;

  // Button settings
  buttonType = (ButtonType)prefs.getUChar("btn_type", BTN_DISABLED);
  buttonPin = prefs.getUChar("btn_pin", BUTTON_DEFAULT_PIN);

  // Cloud email (display only)
  strlcpy(cloudEmail, prefs.getString("cl_email", "").c_str(), sizeof(cloudEmail));

  prefs.end();
}

// ---------------------------------------------------------------------------
//  Save settings
// ---------------------------------------------------------------------------
void saveSettings() {
  prefs.begin(NVS_NAMESPACE, false);

  prefs.putString("wifiSSID", wifiSSID);
  prefs.putString("wifiPass", wifiPass);
  prefs.putUChar("bright", brightness);
  prefs.putUChar("activePrt", activePrinterIndex);

  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    savePrinterConfig(i);
  }

  // Display settings
  prefs.putUChar("dsp_rot", dispSettings.rotation);
  prefs.putUShort("dsp_bg", dispSettings.bgColor);
  prefs.putUShort("dsp_trk", dispSettings.trackColor);

  saveGaugeColors("gc_prg", dispSettings.progress);
  saveGaugeColors("gc_noz", dispSettings.nozzle);
  saveGaugeColors("gc_bed", dispSettings.bed);
  saveGaugeColors("gc_pfn", dispSettings.partFan);
  saveGaugeColors("gc_afn", dispSettings.auxFan);
  saveGaugeColors("gc_cfn", dispSettings.chamberFan);

  // Network settings
  prefs.putBool("net_dhcp", netSettings.useDHCP);
  prefs.putString("net_ip", netSettings.staticIP);
  prefs.putString("net_gw", netSettings.gateway);
  prefs.putString("net_sn", netSettings.subnet);
  prefs.putString("net_dns", netSettings.dns);
  prefs.putBool("net_showip", netSettings.showIPAtStartup);
  prefs.putShort("net_tz", netSettings.gmtOffsetMin);
  prefs.putBool("net_dst", netSettings.dstEnabled);
  prefs.putBool("net_24h", netSettings.use24h);

  // Display power settings
  prefs.putUShort("dp_fmins", dpSettings.finishDisplayMins);
  prefs.putBool("dp_keepon", dpSettings.keepDisplayOn);
  prefs.putBool("dp_clock", dpSettings.showClockAfterFinish);

  prefs.end();
}

void savePrinterConfig(uint8_t index) {
  if (index >= MAX_PRINTERS) return;

  // Caller may already have prefs open, or we open ourselves
  bool needOpen = !prefs.isKey("wifiSSID");  // heuristic check
  if (needOpen) prefs.begin(NVS_NAMESPACE, false);

  char key[16];
  PrinterConfig& cfg = printers[index].config;

  snprintf(key, sizeof(key), "p%d_ip", index);
  prefs.putString(key, cfg.ip);

  snprintf(key, sizeof(key), "p%d_serial", index);
  prefs.putString(key, cfg.serial);

  snprintf(key, sizeof(key), "p%d_code", index);
  prefs.putString(key, cfg.accessCode);

  snprintf(key, sizeof(key), "p%d_name", index);
  prefs.putString(key, cfg.name);

  snprintf(key, sizeof(key), "p%d_mode", index);
  prefs.putUChar(key, cfg.mode);

  snprintf(key, sizeof(key), "p%d_cuid", index);
  prefs.putString(key, cfg.cloudUserId);

  snprintf(key, sizeof(key), "p%d_region", index);
  prefs.putUChar(key, cfg.region);

  if (needOpen) prefs.end();
}

void saveRotationSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("rot_mode", rotState.mode);
  prefs.putULong("rot_intv", rotState.intervalMs);
  prefs.end();
}

void saveButtonSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("btn_type", buttonType);
  prefs.putUChar("btn_pin", buttonPin);
  prefs.end();
}

void resetSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  ESP.restart();
}

// ---------------------------------------------------------------------------
//  Cloud token persistence
// ---------------------------------------------------------------------------
void saveCloudToken(const char* token) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("cl_token", token);
  prefs.end();
}

bool loadCloudToken(char* buf, size_t bufLen) {
  prefs.begin(NVS_NAMESPACE, true);
  String t = prefs.getString("cl_token", "");
  prefs.end();
  if (t.length() == 0) return false;
  strlcpy(buf, t.c_str(), bufLen);
  return true;
}

void clearCloudToken() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove("cl_token");
  prefs.remove("cl_email");
  prefs.end();
  cloudEmail[0] = '\0';
}

void saveCloudEmail(const char* email) {
  strlcpy(cloudEmail, email, sizeof(cloudEmail));
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("cl_email", email);
  prefs.end();
}
