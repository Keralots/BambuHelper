#include "settings.h"
#include "config.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "timezones.h"
#include <Preferences.h>

// Global state
PrinterSlot printers[MAX_PRINTERS];
uint8_t activePrinterIndex = 0;
RotationState rotState = { ROTATE_SMART, ROTATE_INTERVAL_MS, 0, 0, 0 };
char wifiSSID[33] = {0};
char wifiPass[65] = {0};
uint8_t brightness = 200;
DisplaySettings dispSettings;
NetworkSettings netSettings;
DisplayPowerSettings dpSettings;
char cloudEmail[64] = {0};
ButtonType buttonType = BTN_DISABLED;
uint8_t buttonPin = BUTTON_DEFAULT_PIN;
BuzzerSettings buzzerSettings = { false, BUZZER_DEFAULT_PIN, 0, 0, false, false, 35 };
LedSettings ledSettings = {
  /*enabled*/             false,
  /*pin*/                 LED_DEFAULT_PIN,
  /*brightness*/          128,
  /*finishMode*/          LED_FINISH_OFF,
  /*finishSeconds*/       60,
  /*finishBrightness*/    255,
  /*autoOnWhilePrinting*/ false,
  /*pauseBreathing*/      false,
  /*errorStrobe*/         false,
};
TasmotaSettings tasmotaSettings[TASMOTA_PLUG_COUNT] = {};
float tasmotaTariffPerKwh = 0.0f;
char tasmotaCurrency[8] = "\xE2\x82\xAC";  // "€" UTF-8 default

