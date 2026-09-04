// =============================================================================
//  web_template.cpp - PROGMEM page streamer and %TOKEN% placeholder resolver.
//
//  PROGMEM string literals (PAGE_HTML, PAGE_AP_HTML) are defined in
//  include/web_pages.h, which is included only here.
//
//  serveMainPage() streams PAGE_HTML in 2 KB chunks via HTTP chunked transfer,
//  substituting %FOO% placeholders inline. Peak heap during page render is the
//  2 KB buffer plus any per-placeholder String allocation - typically <3 KB.
//
//  serveApPage() sends the small AP-mode page verbatim. No substitution.
// =============================================================================

#include "web_template.h"
#include "web_pages.h"

#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "config.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "timezones.h"
#include "esp_ota_ops.h"   // OTASLOT token: next update partition size
#include "tasmota.h"
#include <Arduino.h>

#ifndef BOARD_NAME
#define BOARD_NAME BOARD_VARIANT
#endif
#ifndef BOARD_PANEL
#define BOARD_PANEL "Display"
#endif

// LED colours are full 24-bit, not the RGB565 the display settings use, so they
// need their own formatter rather than rgb565ToHtml().
static String ledHexColor(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)(rgb & 0xFFFFFF));
  return String(buf);
}

// ---------------------------------------------------------------------------
//  Resolve a single template placeholder to its value.
//  Returns true if name was a known placeholder (even if value is empty).
// ---------------------------------------------------------------------------
static bool resolvePlaceholder(const char* name, String& out) {
  PrinterConfig& cfg = printers[0].config;
  BambuState& st = printers[0].state;
  char buf[8];

  // --- Printer ---
  if (strcmp(name, "SSID") == 0)            { out = wifiSSID; return true; }
  if (strcmp(name, "MODE_LOCAL") == 0)      { out = cfg.mode == CONN_LOCAL ? "selected" : ""; return true; }
  if (strcmp(name, "MODE_CLOUD_ALL") == 0)  { out = isCloudMode(cfg.mode) ? "selected" : ""; return true; }
  if (strcmp(name, "PNAME") == 0)           { out = cfg.name; return true; }
  if (strcmp(name, "IP") == 0)              { out = cfg.ip; return true; }
  if (strcmp(name, "SERIAL") == 0)          { out = cfg.serial; return true; }
  if (strcmp(name, "REGION_US") == 0)       { out = cfg.region == REGION_US ? "selected" : ""; return true; }
  if (strcmp(name, "REGION_EU") == 0)       { out = cfg.region == REGION_EU ? "selected" : ""; return true; }
  if (strcmp(name, "REGION_CN") == 0)       { out = cfg.region == REGION_CN ? "selected" : ""; return true; }
  if (strcmp(name, "CLOUD_STATUS") == 0) {
    char tokenBuf[32];
    if (!loadCloudToken(tokenBuf, sizeof(tokenBuf))) { out = "No token set"; return true; }
    out = "Token active";
    char email[96];
    if (loadCloudEmail(email, sizeof(email))) { out += " ("; out += email; out += ")"; }
    return true;
  }

  // Cache-busting hashes for the gzipped assets the page pulls in.
  if (strcmp(name, "CSSVER") == 0)          { out = webAssetCssVersion(); return true; }
  if (strcmp(name, "JSVER") == 0)           { out = webAssetJsVersion(); return true; }

  // --- Brightness / Night mode ---
  if (strcmp(name, "BL_DISP") == 0) {
    // Inline style for the Brightness card and the screensaver-brightness
    // field. Boards without a controllable backlight (7-pin round GC9A01
    // modules wire BLK hardwired on, BACKLIGHT_PIN=-1) hide them entirely -
    // setBacklight() is a no-op there and the sliders would do nothing.
#if defined(BACKLIGHT_PIN) && (BACKLIGHT_PIN >= 0)
    out = "";
#else
    out = "display:none";
#endif
    return true;
  }
  if (strcmp(name, "BRIGHT") == 0)          { out = String(brightness); return true; }
  if (strcmp(name, "NIGHTEN") == 0)         { out = dpSettings.nightModeEnabled ? "checked" : ""; return true; }
  if (strcmp(name, "NIGHTDISP") == 0)       { out = dpSettings.nightModeEnabled ? "block" : "none"; return true; }
  if (strcmp(name, "NBRIGHT") == 0)         { out = String(dpSettings.nightBrightness); return true; }
  if (strcmp(name, "SSBRIGHT") == 0)        { out = String(dpSettings.screensaverBrightness); return true; }
  if (strcmp(name, "NIGHT_START_OPTS") == 0) {
    out = "";
    for (uint8_t h = 0; h < 24; h++) {
      char opt[64];
      snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>%02d:00</option>",
               h, h == dpSettings.nightStartHour ? " selected" : "", h);
      out += opt;
    }
    return true;
  }
  if (strcmp(name, "NIGHT_END_OPTS") == 0) {
    out = "";
    for (uint8_t h = 0; h < 24; h++) {
      char opt[64];
      snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>%02d:00</option>",
               h, h == dpSettings.nightEndHour ? " selected" : "", h);
      out += opt;
    }
    return true;
  }

  // --- Network ---
  if (strcmp(name, "NET_DHCP") == 0)   { out = netSettings.useDHCP ? "selected" : ""; return true; }
  if (strcmp(name, "NET_STATIC") == 0) { out = netSettings.useDHCP ? "" : "selected"; return true; }
  if (strcmp(name, "NET_IP") == 0)     { out = netSettings.staticIP; return true; }
  if (strcmp(name, "NET_GW") == 0)     { out = netSettings.gateway; return true; }
  if (strcmp(name, "NET_SN") == 0)     { out = netSettings.subnet; return true; }
  if (strcmp(name, "NET_DNS") == 0)    { out = netSettings.dns; return true; }
  if (strcmp(name, "SHOWIP") == 0)     { out = netSettings.showIPAtStartup ? "checked" : ""; return true; }
  if (strcmp(name, "MDNS_EN") == 0)    { out = netSettings.mdnsEnabled ? "checked" : ""; return true; }
  if (strcmp(name, "MDNS_HOST") == 0)  { out = netSettings.hostname; return true; }

  // --- Clock ---
  if (strcmp(name, "USE24H") == 0)     { out = netSettings.use24h ? "checked" : ""; return true; }
  if (strncmp(name, "DATEFMT", 7) == 0 && name[7] >= '0' && name[7] <= '5' && name[8] == '\0') {
    out = netSettings.dateFormat == (name[7] - '0') ? "selected" : "";
    return true;
  }
  if (strncmp(name, "CLKSZ", 5) == 0 && name[5] >= '0' && name[5] <= '3' && name[6] == '\0') {
    out = dispSettings.clockTimeSize == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  if (strncmp(name, "CLKDS", 5) == 0 && name[5] >= '0' && name[5] <= '3' && name[6] == '\0') {
    out = dispSettings.clockDateSize == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  if (strcmp(name, "CLK_HIDEDATE") == 0) { out = dispSettings.hideClockDate ? "checked" : ""; return true; }

  // --- Display rotation ---
  if (strncmp(name, "ROT", 3) == 0 && name[3] >= '0' && name[3] <= '3' && name[4] == '\0') {
    out = dispSettings.rotation == (name[3] - '0') ? "selected" : "";
    return true;
  }

  // --- After-print ---
  {
    uint16_t fm = dpSettings.finishDisplayMins;
    bool keepon = dpSettings.keepDisplayOn;
    bool isPreset = (!keepon && (fm == 0 || fm == 1 || fm == 3 || fm == 5 || fm == 10));
    if (strcmp(name, "AP_CLOCK0") == 0)    { out = (!keepon && fm == 0) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_F1") == 0)        { out = (!keepon && fm == 1) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_F3") == 0)        { out = (!keepon && fm == 3) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_F5") == 0)        { out = (!keepon && fm == 5) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_F10") == 0)       { out = (!keepon && fm == 10) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_CUSTOM") == 0)    { out = (!keepon && !isPreset && fm > 0) ? "selected" : ""; return true; }
    if (strcmp(name, "AP_KEEPON") == 0)    { out = keepon ? "selected" : ""; return true; }
    if (strcmp(name, "CUSTOM_DISP") == 0)  { out = (!keepon && !isPreset && fm > 0) ? "block" : "none"; return true; }
    if (strcmp(name, "FMINS") == 0)        { out = String(fm); return true; }
    // Destination after the finish screen: clock (default) or display+LED off.
    if (strcmp(name, "AF_CLOCK") == 0)     { out = dpSettings.showClockAfterFinish ? "selected" : ""; return true; }
    if (strcmp(name, "AF_OFF") == 0)       { out = dpSettings.showClockAfterFinish ? "" : "selected"; return true; }
  }

  // --- Per-slot printer tabs / topbar dots (slots 1..MAX_ACTIVE_PRINTERS-1) ---
  // Slot 0's tab and dot are hardcoded in PAGE_HTML; these emit the remainder.
  // %PRINTER_TABS%    - "Printer N" tab buttons for slots >= 1.
  // %TOPBAR_DOTS%     - matching topbar status pills for slots >= 1.
  // %DUALP_ADVANCED%  - Advanced disclosure card with the experimental toggle.
  // On LOW_RAM (MAX_ACTIVE_PRINTERS == 2) the loops emit only slot 1, with the
  // experimental hide logic intact, so the page stays byte-identical to before.
  if (strcmp(name, "PRINTER_TABS") == 0) {
    for (uint8_t i = 1; i < MAX_ACTIVE_PRINTERS; i++) {
      out += "<button class=\"tab-btn\" id=\"tab" + String(i) +
             "\" type=\"button\" onclick=\"selectPrinterTab(" + String(i) + ")\"";
#ifdef BOARD_LOW_RAM
      // Slot 1 stays hidden until the experimental toggle enables it;
      // toggleDualPrinterMode() clears the inline style without a reload.
      if (i == 1 && !dualPrinterUnsafe) out += " style=\"display:none\"";
#endif
#ifdef BOARD_HAS_PSRAM
      // Slots 2-3 stay hidden until the experimental 4-printer toggle enables
      // them; toggleQuadPrinterMode() clears the inline style without a reload.
      if (i >= 2 && !quadPrinterBeta) out += " style=\"display:none\"";
#endif
      out += ">Printer " + String(i + 1) + "</button>";
    }
    return true;
  }
  if (strcmp(name, "TOPBAR_DOTS") == 0) {
    for (uint8_t i = 1; i < MAX_ACTIVE_PRINTERS; i++) {
      out += "<span class=\"status-dot\" id=\"topStatusDot" + String(i) +
             "\" title=\"Printer " + String(i + 1) + " connection\"";
#ifdef BOARD_LOW_RAM
      if (i == 1 && !dualPrinterUnsafe) out += " style=\"display:none\"";
#endif
#ifdef BOARD_HAS_PSRAM
      if (i >= 2 && !quadPrinterBeta) out += " style=\"display:none\"";
#endif
      out += "><span id=\"topStatusText" + String(i) + "\">-</span></span>";
    }
    return true;
  }
  if (strcmp(name, "MAXP") == 0) {
    switch (MAX_ACTIVE_PRINTERS) {
      case 4:  out = "four";  break;
      case 3:  out = "three"; break;
      default: out = "two";   break;
    }
    return true;
  }
  if (strcmp(name, "DUALP_ADVANCED") == 0) {
    // Renders bare inside the Advanced > Danger Zone card; no outer card
    // wrapper. The danger-zone gate checkbox controls the surrounding
    // <div id="dangerOps"> visibility.
#ifdef BOARD_LOW_RAM
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"dualp\" value=\"1\" ";
    if (dualPrinterUnsafe) out += "checked";
    out += " onchange=\"toggleDualPrinterMode(this.checked)\">";
    out += "<label for=\"dualp\">Enable 2-printer mode (experimental on low-RAM boards: two simultaneous TLS+MQTT sessions may exhaust the heap and crash the device)</label>";
    out += "</label>";
#elif defined(BOARD_HAS_PSRAM)
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"quadp\" value=\"1\" ";
    if (quadPrinterBeta) out += "checked";
    out += " onchange=\"toggleQuadPrinterMode(this.checked)\">";
    out += "<label for=\"quadp\">Enable 4-printer mode (experimental: running 3-4 printers at once is not yet validated and may exhaust the heap or cause unstable connections. The split dual-screen layout still shows only the first two printers.)</label>";
    out += "</label>";
#else
    out = "";
#endif
    return true;
  }

  // --- Display options ---
  if (strcmp(name, "DACK") == 0)   { out = dpSettings.doorAckEnabled ? "checked" : ""; return true; }
  if (strcmp(name, "KPS") == 0)    { out = dpSettings.keepPrintScreen ? "checked" : ""; return true; }
  if (strcmp(name, "FINTM") == 0)  { out = dpSettings.finishShowTime ? "checked" : ""; return true; }
  if (strcmp(name, "ABAR") == 0)   { out = dispSettings.animatedBar ? "checked" : ""; return true; }
  if (strcmp(name, "PONG") == 0)   { out = dispSettings.pongClock ? "checked" : ""; return true; }
  if (strcmp(name, "SLBL") == 0)   { out = dispSettings.smallLabels ? "checked" : ""; return true; }
  if (strncmp(name, "TIMEM", 5) == 0 && name[5] >= '0' && name[5] <= '2' && name[6] == '\0') {
    out = dispSettings.timeDisplayMode == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  // Edge glow: mode/style/duration selects + reveal states. Shown on every
  // layout - round panels render the glow as a rim ring.
  if (strncmp(name, "GLOWM", 5) == 0 && name[5] >= '0' && name[5] <= '2' && name[6] == '\0') {
    out = dispSettings.glowMode == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  if (strncmp(name, "GLOWS", 5) == 0 && name[5] >= '0' && name[5] <= '2' && name[6] == '\0') {
    out = dispSettings.glowStyle == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  if (strncmp(name, "GLOWD", 5) == 0 && name[5] >= '0' && name[5] <= '2' && name[6] == '\0') {
    out = dispSettings.glowDuration == (uint8_t)(name[5] - '0') ? "selected" : "";
    return true;
  }
  if (strcmp(name, "GLOW_DISP") == 0) {
    out = "";
    return true;
  }
  // Printer errors. The nav entry is a placeholder rather than page markup so
  // it disappears along with the section itself where the feature is compiled
  // out. The section's JS lives in web/app.js and guards on its markup being
  // present, since that file is shared by every board and sees no #if.
  if (strcmp(name, "HMS_NAV") == 0) {
#if HAS_HMS_WEB_UI
    out = F("  <button class=\"nav-item\" type=\"button\" data-section=\"errors\">"
            "<span>Printer Errors</span></button>");
#else
    out = "";
#endif
    return true;
  }
#if HAS_HMS_WEB_UI
  // HMSA<n> is the auto-present mode; HMSM<n> the four alert checkboxes, one
  // bit each (0 glow, 1 buzzer, 2 LED, 3 wake).
  if (strncmp(name, "HMSA", 4) == 0 && name[4] >= '0' && name[4] <= '2' && name[5] == '\0') {
    out = dispSettings.hmsAutoPresent == (uint8_t)(name[4] - '0') ? "selected" : "";
    return true;
  }
  if (strncmp(name, "HMSM", 4) == 0 && name[4] >= '0' && name[4] <= '3' && name[5] == '\0') {
    out = (dispSettings.hmsAlertMask & (1 << (name[4] - '0'))) ? "checked" : "";
    return true;
  }
  if (strcmp(name, "HMS_EN") == 0)  { out = dispSettings.hmsEnabled ? "checked" : ""; return true; }
  if (strcmp(name, "HMS_SEV") == 0) { out = dispSettings.hmsSeverityAll ? "checked" : ""; return true; }
  if (strcmp(name, "HMS_DISP") == 0) { out = dispSettings.hmsEnabled ? "block" : "none"; return true; }
  if (strcmp(name, "HMS_ONL") == 0) { out = dispSettings.hmsLookupOnline ? "checked" : ""; return true; }
  if (strcmp(name, "HMS_ONL_DISP") == 0) {
    // Nothing to look up where the sentences are compiled in.
#if HAS_FULL_HMS_TABLE
    out = "none";
#else
    out = "block";
#endif
    return true;
  }
#endif  // HAS_HMS_WEB_UI
  if (strcmp(name, "GLOWF_DISP") == 0) { out = dispSettings.glowMode != 0 ? "block" : "none"; return true; }
  if (strcmp(name, "GLOWC_DISP") == 0) { out = dispSettings.glowMode == 1 ? "block" : "none"; return true; }
  if (strcmp(name, "FMP") == 0)    { out = dispSettings.fanMatchPrinter ? "checked" : ""; return true; }
  if (strcmp(name, "HIDELP") == 0) { out = dispSettings.hideStatusReadout ? "checked" : ""; return true; }
  if (strcmp(name, "CLK_INFO") == 0) { out = dispSettings.showClockInfo ? "checked" : ""; return true; }
  if (strcmp(name, "BTN_PWR") == 0) { out = dispSettings.buttonPowerControl ? "checked" : ""; return true; }
  if (strcmp(name, "AMST_ROW") == 0) {
    // Per-tray filament-type labels only render in the enhanced portrait AMS
    // strip, and only 320x480 (Guition / ws_lcd_350) drives the 3-AMS case
    // where the labels get too cramped to read. On 240x320 the 3-AMS view
    // never enters the enhanced layout, so the toggle would be a no-op there.
    // Gate the row to the layouts where it actually does something.
#if defined(DISPLAY_320x480)
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"amst\" value=\"1\" ";
    out += dispSettings.amsTrayTypes ? "checked" : "";
    out += " onchange=\"toggleSetting('amst',this.checked)\">";
    out += "<label for=\"amst\">Show filament type under AMS bars</label>";
    out += "</label>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "INVCOL_ROW") == 0) {
#if defined(DISPLAY_240x320)
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"invcol\" value=\"1\" ";
    out += dispSettings.invertColors ? "checked" : "";
    out += " onchange=\"toggleSetting('invcol',this.checked)\">";
    out += "<label for=\"invcol\">Invert display colors (fix white background)</label>";
    out += "</label>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "CYD_PANEL_ROW") == 0) {
#if defined(DISPLAY_CYD)
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"cydcls\" value=\"1\" ";
    out += dispSettings.cydPanelClassic ? "checked" : "";
    out += " onchange=\"toggleSetting('cydcls',this.checked)\">";
    out += "<label for=\"cydcls\">Use Classic CYD panel fallback (older units only - device will reboot)</label>";
    out += "</label>";
    out += "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"cyd32e\" value=\"1\" ";
    out += dispSettings.cyd32eVariant ? "checked" : "";
    out += " onchange=\"toggleSetting('cyd32e',this.checked)\">";
    out += "<label for=\"cyd32e\">ESP32-32E clone board (fixes silent speaker + stuck red LED - device will reboot)</label>";
    out += "</label>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "ROUND_SKIN_ROW") == 0) {
#if defined(DISPLAY_ROUND_240)
    // Round boards only: printing dashboard skin picker. Posts through the
    // same /save/toggle endpoint as the checkboxes (val carries 0-2).
    auto sel = [&](uint8_t v) { return dispSettings.roundSkin == v ? " selected" : ""; };
    out  = "<div class=\"field\"><label for=\"rskin\">Print dashboard skin</label>";
    out += "<select id=\"rskin\" onchange=\"toggleSetting('rskin',this.value)\">";
    out += "<option value=\"0\""; out += sel(0); out += ">Rim (progress ring + mini gauges)</option>";
    out += "<option value=\"1\""; out += sel(1); out += ">Speedo (large 240&deg; arc)</option>";
    out += "<option value=\"2\""; out += sel(2); out += ">Rings (concentric progress/nozzle/bed)</option>";
    out += "</select>";
    out += "<span class=\"text-dim small\">applies immediately</span></div>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "EXTENDED_MODES_CARD") == 0) {
    // Card lives in the Advanced section. Only renders on layouts that
    // actually have extended grid modes, otherwise the whole card is empty.
#if defined(DISPLAY_240x320) || defined(DISPLAY_320x480)
    out  = "<div class=\"card\">";
    out += "<div class=\"card-head\"><div><h3>Extended grid modes</h3>";
    out += "<p>Trade the on-screen AMS area for extra gauge slots. ";
    out += "Configure the new slots under <strong>Gauge Layout</strong>.</p>";
    out += "</div></div>";
    out += "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"l8s\" value=\"1\" ";
    out += dispSettings.landscape8Slots ? "checked" : "";
    out += " onchange=\"toggleGridMode('l8s', this.checked)\">";
    out += "<label for=\"l8s\">Landscape 8 gauge slots (replaces AMS sidebar with a 2x4 grid)</label>";
    out += "</label>";
    out += "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"p9s\" value=\"1\" ";
    out += dispSettings.portrait9Slots ? "checked" : "";
    out += " onchange=\"toggleGridMode('p9s', this.checked)\">";
    out += "<label for=\"p9s\">Portrait 9 gauge slots (replaces AMS strip with a 3x3 grid)</label>";
    out += "</label>";
    out += "</div>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "EXTRAS_SECTIONS") == 0) {
    // Two independent extras blocks - landscape col 4 + portrait row 3.
    // Each is gauge-type-configured per-printer through landscapeExtras /
    // portraitExtras. Boards without LAYOUT_HAS_LANDSCAPE / LY_PORT9_GAUGE_R
    // don't render anything here so the user isn't offered settings the
    // device can't use. Inline display:none reflects the toggle's state at
    // render time; toggleGridMode() in the Advanced section keeps it in sync.
#if defined(DISPLAY_240x320) || defined(DISPLAY_320x480)
    out  = "<div id=\"landExtrasGroup\"";
    if (!dispSettings.landscape8Slots) out += " style=\"display:none\"";
    out += "><div class=\"row-divider\">&#9656; Landscape extras (column 4, used by <em>Landscape 8 slots</em>)</div>";
    out += "<div class=\"gauge-grid\">";
    out += "<div class=\"cell\"><label>Col 4 top</label><select id=\"lx0\" class=\"gauge-slot-sel\"></select></div>";
    out += "<div class=\"cell\"><label>Col 4 bot</label><select id=\"lx1\" class=\"gauge-slot-sel\"></select></div>";
    out += "</div></div>";
    out += "<div id=\"portExtrasGroup\"";
    if (!dispSettings.portrait9Slots) out += " style=\"display:none\"";
    out += "><div class=\"row-divider\">&#9656; Portrait extras (row 3, used by <em>Portrait 9 slots</em>)</div>";
    out += "<div class=\"gauge-grid\">";
    out += "<div class=\"cell\"><label>Row 3 left</label><select id=\"px0\" class=\"gauge-slot-sel\"></select></div>";
    out += "<div class=\"cell\"><label>Row 3 mid</label><select id=\"px1\" class=\"gauge-slot-sel\"></select></div>";
    out += "<div class=\"cell\"><label>Row 3 right</label><select id=\"px2\" class=\"gauge-slot-sel\"></select></div>";
    out += "</div></div>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "PONG_DISP") == 0) {
    // Inline style for the Breakout-clock row. The game plays in a rectangle;
    // on round panels the walls and corner bricks sit outside the visible
    // circle, so the firmware never runs it there (display_ui always draws
    // the watch-face clock) and the option is hidden.
#if defined(DISPLAY_ROUND_240)
    out = "display:none";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "ISROUND") == 0) {
    // JS board flag: the Gauge Layout card re-labels itself for round boards
    // (3 Rim-skin mini slots, no bottom row / AMS view / extras).
#if defined(DISPLAY_ROUND_240)
    out = "1";
#else
    out = "0";
#endif
    return true;
  }
  if (strcmp(name, "HMSFULL") == 0) {
    // JS board flag: this device carries the full HMS sentence table, so the
    // error card must not fetch the published mirror. A code that has no text
    // here is blank in Bambu's feed, and the mirror is generated from that same
    // feed with the same blanks dropped - the download could only ever fail to
    // help, at 500+ KB a page load.
#if HAS_FULL_HMS_TABLE
    out = "1";
#else
    out = "0";
#endif
    return true;
  }
  if (strcmp(name, "AMSV_ROW") == 0) {
    // AMS view swaps gauge row 2 for the AMS strip - 240x240 square boards
    // only. Round boards have no AMS strip, big boards show AMS natively.
#if !defined(DISPLAY_240x320) && !defined(DISPLAY_320x480) && \
    !defined(DISPLAY_480x480) && !defined(DISPLAY_ROUND_240)
    out  = "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"amsv\" value=\"1\" onchange=\"syncAmsView()\">";
    out += "<label for=\"amsv\">AMS view (replaces bottom gauges)</label>";
    out += "</label>";
#else
    out = "";
#endif
    return true;
  }

  // --- Colors (global + per-gauge) ---
  if (strcmp(name, "CLR_BG") == 0)    { rgb565ToHtml(dispSettings.bgColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_TRACK") == 0) { rgb565ToHtml(dispSettings.trackColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_PBAR") == 0)  { rgb565ToHtml(dispSettings.progressBarColor, buf); out = buf; return true; }
  if (strcmp(name, "GLOW_CLR") == 0)  { rgb565ToHtml(dispSettings.glowColor, buf); out = buf; return true; }
  if (strcmp(name, "CLK_TIME") == 0)  { rgb565ToHtml(dispSettings.clockTimeColor, buf); out = buf; return true; }
  if (strcmp(name, "CLK_DATE") == 0)  { rgb565ToHtml(dispSettings.clockDateColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_ETA") == 0)   { rgb565ToHtml(dispSettings.etaColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_FIN") == 0)   { rgb565ToHtml(dispSettings.finishColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_STOK") == 0)  { rgb565ToHtml(dispSettings.statusOkColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_PNAME") == 0) { rgb565ToHtml(dispSettings.printerNameColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_TXT") == 0)   { rgb565ToHtml(dispSettings.textColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_TXTD") == 0)  { rgb565ToHtml(dispSettings.textDimColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_DORC") == 0)  { rgb565ToHtml(dispSettings.doorClosedColor, buf); out = buf; return true; }
  if (strcmp(name, "CLR_DORO") == 0)  { rgb565ToHtml(dispSettings.doorOpenColor, buf); out = buf; return true; }
  {
    static const struct { const char* prefix; const GaugeColors* gc; } gauges[] = {
      {"PRG", &dispSettings.progress}, {"NOZ", &dispSettings.nozzle},
      {"BED", &dispSettings.bed},      {"PFN", &dispSettings.partFan},
      {"AFN", &dispSettings.auxFan},   {"AFR", &dispSettings.auxFanRight},
      {"CFN", &dispSettings.chamberFan}, {"EXH", &dispSettings.exhaustFan},
      {"CHT", &dispSettings.chamberTemp}, {"HBK", &dispSettings.heatbreak},
      {"PWR", &dispSettings.power},        {"LYR", &dispSettings.layer},
    };
    for (auto& g : gauges) {
      size_t plen = strlen(g.prefix);
      if (strncmp(name, g.prefix, plen) == 0 && name[plen] == '_' && name[plen+2] == '\0') {
        char suffix = name[plen+1];
        if (suffix == 'A')      rgb565ToHtml(g.gc->arc, buf);
        else if (suffix == 'L') rgb565ToHtml(g.gc->label, buf);
        else if (suffix == 'V') rgb565ToHtml(g.gc->value, buf);
        else continue;
        out = buf;
        return true;
      }
    }
  }

  // --- Custom gauge labels (*_LBL). Stored already sanitized, so emit raw. ---
  {
    static const struct { const char* tok; const char* val; } labels[] = {
      {"PRG_LBL", gaugeLabels.progress},   {"NOZ_LBL", gaugeLabels.nozzle},
      {"BED_LBL", gaugeLabels.bed},        {"PFN_LBL", gaugeLabels.partFan},
      {"AFN_LBL", gaugeLabels.auxFan},     {"AFR_LBL", gaugeLabels.auxFanRight},
      {"CFN_LBL", gaugeLabels.chamberFan}, {"EXH_LBL", gaugeLabels.exhaustFan},
      {"CHT_LBL", gaugeLabels.chamberTemp},{"HBK_LBL", gaugeLabels.heatbreak},
      {"PWR_LBL", gaugeLabels.power},      {"LYR_LBL", gaugeLabels.layer},
      {"CLK_LBL", gaugeLabels.clock},      {"AMS_LBL", gaugeLabels.amsBase},
      {"NZR_LBL", gaugeLabels.nozzleRight},{"NZL_LBL", gaugeLabels.nozzleLeft},
      {"DOR_LBL", gaugeLabels.door},
    };
    for (auto& l : labels) {
      if (strcmp(name, l.tok) == 0) { out = l.val; return true; }
    }
  }

  // --- Status / version / board ---
  if (strcmp(name, "DBGLOG") == 0)       { out = mqttDebugLog ? "checked" : ""; return true; }
  if (strcmp(name, "FW_VER") == 0)       { out = FW_VERSION; return true; }
  // Update-size gate for the release check JS: OTA slot size in bytes and
  // physical flash chip size in MB (16 MB chip on a small slot => the fix is a
  // one-time web-flasher repartition, and the warning says so).
  if (strcmp(name, "OTASLOT") == 0) {
    const esp_partition_t* p = esp_ota_get_next_update_partition(NULL);
    out = String(p ? p->size : 0);
    return true;
  }
  if (strcmp(name, "FLASHMB") == 0)      { out = String(ESP.getFlashChipSize() >> 20); return true; }
  if (strcmp(name, "BOARD") == 0)        { out = BOARD_VARIANT; return true; }
  if (strcmp(name, "BOARD_NAME") == 0)   { out = BOARD_NAME; return true; }
  if (strcmp(name, "BOARD_PANEL") == 0)  { out = BOARD_PANEL; return true; }
  if (strcmp(name, "STATUS_CLASS") == 0) {
    out = st.connected ? "status-pill status-ok"
                       : isPrinterConfigured(0) ? "status-pill status-off"
                                                : "status-pill status-na";
    return true;
  }
  if (strcmp(name, "STATUS_TEXT") == 0) {
    out = st.connected ? "Connected" : isPrinterConfigured(0) ? "Disconnected" : "Not Configured";
    return true;
  }

  // --- Multi-printer rotation ---
  if (strcmp(name, "RMODE_OFF") == 0)   { out = rotState.mode == ROTATE_OFF ? "selected" : ""; return true; }
  if (strcmp(name, "RMODE_AUTO") == 0)  { out = rotState.mode == ROTATE_AUTO ? "selected" : ""; return true; }
  if (strcmp(name, "RMODE_SMART") == 0) { out = rotState.mode == ROTATE_SMART ? "selected" : ""; return true; }
  if (strcmp(name, "ROT_INTERVAL") == 0){ out = String(rotState.intervalMs / 1000); return true; }
  if (strcmp(name, "ROT_SPLIT_CHK") == 0){ out = rotState.splitEnabled ? "checked" : ""; return true; }
  if (strcmp(name, "ROT_SPLITF_CHK") == 0){ out = rotState.splitForce ? "checked" : ""; return true; }

  // --- Gauge full-scale ranges ---
  if (strcmp(name, "NOZ_MAX") == 0) { out = String(dispSettings.nozzleScaleMax); return true; }
  if (strcmp(name, "BED_MAX") == 0) { out = String(dispSettings.bedScaleMax); return true; }
  if (strcmp(name, "CHT_MAX") == 0) { out = String(dispSettings.chamberScaleMax); return true; }
  if (strcmp(name, "PWR_MAX") == 0) { out = String(dispSettings.powerScaleW); return true; }

  // --- Gauge behavior: smoothing speed + temp warning color ---
  if (strcmp(name, "GSMOOTH_OFF") == 0)  { out = dispSettings.gaugeSmoothing == 0 ? "selected" : ""; return true; }
  if (strcmp(name, "GSMOOTH_SLOW") == 0) { out = dispSettings.gaugeSmoothing == 1 ? "selected" : ""; return true; }
  if (strcmp(name, "GSMOOTH_NORM") == 0) { out = dispSettings.gaugeSmoothing == 2 ? "selected" : ""; return true; }
  if (strcmp(name, "GSMOOTH_FAST") == 0) { out = dispSettings.gaugeSmoothing == 3 ? "selected" : ""; return true; }
  if (strcmp(name, "WARN_CLR") == 0) { rgb565ToHtml(dispSettings.warnColor, buf); out = buf; return true; }
  if (strcmp(name, "WARN_THR") == 0) { out = String(dispSettings.warnThresholdPct); return true; }

  // --- Button ---
  if (strcmp(name, "BTN_OFF") == 0)    { out = buttonType == BTN_DISABLED ? "selected" : ""; return true; }
  if (strcmp(name, "BTN_PUSH") == 0)   { out = buttonType == BTN_PUSH ? "selected" : ""; return true; }
  if (strcmp(name, "BTN_TOUCH") == 0)  { out = buttonType == BTN_TOUCH ? "selected" : ""; return true; }
  if (strcmp(name, "BTN_SCREEN") == 0) { out = buttonType == BTN_TOUCHSCREEN ? "selected" : ""; return true; }
  if (strcmp(name, "BTN_PIN") == 0)    { out = String(buttonPin); return true; }

  // --- Buzzer ---
  if (strcmp(name, "BUZ_OFF") == 0) { out = buzzerSettings.enabled ? "" : "selected"; return true; }
  if (strcmp(name, "BUZ_ON") == 0)  { out = buzzerSettings.enabled ? "selected" : ""; return true; }
  if (strcmp(name, "BUZ_PIN") == 0) { out = String(buzzerSettings.pin); return true; }
  if (strcmp(name, "ES8311_AUDIO") == 0) {
    // "1" for any board with built-in I2S audio (no GPIO pin selection needed)
#if defined(BOARD_HAS_ES8311_AUDIO) || defined(BOARD_HAS_NS4168_AUDIO)
    out = "1";
#else
    out = "0";
#endif
    return true;
  }
  if (strcmp(name, "BUZ_QS") == 0)        { out = String(buzzerSettings.quietStartHour); return true; }
  if (strcmp(name, "BUZ_QE") == 0)        { out = String(buzzerSettings.quietEndHour); return true; }
  if (strcmp(name, "BUZ_CLICK") == 0)     { out = buzzerSettings.buttonClick ? "checked" : ""; return true; }
  if (strcmp(name, "BUZ_BED_ALERT") == 0) { out = buzzerSettings.bedCooldownAlert ? "checked" : ""; return true; }
  if (strcmp(name, "BUZ_BED_TEMP") == 0)  { out = String(buzzerSettings.bedCooldownThresholdC); return true; }

  // --- External LED ---
  if (strcmp(name, "LED_OFF") == 0)     { out = ledSettings.enabled ? "" : "selected"; return true; }
  if (strcmp(name, "LED_ON") == 0)      { out = ledSettings.enabled ? "selected" : ""; return true; }
  if (strcmp(name, "LED_PIN") == 0)     { out = String(ledSettings.pin); return true; }
  if (strcmp(name, "LED_BR") == 0)      { out = String(ledSettings.brightness); return true; }
  if (strcmp(name, "LED_FX_OFF")    == 0) { out = ledSettings.finishMode == LED_FINISH_OFF       ? "selected" : ""; return true; }
  if (strcmp(name, "LED_FX_BREATH") == 0) { out = ledSettings.finishMode == LED_FINISH_BREATHING ? "selected" : ""; return true; }
  if (strcmp(name, "LED_FX_HB")     == 0) { out = ledSettings.finishMode == LED_FINISH_HEARTBEAT ? "selected" : ""; return true; }
  if (strcmp(name, "LED_FX_SEC")    == 0) { out = String(ledSettings.finishSeconds); return true; }
  if (strcmp(name, "LED_FX_BR")     == 0) { out = String(ledSettings.finishBrightness); return true; }
  if (strcmp(name, "LED_AUTO")      == 0) { out = ledSettings.autoOnWhilePrinting ? "checked" : ""; return true; }
  if (strcmp(name, "LED_PAUSE")     == 0) { out = ledSettings.pauseBreathing ? "checked" : ""; return true; }
  if (strcmp(name, "LED_ERR")       == 0) { out = ledSettings.errorStrobe ? "checked" : ""; return true; }
  if (strcmp(name, "LED_NIGHT")     == 0) { out = ledSettings.nightOff ? "checked" : ""; return true; }
  if (strcmp(name, "LED_ERR_SEC")   == 0) { out = String(ledSettings.errorStrobeSeconds); return true; }
  if (strcmp(name, "LED_DRV_S")   == 0) { out = ledSettings.driver == LED_DRV_SINGLE ? "selected" : ""; return true; }
  if (strcmp(name, "LED_DRV_R")   == 0) { out = ledSettings.driver == LED_DRV_RGB    ? "selected" : ""; return true; }
  // Offered only where the WS2812 driver was compiled in - see HAS_LED_PIXEL.
  if (strcmp(name, "LED_DRV_PIXEL_OPT") == 0) {
#if HAS_LED_PIXEL
    out  = "<option value=\"2\"";
    if (ledSettings.driver == LED_DRV_PIXEL) out += " selected";
    out += ">WS2812 pixel (one data pin)</option>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "LED_PIN_G")   == 0) { out = String(ledSettings.pinG); return true; }
  if (strcmp(name, "LED_PIN_B")   == 0) { out = String(ledSettings.pinB); return true; }
  if (strcmp(name, "LED_ANODE")   == 0) { out = ledSettings.commonAnode ? "checked" : ""; return true; }
  if (strcmp(name, "LED_C_IDLE")  == 0) { out = ledHexColor(ledSettings.colorIdle);     return true; }
  if (strcmp(name, "LED_C_PRINT") == 0) { out = ledHexColor(ledSettings.colorPrinting); return true; }
  if (strcmp(name, "LED_C_PAUSE") == 0) { out = ledHexColor(ledSettings.colorPaused);   return true; }
  if (strcmp(name, "LED_C_FIN")   == 0) { out = ledHexColor(ledSettings.colorFinished); return true; }
  if (strcmp(name, "LED_C_ERR")   == 0) { out = ledHexColor(ledSettings.colorError);    return true; }
  // Shortcut that fills the pin fields with the board's own onboard RGB wiring.
  // Emitted as a block rather than inline markup because PAGE_HTML is one raw
  // string literal - a #if inside it would ship as literal text. The pin values
  // are resolved per render: on the CYD the red pin follows the ESP32-32E
  // runtime variant.
  if (strcmp(name, "LED_ONBOARD_ROW") == 0) {
#if BOARD_HAS_ONBOARD_RGB
    uint8_t r = 0, g = 0, b = 0; bool anode = false;
    onboardRgbPins(r, g, b, anode);
    out  = "<div id=\"ledOnboard\" class=\"field\" data-r=\"" + String(r) + "\"";
    out += " data-g=\"" + String(g) + "\" data-b=\"" + String(b) + "\"";
    out += " data-anode=\"" + String(anode ? "1" : "0") + "\"";
    out += " data-drv=\"" + String((int)(BOARD_HAS_ONBOARD_RGB_PIXEL ? LED_DRV_PIXEL : LED_DRV_RGB)) + "\">";
    out += "<button type=\"button\" class=\"btn btn-ghost btn-sm\" onclick=\"ledUseOnboard()\">";
    out += "Use this board's onboard RGB LED</button></div>";
#else
    out = "";
#endif
    return true;
  }

  // --- Battery indicator (Waveshare boards only) ---
  if (strcmp(name, "BAT_TOGGLE_ROW") == 0) {
#if defined(BOARD_HAS_BATTERY)
    out  = "<div class=\"card\">";
    out += "<div class=\"card-head\"><div><h3>Battery indicator</h3>";
    out += "<p>For boards with a battery wired. Shows charge state on the display.</p></div></div>";
    out += "<label class=\"check-row\">";
    out += "<input type=\"checkbox\" id=\"batshow\"";
    if (dispSettings.showBatteryIndicator) out += " checked";
    out += "><label for=\"batshow\">Show battery indicator</label>";
    out += "</label>";
    out += "<div class=\"hint\" style=\"padding-left:28px;margin-top:-4px\">Hide if your board has no battery wired (avoids phantom readings).</div>";
    out += "</div>";
#else
    out = "";
#endif
    return true;
  }

  // --- Tasmota power monitoring ---
  if (strcmp(name, "POWER_TAB_2") == 0) {
    // Extra plug tabs beyond Plug 1 (ptab0 is static markup): one per active
    // printer slot. Empty on single-plug low-RAM boards, one tab on full-RAM,
    // three on PSRAM boards running four printers.
    out = "";
    for (uint8_t i = 1; i < TASMOTA_PLUG_COUNT; i++) {
      out += "<button type=\"button\" class=\"power-tab-btn\" id=\"ptab";
      out += String(i);
      out += "\" onclick=\"selectPowerTab(";
      out += String(i);
      out += ")\">Plug ";
      out += String(i + 1);
      out += "</button>";
    }
    return true;
  }
  if (strcmp(name, "POWER_SLOT_BLOCK") == 0) {
#if TASMOTA_PLUG_COUNT == 1
    out  = "<div class=\"field\"><label for=\"tsm_slot\">Assigned printer</label>";
    out += "<select id=\"tsm_slot\"><option value=\"255\">Any printer</option>";
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      out += "<option value=\"";
      out += String(i);
      out += "\">";
      const char* nm = printers[i].config.name;
      if (nm[0] != '\0') out += nm;
      else { out += "Printer "; out += String(i + 1); }
      out += "</option>";
    }
    out += "</select>";
    out += "<div class=\"hint\">When set to <em>Any printer</em>, energy stats and auto-off use Printer 1.</div></div>";
#else
    out = "";
#endif
    return true;
  }
  if (strcmp(name, "TSM_PI_OPTIONS") == 0) {
    static const uint8_t intervals[] = {10, 15, 20, 30, 60};
    static const char* const labels[] = {"10 seconds", "15 seconds", "20 seconds", "30 seconds", "60 seconds"};
    out = "";
    for (int i = 0; i < 5; i++) {
      out += "<option value=\"";
      out += String(intervals[i]);
      out += "\">";
      out += labels[i];
      out += "</option>";
    }
    return true;
  }

  return false;  // unknown placeholder
}

// ---------------------------------------------------------------------------
//  Stream the HTML template from PROGMEM, resolving placeholders on the fly.
//  All output (literal HTML + placeholder values) goes into a single 2 KB
//  buffer; sendContent() flushes only when full, minimizing TCP writes.
//  Peak heap during render is the 2 KB buffer plus any per-placeholder String.
// ---------------------------------------------------------------------------
// One piece of the page. The page is split so a board without the printer-error
// feature never links its markup; the pieces stream back to back into a single
// chunked response, so a split is invisible to the browser.
struct TemplateSegment {
  const char* data;
  size_t      len;
};

static void streamTemplate(const TemplateSegment* segs, uint8_t segCount) {
  static const size_t BUF_SIZE = 2048;
  char* buf = (char*)malloc(BUF_SIZE + 1);
  if (!buf) {
    server.send(503, "text/plain", "Out of memory");
    return;
  }
  size_t bufLen = 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", "");

  auto flush = [&]() {
    if (bufLen > 0) {
      buf[bufLen] = '\0';
      server.sendContent(buf);
      bufLen = 0;
    }
  };

  auto emit = [&](const char* data, size_t len) {
    while (len > 0) {
      size_t space = BUF_SIZE - bufLen;
      size_t n = len < space ? len : space;
      memcpy(buf + bufLen, data, n);
      bufLen += n;
      data += n;
      len -= n;
      if (bufLen >= BUF_SIZE) flush();
    }
  };

  // On ESP32, PROGMEM is directly memory-mapped and readable as const char*.
  for (uint8_t seg = 0; seg < segCount; seg++) {
  const char* end = segs[seg].data + segs[seg].len;
  const char* pos = segs[seg].data;
  const char* literalStart = pos;

  while (pos < end) {
    if (*pos != '%') { pos++; continue; }
    if (pos + 1 >= end || !(pos[1] >= 'A' && pos[1] <= 'Z')) { pos++; continue; }

    const char* pEnd = pos + 2;
    while (pEnd < end && *pEnd != '%' && *pEnd != '\n' && (pEnd - pos) < 30) pEnd++;
    if (pEnd >= end || *pEnd != '%') { pos++; continue; }

    bool valid = true;
    for (const char* c = pos + 1; c < pEnd; c++) {
      if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) {
        valid = false; break;
      }
    }
    if (!valid) { pos++; continue; }

    size_t nameLen = pEnd - pos - 1;
    char name[32];
    if (nameLen >= sizeof(name)) { pos++; continue; }
    memcpy(name, pos + 1, nameLen);
    name[nameLen] = '\0';

    String value;
    if (resolvePlaceholder(name, value)) {
      if (pos > literalStart) emit(literalStart, pos - literalStart);
      if (value.length() > 0) emit(value.c_str(), value.length());
      pos = pEnd + 1;
      literalStart = pos;
    } else {
      pos++;
    }
  }

  if (end > literalStart) emit(literalStart, end - literalStart);
  }
  flush();
  server.sendContent("");
  free(buf);
}

// ---------------------------------------------------------------------------
//  Public entry points
// ---------------------------------------------------------------------------
void serveMainPage() {
  const TemplateSegment segs[] = {
    { PAGE_HTML_1, sizeof(PAGE_HTML_1) - 1 },
#if HAS_HMS_WEB_UI
    { PAGE_HTML_ERRORS, sizeof(PAGE_HTML_ERRORS) - 1 },
#endif
    { PAGE_HTML_2, sizeof(PAGE_HTML_2) - 1 },
  };
  streamTemplate(segs, (uint8_t)(sizeof(segs) / sizeof(segs[0])));
}

void serveApPage() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", FPSTR(PAGE_AP_HTML));
}
