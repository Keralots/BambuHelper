#include "settings.h"
#include "config.h"
#include "timezones.h"
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
BuzzerSettings buzzerSettings = { false, BUZZER_DEFAULT_PIN, 0, 0 };
TasmotaSettings tasmotaSettings = { false, "", 0, 30, 255 };

static Preferences prefs;

// ---------------------------------------------------------------------------
//  RGB565 <-> HTML hex conversion
// ---------------------------------------------------------------------------
uint16_t htmlToRgb565(const char* hex) {
  if (!hex || hex[0] == '\0') return 0;
  // Skip '#' if present
  if (hex[0] == '#') hex++;
  uint32_t rgb = strtoul(hex, nullptr, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t bambuColorToRgb565(const char* rrggbbaa) {
  if (!rrggbbaa || strlen(rrggbbaa) < 6) return 0;
  uint32_t rgba = strtoul(rrggbbaa, nullptr, 16);
  // RRGGBBAA: shift depends on length (6 = RRGGBB, 8 = RRGGBBAA)
  uint8_t r, g, b;
  if (strlen(rrggbbaa) >= 8) {
    r = (rgba >> 24) & 0xFF;
    g = (rgba >> 16) & 0xFF;
    b = (rgba >> 8) & 0xFF;
  } else {
    r = (rgba >> 16) & 0xFF;
    g = (rgba >> 8) & 0xFF;
    b = rgba & 0xFF;
  }

  // --- Saturation & Brightness Boost ---
  // Bambu Lab's MQTT hex colors are often extremely pastel (washed out),
  // causing CYD TFT displays to render them with an ugly dithered look.
  // We boost saturation by pulling the lowest RGB value closer to 0.
  uint8_t max_val = r;
  if (g > max_val) max_val = g;
  if (b > max_val) max_val = b;
  
  uint8_t min_val = r;
  if (g < min_val) min_val = g;
  if (b < min_val) min_val = b;

  if (max_val > 0 && max_val > min_val) {
    // Avoid blowing out almost-grey colors (like silver/grey filaments)
    // Only boost if there is a distinct color (saturation > 10%)
    if ((max_val - min_val) > (max_val / 10)) {
       float scale = (float)max_val / (max_val - min_val);
       float full_r = (r - min_val) * scale;
       float full_g = (g - min_val) * scale;
       float full_b = (b - min_val) * scale;

       // Blend 65% towards fully saturated color to make it pop
       float blend = 0.65f;
       r = (uint8_t)(r + (full_r - r) * blend);
       g = (uint8_t)(g + (full_g - g) * blend);
       b = (uint8_t)(b + (full_b - b) * blend);
    }
  }

  // Boost global brightness of the color so it's not too dark on the TFT
  if (max_val > 0 && max_val < 220) {
    float b_scale = 220.0f / max_val;
    r = (uint8_t)(r * b_scale);
    g = (uint8_t)(g * b_scale);
    b = (uint8_t)(b * b_scale);
  }

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
  ds.animatedBar = true;
  ds.pongClock = false;
  ds.smallLabels = false;
  ds.invertColors = false;
  ds.cydExtraMode = 0;

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
  // Open read-write from the start: we may need to write a migration flag.
  // This is safe and avoids closing/reopening the partition mid-load.
  prefs.begin(NVS_NAMESPACE, false);

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
    strlcpy(printers[i].state.gcodeState, "UNKNOWN", sizeof(printers[i].state.gcodeState));
  }

  // Display settings
  DisplaySettings def;
  defaultDisplaySettings(def);

  dispSettings.rotation = prefs.getUChar("dsp_rot", def.rotation);
  dispSettings.bgColor = prefs.getUShort("dsp_bg", def.bgColor);
  dispSettings.trackColor = prefs.getUShort("dsp_trk", def.trackColor);
  dispSettings.animatedBar = prefs.getBool("dsp_abar", def.animatedBar);
  dispSettings.pongClock = prefs.getBool("dsp_pong", def.pongClock);
  dispSettings.smallLabels = prefs.getBool("dsp_slbl", def.smallLabels);
  dispSettings.invertColors = prefs.getBool("dsp_inv", def.invertColors);
  dispSettings.cydExtraMode = prefs.getUChar("dsp_cydex", 0);
  dispSettings.cydExtraMode = 0;  // temporary: force AMS-only on CYD

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

  // Timezone: load POSIX string, migrating from legacy gmtOffsetMin if needed.
  // All reads and any migration writes happen in the same open transaction to
  // prevent incomplete state if power is lost mid-migration.
  bool tzMigrated = prefs.getBool("tz_migrated", false);
  String tzStr = prefs.getString("net_tzstr", "");
  if (tzStr.isEmpty() && !tzMigrated) {
    // Legacy migration: convert old integer offset to POSIX timezone string.
    int16_t oldOffset = prefs.getShort("net_tz", 60);
    const char* migrated = getDefaultTimezoneForOffset(oldOffset);
    if (migrated) {
      tzStr = migrated;
    } else {
      tzStr = "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00";
    }
    // Write both the migrated value and the completion flag in the same
    // transaction so a power loss cannot leave migration half-done.
    prefs.putString("net_tzstr", tzStr);
    prefs.putBool("tz_migrated", true);
    Serial.printf("[SETTINGS] Migrated timezone: offset %d -> %s\n", oldOffset, tzStr.c_str());
  } else if (!tzStr.isEmpty() && !tzMigrated) {
    // Recovery: net_tzstr already exists but flag is absent — this device ran
    // the old migration code and lost power before it could be marked done.
    // Stamp the flag now so future boots skip migration entirely.
    prefs.putBool("tz_migrated", true);
  }
  strlcpy(netSettings.timezoneStr, tzStr.c_str(), sizeof(netSettings.timezoneStr));

  // Re-resolve the index from the POSIX string (handles database reordering
  // across firmware updates without relying on a stored index value).
  {
    size_t cnt;
    const TimezoneRegion* regions = getSupportedTimezones(&cnt);
    netSettings.timezoneIndex = 14;  // default: CET (Amsterdam, Berlin, Rome)
    for (size_t i = 0; i < cnt; i++) {
      if (strcmp(regions[i].posixString, netSettings.timezoneStr) == 0) {
        netSettings.timezoneIndex = (uint8_t)i;
        break;
      }
    }
  }

  netSettings.use24h = prefs.getBool("net_24h", true);
  netSettings.dateFormat = prefs.getUChar("net_datefmt", 0);

  // Display power settings
  dpSettings.finishDisplayMins = prefs.getUShort("dp_fmins", 3);
  dpSettings.keepDisplayOn = prefs.getBool("dp_keepon", false);
  dpSettings.showClockAfterFinish = prefs.getBool("dp_clock", true);
  dpSettings.doorAckEnabled = prefs.getBool("dp_dack", false);
  dpSettings.nightModeEnabled = prefs.getBool("dp_night", false);
  dpSettings.nightStartHour = prefs.getUChar("dp_nstart", 22);
  dpSettings.nightEndHour = prefs.getUChar("dp_nend", 7);
  dpSettings.nightBrightness = prefs.getUChar("dp_nbright", 30);
  dpSettings.screensaverBrightness = prefs.getUChar("dp_ssbright", 30);

  // Rotation settings (multi-printer)
  rotState.mode = (RotateMode)prefs.getUChar("rot_mode", ROTATE_SMART);
  rotState.intervalMs = prefs.getULong("rot_intv", ROTATE_INTERVAL_MS);
  if (rotState.intervalMs < ROTATE_MIN_MS) rotState.intervalMs = ROTATE_MIN_MS;
  if (rotState.intervalMs > ROTATE_MAX_MS) rotState.intervalMs = ROTATE_MAX_MS;
  rotState.displayIndex = 0;
  rotState.lastRotateMs = 0;

  // Button settings
#ifdef DISPLAY_CYD
  buttonType = (ButtonType)prefs.getUChar("btn_type", BTN_TOUCHSCREEN);
#else
  buttonType = (ButtonType)prefs.getUChar("btn_type", BTN_DISABLED);
#endif
  buttonPin = prefs.getUChar("btn_pin", BUTTON_DEFAULT_PIN);

  // Buzzer settings
  buzzerSettings.enabled = prefs.getBool("buz_on", false);
  buzzerSettings.pin = prefs.getUChar("buz_pin", BUZZER_DEFAULT_PIN);
  buzzerSettings.quietStartHour = prefs.getUChar("buz_qstart", 0);
  buzzerSettings.quietEndHour = prefs.getUChar("buz_qend", 0);

  // Cloud email (display only)
  strlcpy(cloudEmail, prefs.getString("cl_email", "").c_str(), sizeof(cloudEmail));

  // Tasmota power monitoring
  tasmotaSettings.enabled = prefs.getBool("tsm_en", false);
  strlcpy(tasmotaSettings.ip, prefs.getString("tsm_ip", "").c_str(), sizeof(tasmotaSettings.ip));
  tasmotaSettings.displayMode = prefs.getUChar("tsm_dm", 0);
  tasmotaSettings.pollInterval = prefs.getUChar("tsm_pi", 10);
  tasmotaSettings.assignedSlot = prefs.getUChar("tsm_slot", 255);

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
  prefs.putBool("dsp_abar", dispSettings.animatedBar);
  prefs.putBool("dsp_pong", dispSettings.pongClock);
  prefs.putBool("dsp_slbl", dispSettings.smallLabels);
  prefs.putBool("dsp_inv", dispSettings.invertColors);
  prefs.putUChar("dsp_cydex", dispSettings.cydExtraMode);

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
  prefs.putString("net_tzstr", netSettings.timezoneStr);
  prefs.putUChar("net_tzidx", netSettings.timezoneIndex);
  prefs.putBool("net_24h", netSettings.use24h);
  prefs.putUChar("net_datefmt", netSettings.dateFormat);

  // Display power settings
  prefs.putUShort("dp_fmins", dpSettings.finishDisplayMins);
  prefs.putBool("dp_keepon", dpSettings.keepDisplayOn);
  prefs.putBool("dp_clock", dpSettings.showClockAfterFinish);
  prefs.putBool("dp_dack", dpSettings.doorAckEnabled);
  prefs.putBool("dp_night", dpSettings.nightModeEnabled);
  prefs.putUChar("dp_nstart", dpSettings.nightStartHour);
  prefs.putUChar("dp_nend", dpSettings.nightEndHour);
  prefs.putUChar("dp_nbright", dpSettings.nightBrightness);
  prefs.putUChar("dp_ssbright", dpSettings.screensaverBrightness);

  // Tasmota power monitoring
  prefs.putBool("tsm_en", tasmotaSettings.enabled);
  prefs.putString("tsm_ip", tasmotaSettings.ip);
  prefs.putUChar("tsm_dm", tasmotaSettings.displayMode);
  prefs.putUChar("tsm_pi", tasmotaSettings.pollInterval);
  prefs.putUChar("tsm_slot", tasmotaSettings.assignedSlot);

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

void saveBuzzerSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("buz_on", buzzerSettings.enabled);
  prefs.putUChar("buz_pin", buzzerSettings.pin);
  prefs.putUChar("buz_qstart", buzzerSettings.quietStartHour);
  prefs.putUChar("buz_qend", buzzerSettings.quietEndHour);
  prefs.end();
}

void resetSettings() {
  // Clear sensitive data from RAM before wiping NVS
  memset(wifiPass, 0, sizeof(wifiPass));
  memset(cloudEmail, 0, sizeof(cloudEmail));
  for (int i = 0; i < MAX_PRINTERS; i++) {
    memset(printers[i].config.accessCode, 0, sizeof(printers[i].config.accessCode));
    memset(printers[i].config.cloudUserId, 0, sizeof(printers[i].config.cloudUserId));
  }

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