// Experimental: opt-in 2-printer mode on BOARD_LOW_RAM. Local-only -
// NOT included in /settings/export to avoid propagating an unsafe mode
// across devices via JSON backup.
bool dualPrinterUnsafe = false;

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

  // Near-black filaments: Bambu reports "black" spools as ~#161616, not pure
  // #000000. The residual RGB565 value (0x10A2) renders as true black on IPS
  // panels (jc3248w535) but as a visible dark grey on ST7789 panels
  // (ws_lcd_200, CYD). Snap near-black to pure black so it looks black on every
  // panel. Done before the boosts so it can't be re-lightened.
  if (max_val < 32) return 0x0000;

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

  // Boost global brightness so pastel colors pop, but preserve intentionally
  // dark filaments (black, dark grey). Below this threshold we leave the
  // color alone - inflating r=g=b=10 to ~220 turned black filaments near-white.
  if (max_val >= 32 && max_val < 220) {
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

void sanitizeHostname(const char* in, char* out, size_t outSize) {
  if (outSize == 0) return;
  size_t n = 0;
  for (const char* p = in; *p && n < outSize - 1; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;  // tolower
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') out[n++] = c;
  }
  out[n] = '\0';
  // Strip leading/trailing hyphens (invalid as DNS label boundaries).
  size_t start = 0;
  while (out[start] == '-') start++;
  if (start > 0) memmove(out, out + start, strlen(out + start) + 1);
  size_t len = strlen(out);
  while (len > 0 && out[len - 1] == '-') out[--len] = '\0';
  if (out[0] == '\0') strlcpy(out, "bambuhelper", outSize);
}

// ---------------------------------------------------------------------------
//  Default display settings (matches original config.h colors)
// ---------------------------------------------------------------------------
void defaultDisplaySettings(DisplaySettings& ds) {
  ds.rotation = 0;
  ds.bgColor = CLR_BG;
  ds.trackColor = CLR_TRACK;
  ds.progressBarColor = CLR_GREEN;
  ds.animatedBar = true;
  ds.pongClock = false;
  ds.smallLabels = false;
  ds.showTimeRemaining = false;
  ds.fanMatchPrinter = true;
  ds.invertColors = false;
  ds.cydPanelClassic = false;
  ds.landscape8Slots = false;
  ds.portrait9Slots = false;
  ds.clockTimeColor = CLR_TEXT;
  ds.clockDateColor = CLR_TEXT_DIM;
  ds.clockTimeSize = 0;        // Auto
  ds.hideClockDate = false;
  ds.showClockInfo = false;
  ds.amsTrayTypes = true;       // default ON: preserves existing per-tray labels
  ds.showBatteryIndicator = true;
  ds.nozzleScaleMax  = GAUGE_NOZZLE_SCALE_DEFAULT;
  ds.bedScaleMax     = GAUGE_BED_SCALE_DEFAULT;
  ds.chamberScaleMax = GAUGE_CHAMBER_SCALE_DEFAULT;
  ds.powerScaleW     = GAUGE_POWER_SCALE_DEFAULT;
  ds.gaugeSmoothing  = 2;          // Normal
  ds.warnColor       = CLR_RED;
  ds.warnThresholdPct = 0;         // off by default (no behavior change)

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
  // Aux right fan (X2D): orange arc, orange label, white value
  ds.auxFanRight = { CLR_ORANGE, CLR_ORANGE, CLR_TEXT };
  // Chamber fan: green arc, green label, white value
  ds.chamberFan = { CLR_GREEN, CLR_GREEN, CLR_TEXT };
  // Exhaust fan (X2D): green arc, green label, white value
  ds.exhaustFan = { CLR_GREEN, CLR_GREEN, CLR_TEXT };
  // Chamber temp: cyan arc, cyan label, white value
  ds.chamberTemp = { CLR_CYAN, CLR_CYAN, CLR_TEXT };
  // Heatbreak fan: orange arc, orange label, white value
  ds.heatbreak = { CLR_ORANGE, CLR_ORANGE, CLR_TEXT };
}

// Default standard 2x3 grid: Progress, Nozzle, Bed, Part Fan, Aux Fan, Chamber Fan.
static void defaultGaugeSlots(uint8_t* slots) {
  slots[0] = GAUGE_PROGRESS;
  slots[1] = GAUGE_NOZZLE;
  slots[2] = GAUGE_BED;
  slots[3] = GAUGE_PART_FAN;
  slots[4] = GAUGE_AUX_FAN;
  slots[5] = GAUGE_CHAMBER_FAN;
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

    // Gauge slot layout (per-printer). 3 NVS keys:
    //   p%d_slots - standard 2x3 (6 bytes, always present after first save)
    //   p%d_lext  - landscape extras (2 bytes, only after enabling 8-slot mode)
    //   p%d_pext  - portrait  extras (3 bytes, only after enabling 9-slot mode)
    // Missing keys -> EMPTY. Legacy 8/9-byte records from the in-development
    // 'shared extras' branch read as 6-byte standard slots (trailing bytes
    // ignored); their old landscape slots 6/7 will be re-set in the web UI.
    snprintf(key, sizeof(key), "p%d_slots", i);
    memset(cfg.gaugeSlots, GAUGE_EMPTY, sizeof(cfg.gaugeSlots));
    size_t read = prefs.getBytes(key, cfg.gaugeSlots, sizeof(cfg.gaugeSlots));
    if (read < sizeof(cfg.gaugeSlots)) {
      defaultGaugeSlots(cfg.gaugeSlots);
    } else {
      uint8_t def[GAUGE_SLOT_COUNT];
      defaultGaugeSlots(def);
      for (uint8_t g = 0; g < GAUGE_SLOT_COUNT; g++) {
        if (cfg.gaugeSlots[g] >= GAUGE_TYPE_COUNT) cfg.gaugeSlots[g] = def[g];
      }
    }

    snprintf(key, sizeof(key), "p%d_lext", i);
    memset(cfg.landscapeExtras, GAUGE_EMPTY, sizeof(cfg.landscapeExtras));
    prefs.getBytes(key, cfg.landscapeExtras, sizeof(cfg.landscapeExtras));
    for (uint8_t g = 0; g < LANDSCAPE_EXTRA_COUNT; g++) {
      if (cfg.landscapeExtras[g] >= GAUGE_TYPE_COUNT) cfg.landscapeExtras[g] = GAUGE_EMPTY;
    }

    snprintf(key, sizeof(key), "p%d_pext", i);
    memset(cfg.portraitExtras, GAUGE_EMPTY, sizeof(cfg.portraitExtras));
    prefs.getBytes(key, cfg.portraitExtras, sizeof(cfg.portraitExtras));
    for (uint8_t g = 0; g < PORTRAIT_EXTRA_COUNT; g++) {
      if (cfg.portraitExtras[g] >= GAUGE_TYPE_COUNT) cfg.portraitExtras[g] = GAUGE_EMPTY;
    }

    // AMS view (per-printer): 240x240 only, replaces gauge row 2 with AMS strip
    snprintf(key, sizeof(key), "p%d_amsv", i);
    cfg.amsView = prefs.getBool(key, false);

    // Zero out state
    memset(&printers[i].state, 0, sizeof(BambuState));
    setPrinterGcodeStateCanonical(printers[i].state, GCODE_UNKNOWN);
  }

  // One-shot migration: copy legacy global dsp_amsv to every printer slot
  // that doesn't already have its own value, then remove the legacy key.
  if (prefs.isKey("dsp_amsv")) {
    bool legacy = prefs.getBool("dsp_amsv", false);
    for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
      char key[16];
      snprintf(key, sizeof(key), "p%d_amsv", i);
      if (!prefs.isKey(key)) {
        prefs.putBool(key, legacy);
        printers[i].config.amsView = legacy;
      }
    }
    prefs.remove("dsp_amsv");
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
  dispSettings.showTimeRemaining = prefs.getBool("dsp_shtire", def.showTimeRemaining);
  dispSettings.fanMatchPrinter = prefs.getBool("dsp_fanmp", def.fanMatchPrinter);
  dispSettings.invertColors = prefs.getBool("dsp_inv", def.invertColors);
  dispSettings.cydPanelClassic = prefs.getBool("dsp_cydcls", def.cydPanelClassic);
  dispSettings.landscape8Slots = prefs.getBool("dsp_l8s", def.landscape8Slots);
  dispSettings.portrait9Slots = prefs.getBool("dsp_p9s", def.portrait9Slots);
  dispSettings.clockTimeColor = prefs.getUShort("dsp_clkt", CLR_TEXT);
  dispSettings.clockDateColor = prefs.getUShort("dsp_clkd", CLR_TEXT_DIM);
  {
    uint8_t cts = prefs.getUChar("dsp_clkts", def.clockTimeSize);
    dispSettings.clockTimeSize = (cts <= 3) ? cts : 0;
  }
  dispSettings.hideClockDate = prefs.getBool("dsp_clkhd", def.hideClockDate);
  dispSettings.showClockInfo = prefs.getBool("dsp_clkif", def.showClockInfo);
  dispSettings.amsTrayTypes = prefs.getBool("dsp_amst", def.amsTrayTypes);
  dispSettings.showBatteryIndicator = prefs.getBool("dsp_bat", def.showBatteryIndicator);
  dispSettings.nozzleScaleMax  = constrain((int)prefs.getUShort("dsp_nozmx", def.nozzleScaleMax),
                                           GAUGE_NOZZLE_SCALE_MIN, GAUGE_NOZZLE_SCALE_MAX);
  dispSettings.bedScaleMax     = constrain((int)prefs.getUShort("dsp_bedmx", def.bedScaleMax),
                                           GAUGE_BED_SCALE_MIN, GAUGE_BED_SCALE_MAX);
  dispSettings.chamberScaleMax = constrain((int)prefs.getUShort("dsp_chbmx", def.chamberScaleMax),
                                           GAUGE_CHAMBER_SCALE_MIN, GAUGE_CHAMBER_SCALE_MAX);
  dispSettings.powerScaleW     = constrain((int)prefs.getUShort("dsp_pwrmx", def.powerScaleW),
                                           GAUGE_POWER_SCALE_MIN, GAUGE_POWER_SCALE_MAX);
  {
    uint8_t sm = prefs.getUChar("dsp_smooth", def.gaugeSmoothing);
    dispSettings.gaugeSmoothing = (sm <= 3) ? sm : 2;
  }
  dispSettings.warnColor        = prefs.getUShort("dsp_wclr", def.warnColor);
  dispSettings.warnThresholdPct = constrain((int)prefs.getUChar("dsp_wthr", def.warnThresholdPct), 0, 100);

  loadGaugeColors("gc_prg", dispSettings.progress, def.progress);
  loadGaugeColors("gc_noz", dispSettings.nozzle, def.nozzle);
  loadGaugeColors("gc_bed", dispSettings.bed, def.bed);
  loadGaugeColors("gc_pfn", dispSettings.partFan, def.partFan);
  loadGaugeColors("gc_afn", dispSettings.auxFan, def.auxFan);
  loadGaugeColors("gc_afr", dispSettings.auxFanRight, def.auxFanRight);
  loadGaugeColors("gc_cfn", dispSettings.chamberFan, def.chamberFan);
  loadGaugeColors("gc_exh", dispSettings.exhaustFan, def.exhaustFan);
  loadGaugeColors("gc_cht", dispSettings.chamberTemp, def.chamberTemp);
  loadGaugeColors("gc_hbk", dispSettings.heatbreak, def.heatbreak);

  // Top progress bar color. Before this setting existed the bar reused the
  // Progress gauge arc color, so migrate absent keys to that value to avoid a
  // visible color change for users who had customized the progress arc.
  if (prefs.isKey("dsp_pbar")) {
    dispSettings.progressBarColor = prefs.getUShort("dsp_pbar", def.progressBarColor);
  } else {
    dispSettings.progressBarColor = dispSettings.progress.arc;
  }

  // Network settings
  netSettings.useDHCP = prefs.getBool("net_dhcp", true);
  strlcpy(netSettings.staticIP, prefs.getString("net_ip", "").c_str(), sizeof(netSettings.staticIP));
  strlcpy(netSettings.gateway, prefs.getString("net_gw", "").c_str(), sizeof(netSettings.gateway));
  strlcpy(netSettings.subnet, prefs.getString("net_sn", "255.255.255.0").c_str(), sizeof(netSettings.subnet));
  strlcpy(netSettings.dns, prefs.getString("net_dns", "").c_str(), sizeof(netSettings.dns));
  netSettings.showIPAtStartup = prefs.getBool("net_showip", true);
  netSettings.mdnsEnabled = prefs.getBool("net_mdns", false);
  // Sanitize on load too: a bad value from an older build or corrupted NVS must
  // never reach %MDNS_HOST% raw.
  sanitizeHostname(prefs.getString("net_host", "bambuhelper").c_str(),
                   netSettings.hostname, sizeof(netSettings.hostname));

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
  dpSettings.keepPrintScreen = prefs.getBool("dp_kps", false);
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
#if defined(USE_CST816) || defined(USE_CST328) || defined(USE_XPT2046) || defined(USE_FT5X06) || defined(USE_FT6336) || defined(USE_AXS_TOUCH) || defined(TOUCH_CS)
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
  buzzerSettings.buttonClick = prefs.getBool("buz_click", false);
  buzzerSettings.bedCooldownAlert = prefs.getBool("buz_bed_on", false);
  uint8_t bct = prefs.getUChar("buz_bed_c", 35);
  if (bct < 20 || bct > 80) bct = 35;
  buzzerSettings.bedCooldownThresholdC = bct;

  // External LED settings
  ledSettings.enabled    = prefs.getBool ("led_on",  false);
  ledSettings.pin        = prefs.getUChar("led_pin", LED_DEFAULT_PIN);
  ledSettings.brightness = prefs.getUChar("led_br",  128);

  ledSettings.finishMode       = prefs.getUChar ("led_fx_md",  LED_FINISH_OFF);
  ledSettings.finishSeconds    = prefs.getUShort("led_fx_sec", 60);
  ledSettings.finishBrightness = prefs.getUChar ("led_fx_br",  255);

  ledSettings.autoOnWhilePrinting = prefs.getBool("led_auto_pr", false);
  ledSettings.pauseBreathing      = prefs.getBool("led_pause",   false);
  ledSettings.errorStrobe         = prefs.getBool("led_err",     false);

  // Cloud email (display only)
  strlcpy(cloudEmail, prefs.getString("cl_email", "").c_str(), sizeof(cloudEmail));

  // Tasmota power monitoring — array of N plugs with numbered NVS keys
  // One-shot migration from legacy singleton keys (tsm_en/ip/dm/pi/slot) into
  // numbered keys (tsm0_*, tsm1_*). Runs once when legacy keys exist and
  // tsm0_en is absent. Legacy keys are removed after migration completes.
  if (prefs.isKey("tsm_en") && !prefs.isKey("tsm0_en")) {
    bool    legEn   = prefs.getBool ("tsm_en",   false);
    String  legIp   = prefs.getString("tsm_ip",  "");
    uint8_t legDm   = prefs.getUChar("tsm_dm",   0);
    uint8_t legPi   = prefs.getUChar("tsm_pi",   10);
    uint8_t legSlot = prefs.getUChar("tsm_slot", 255);
    if (legSlot != 255 && legSlot >= MAX_ACTIVE_PRINTERS) legSlot = 255;

#if TASMOTA_PLUG_COUNT == 1
    // Single-plug build: copy to plug 0 and keep assignedSlot
    prefs.putBool ("tsm0_en",  legEn);
    prefs.putString("tsm0_ip", legIp);
    prefs.putUChar("tsm0_dm",  legDm);
    prefs.putUChar("tsm0_pi",  legPi);
    prefs.putUChar("tsm0_as",  legSlot);
#else
    // Dual-plug build: route to plug index 0 or 1 based on legacy slot
    uint8_t targetPlug = (legSlot == 1) ? 1 : 0;
    char k[12];
    snprintf(k, sizeof(k), "tsm%u_en", targetPlug);  prefs.putBool(k, legEn);
    snprintf(k, sizeof(k), "tsm%u_ip", targetPlug);  prefs.putString(k, legIp);
    snprintf(k, sizeof(k), "tsm%u_dm", targetPlug);  prefs.putUChar(k, legDm);
    snprintf(k, sizeof(k), "tsm%u_pi", targetPlug);  prefs.putUChar(k, legPi);
#endif
    prefs.remove("tsm_en");
    prefs.remove("tsm_ip");
    prefs.remove("tsm_dm");
    prefs.remove("tsm_pi");
    prefs.remove("tsm_slot");
    Serial.println("[SETTINGS] Migrated legacy Tasmota keys to numbered scheme");
  }

  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; i++) {
    char k[12];
    snprintf(k, sizeof(k), "tsm%u_en",  i); tasmotaSettings[i].enabled = prefs.getBool(k, false);
    snprintf(k, sizeof(k), "tsm%u_ip",  i); strlcpy(tasmotaSettings[i].ip, prefs.getString(k, "").c_str(), sizeof(tasmotaSettings[i].ip));
    snprintf(k, sizeof(k), "tsm%u_dm",  i); tasmotaSettings[i].displayMode = prefs.getUChar(k, 0);
    snprintf(k, sizeof(k), "tsm%u_pi",  i); {
      uint8_t pi = prefs.getUChar(k, 10);
      if (pi < 10 || pi > 60) pi = 10;
      tasmotaSettings[i].pollInterval = pi;
    }
    snprintf(k, sizeof(k), "tsm%u_ao",  i); tasmotaSettings[i].autoOffEnabled = prefs.getBool(k, false);
    snprintf(k, sizeof(k), "tsm%u_ad",  i); {
      uint8_t ad = prefs.getUChar(k, 10);
      if (ad < 1 || ad > 240) ad = 10;
      tasmotaSettings[i].autoOffDelayMin = ad;
    }
    snprintf(k, sizeof(k), "tsm%u_aod", i); tasmotaSettings[i].autoOffCancelOnDoor = prefs.getBool(k, false);
#if TASMOTA_PLUG_COUNT == 1
    snprintf(k, sizeof(k), "tsm%u_as",  i); {
      uint8_t a = prefs.getUChar(k, 255);
      if (a != 255 && a >= MAX_ACTIVE_PRINTERS) a = 255;
      tasmotaSettings[i].assignedSlot = a;
    }
#endif
  }
  strlcpy(tasmotaCurrency, prefs.getString("tsm_cur", "\xE2\x82\xAC").c_str(), sizeof(tasmotaCurrency));
  {
    float t = prefs.getFloat("tsm_tariff", 0.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 10.0f) t = 10.0f;
    tasmotaTariffPerKwh = t;
  }

  // Experimental dual-printer override on BOARD_LOW_RAM (local-only, not exported)
  dualPrinterUnsafe = prefs.getBool("dualp", false);

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
  prefs.putUShort("dsp_pbar", dispSettings.progressBarColor);
  prefs.putBool("dsp_abar", dispSettings.animatedBar);
  prefs.putBool("dsp_pong", dispSettings.pongClock);
  prefs.putBool("dsp_slbl", dispSettings.smallLabels);
  prefs.putBool("dsp_shtire", dispSettings.showTimeRemaining);
  prefs.putBool("dsp_fanmp", dispSettings.fanMatchPrinter);
  prefs.putBool("dsp_inv", dispSettings.invertColors);
  prefs.putBool("dsp_cydcls", dispSettings.cydPanelClassic);
  prefs.putBool("dsp_l8s", dispSettings.landscape8Slots);
  prefs.putBool("dsp_p9s", dispSettings.portrait9Slots);
  prefs.putUShort("dsp_clkt", dispSettings.clockTimeColor);
  prefs.putUShort("dsp_clkd", dispSettings.clockDateColor);
  prefs.putUChar("dsp_clkts", dispSettings.clockTimeSize);
  prefs.putBool("dsp_clkhd", dispSettings.hideClockDate);
  prefs.putBool("dsp_clkif", dispSettings.showClockInfo);
  prefs.putBool("dsp_amst", dispSettings.amsTrayTypes);
  prefs.putBool("dsp_bat", dispSettings.showBatteryIndicator);
  prefs.putUShort("dsp_nozmx", dispSettings.nozzleScaleMax);
  prefs.putUShort("dsp_bedmx", dispSettings.bedScaleMax);
  prefs.putUShort("dsp_chbmx", dispSettings.chamberScaleMax);
  prefs.putUShort("dsp_pwrmx", dispSettings.powerScaleW);
  prefs.putUChar("dsp_smooth", dispSettings.gaugeSmoothing);
  prefs.putUShort("dsp_wclr", dispSettings.warnColor);
  prefs.putUChar("dsp_wthr", dispSettings.warnThresholdPct);

  saveGaugeColors("gc_prg", dispSettings.progress);
  saveGaugeColors("gc_noz", dispSettings.nozzle);
  saveGaugeColors("gc_bed", dispSettings.bed);
  saveGaugeColors("gc_pfn", dispSettings.partFan);
  saveGaugeColors("gc_afn", dispSettings.auxFan);
  saveGaugeColors("gc_afr", dispSettings.auxFanRight);
  saveGaugeColors("gc_cfn", dispSettings.chamberFan);
  saveGaugeColors("gc_exh", dispSettings.exhaustFan);
  saveGaugeColors("gc_cht", dispSettings.chamberTemp);
  saveGaugeColors("gc_hbk", dispSettings.heatbreak);

  // Network settings
  prefs.putBool("net_dhcp", netSettings.useDHCP);
  prefs.putString("net_ip", netSettings.staticIP);
  prefs.putString("net_gw", netSettings.gateway);
  prefs.putString("net_sn", netSettings.subnet);
  prefs.putString("net_dns", netSettings.dns);
  prefs.putBool("net_showip", netSettings.showIPAtStartup);
  prefs.putBool("net_mdns", netSettings.mdnsEnabled);
  prefs.putString("net_host", netSettings.hostname);
  prefs.putString("net_tzstr", netSettings.timezoneStr);
  prefs.putUChar("net_tzidx", netSettings.timezoneIndex);
  prefs.putBool("net_24h", netSettings.use24h);
  prefs.putUChar("net_datefmt", netSettings.dateFormat);

  // Display power settings
  prefs.putUShort("dp_fmins", dpSettings.finishDisplayMins);
  prefs.putBool("dp_keepon", dpSettings.keepDisplayOn);
  prefs.putBool("dp_clock", dpSettings.showClockAfterFinish);
  prefs.putBool("dp_dack", dpSettings.doorAckEnabled);
  prefs.putBool("dp_kps", dpSettings.keepPrintScreen);
  prefs.putBool("dp_night", dpSettings.nightModeEnabled);
  prefs.putUChar("dp_nstart", dpSettings.nightStartHour);
  prefs.putUChar("dp_nend", dpSettings.nightEndHour);
  prefs.putUChar("dp_nbright", dpSettings.nightBrightness);
  prefs.putUChar("dp_ssbright", dpSettings.screensaverBrightness);

  // Tasmota power monitoring — numbered keys per plug
  for (uint8_t i = 0; i < TASMOTA_PLUG_COUNT; i++) {
    char k[12];
    // Clamp on save too in case anything ever assigns out-of-range values
    uint8_t pi = tasmotaSettings[i].pollInterval;
    if (pi < 10 || pi > 60) pi = 10;
    uint8_t ad = tasmotaSettings[i].autoOffDelayMin;
    if (ad < 1 || ad > 240) ad = 10;

    snprintf(k, sizeof(k), "tsm%u_en",  i); prefs.putBool(k, tasmotaSettings[i].enabled);
    snprintf(k, sizeof(k), "tsm%u_ip",  i); prefs.putString(k, tasmotaSettings[i].ip);
    snprintf(k, sizeof(k), "tsm%u_dm",  i); prefs.putUChar(k, tasmotaSettings[i].displayMode);
    snprintf(k, sizeof(k), "tsm%u_pi",  i); prefs.putUChar(k, pi);
    snprintf(k, sizeof(k), "tsm%u_ao",  i); prefs.putBool(k, tasmotaSettings[i].autoOffEnabled);
    snprintf(k, sizeof(k), "tsm%u_aod", i); prefs.putBool(k, tasmotaSettings[i].autoOffCancelOnDoor);
    snprintf(k, sizeof(k), "tsm%u_ad",  i); prefs.putUChar(k, ad);
#if TASMOTA_PLUG_COUNT == 1
    uint8_t a = tasmotaSettings[i].assignedSlot;
    if (a != 255 && a >= MAX_ACTIVE_PRINTERS) a = 255;
    snprintf(k, sizeof(k), "tsm%u_as",  i); prefs.putUChar(k, a);
#endif
  }
  prefs.putString("tsm_cur", tasmotaCurrency);
  {
    float t = tasmotaTariffPerKwh;
    if (t < 0.0f) t = 0.0f;
    if (t > 10.0f) t = 10.0f;
    prefs.putFloat("tsm_tariff", t);
  }

  // Experimental dual-printer override on BOARD_LOW_RAM (local-only, not exported)
  prefs.putBool("dualp", dualPrinterUnsafe);

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

  snprintf(key, sizeof(key), "p%d_slots", index);
  prefs.putBytes(key, cfg.gaugeSlots, sizeof(cfg.gaugeSlots));
  snprintf(key, sizeof(key), "p%d_lext", index);
  prefs.putBytes(key, cfg.landscapeExtras, sizeof(cfg.landscapeExtras));
  snprintf(key, sizeof(key), "p%d_pext", index);
  prefs.putBytes(key, cfg.portraitExtras, sizeof(cfg.portraitExtras));

  snprintf(key, sizeof(key), "p%d_amsv", index);
  prefs.putBool(key, cfg.amsView);

  if (needOpen) prefs.end();
}

void clearPrinterConfig(uint8_t index) {
  if (index >= MAX_PRINTERS) return;
  PrinterConfig& cfg = printers[index].config;
  memset(&cfg, 0, sizeof(cfg));
  cfg.mode   = CONN_LOCAL;
  cfg.region = REGION_US;
  defaultGaugeSlots(cfg.gaugeSlots);
  memset(cfg.landscapeExtras, GAUGE_EMPTY, sizeof(cfg.landscapeExtras));
  memset(cfg.portraitExtras, GAUGE_EMPTY, sizeof(cfg.portraitExtras));
  savePrinterConfig(index);
}

void saveRotationSettings() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("rot_mode", rotState.mode);
  prefs.putULong("rot_intv", rotState.intervalMs);
  prefs.end();
}

void saveButtonSettings() {
  sanitizeButtonPin();
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("btn_type", buttonType);
  prefs.putUChar("btn_pin", buttonPin);
  prefs.end();
}

void saveBuzzerSettings() {
  sanitizeBuzzerPin();
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("buz_on", buzzerSettings.enabled);
  prefs.putUChar("buz_pin", buzzerSettings.pin);
  prefs.putUChar("buz_qstart", buzzerSettings.quietStartHour);
  prefs.putUChar("buz_qend", buzzerSettings.quietEndHour);
  prefs.putBool("buz_click", buzzerSettings.buttonClick);
  prefs.putBool("buz_bed_on", buzzerSettings.bedCooldownAlert);
  prefs.putUChar("buz_bed_c", buzzerSettings.bedCooldownThresholdC);
  prefs.end();
}

// External LED — only path that writes LED to NVS. Always sanitizes first so
// no invalid pin (peripheral conflict, input-only, flash, etc.) ever reaches
// persistent storage. LED is intentionally NOT in saveSettings().
void saveLedSettings() {
  sanitizeLedPin();
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool ("led_on",  ledSettings.enabled);
  prefs.putUChar("led_pin", ledSettings.pin);
  prefs.putUChar("led_br",  ledSettings.brightness);

  prefs.putUChar ("led_fx_md",  ledSettings.finishMode);
  prefs.putUShort("led_fx_sec", ledSettings.finishSeconds);
  prefs.putUChar ("led_fx_br",  ledSettings.finishBrightness);

  prefs.putBool("led_auto_pr", ledSettings.autoOnWhilePrinting);
  prefs.putBool("led_pause",   ledSettings.pauseBreathing);
  prefs.putBool("led_err",     ledSettings.errorStrobe);
  prefs.end();
}

void saveBatteryIndicatorSetting() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("dsp_bat", dispSettings.showBatteryIndicator);
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
