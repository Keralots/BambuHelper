#include "display_ui.h"
#include "display_gauges.h"
#include "display_anim.h"
#include "clock_mode.h"
#include "icons.h"
#include "config.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "settings.h"
#include <WiFi.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();

static ScreenState currentScreen = SCREEN_SPLASH;
static ScreenState prevScreen = SCREEN_SPLASH;
static bool forceRedraw = true;
static unsigned long lastDisplayUpdate = 0;

// Previous state for smart redraw
static BambuState prevState;
static unsigned long connectScreenStart = 0;

// ---------------------------------------------------------------------------
//  Backlight
// ---------------------------------------------------------------------------
void setBacklight(uint8_t level) {
#if defined(BACKLIGHT_PIN) && BACKLIGHT_PIN >= 0
  analogWrite(BACKLIGHT_PIN, level);
#endif
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initDisplay() {
  Serial.println("Display: pre-init delay...");
  delay(500);
  Serial.println("Display: calling tft.init()...");
  Serial.flush();
  tft.init();  // TFT_eSPI configures SPI from build flags
  Serial.println("Display: tft.init() done");
  tft.setRotation(dispSettings.rotation);
  Serial.println("Display: setRotation done");
  tft.fillScreen(CLR_BG);
  Serial.println("Display: fillScreen done");

#if defined(BACKLIGHT_PIN) && BACKLIGHT_PIN >= 0
  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(200);
#endif

  memset(&prevState, 0, sizeof(prevState));

  // Splash screen
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("BambuHelper", SCREEN_W / 2, SCREEN_H / 2 - 20);
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Printer Monitor", SCREEN_W / 2, SCREEN_H / 2 + 10);
  tft.setTextFont(1);
  tft.drawString(FW_VERSION, SCREEN_W / 2, SCREEN_H / 2 + 30);
}

void applyDisplaySettings() {
  tft.setRotation(dispSettings.rotation);
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
}

void triggerDisplayTransition() {
  // Clear previous state so everything redraws for the new printer
  memset(&prevState, 0, sizeof(prevState));
  tft.fillScreen(dispSettings.bgColor);
  forceRedraw = true;
}

void setScreenState(ScreenState state) {
  currentScreen = state;
}

ScreenState getScreenState() {
  return currentScreen;
}

// ---------------------------------------------------------------------------
//  Nozzle label helper (dual nozzle H2D/H2C)
// ---------------------------------------------------------------------------
static const char* nozzleLabel(const BambuState& s) {
  if (!s.dualNozzle) return "Nozzle";
  return s.activeNozzle == 0 ? "Nozzle R" : "Nozzle L";
}

// ---------------------------------------------------------------------------
//  Speed level name helper
// ---------------------------------------------------------------------------
static const char* speedLevelName(uint8_t level) {
  switch (level) {
    case 1: return "Silent";
    case 2: return "Std";
    case 3: return "Sport";
    case 4: return "Ludicr";
    default: return "---";
  }
}

static uint16_t speedLevelColor(uint8_t level) {
  switch (level) {
    case 1: return CLR_BLUE;
    case 2: return CLR_GREEN;
    case 3: return CLR_ORANGE;
    case 4: return CLR_RED;
    default: return CLR_TEXT_DIM;
  }
}

// ---------------------------------------------------------------------------
//  Screen: AP Mode
// ---------------------------------------------------------------------------
static void drawAPMode() {
  tft.setTextDatum(MC_DATUM);

  // Title
  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Setup", SCREEN_W / 2, 40);

  // Instructions
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connect to WiFi:", SCREEN_W / 2, 80);

  // AP SSID
  tft.setTextColor(CLR_CYAN, CLR_BG);
  tft.setTextFont(4);
  char ssid[32];
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_PREFIX, mac);
  tft.drawString(ssid, SCREEN_W / 2, 110);

  // Password
  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Password:", SCREEN_W / 2, 140);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString(WIFI_AP_PASSWORD, SCREEN_W / 2, 158);

  // IP
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Then open:", SCREEN_W / 2, 185);
  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("192.168.4.1", SCREEN_W / 2, 210);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting WiFi
// ---------------------------------------------------------------------------
static void drawConnectingWiFi() {
  tft.setTextDatum(MC_DATUM);

  drawSpinner(tft, SCREEN_W / 2, SCREEN_H / 2 - 30, 35, CLR_BLUE);

  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to WiFi", SCREEN_W / 2, SCREEN_H / 2 + 20);

  int16_t tw = tft.textWidth("Connecting to WiFi");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 + 14, CLR_TEXT);
}

// ---------------------------------------------------------------------------
//  Screen: WiFi Connected (show IP)
// ---------------------------------------------------------------------------
static void drawWiFiConnected() {
  if (!forceRedraw) return;

  tft.setTextDatum(MC_DATUM);

  // Checkmark circle with tick
  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 - 40;
  tft.fillCircle(cx, cy, 25, CLR_GREEN);
  // Draw thick tick mark (3px wide)
  for (int i = -1; i <= 1; i++) {
    tft.drawLine(cx - 12, cy + i,     cx - 4, cy + 8 + i, CLR_BG);  // short leg
    tft.drawLine(cx - 4,  cy + 8 + i, cx + 12, cy - 6 + i, CLR_BG); // long leg
  }

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("WiFi Connected", SCREEN_W / 2, SCREEN_H / 2 + 10);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, SCREEN_H / 2 + 40);
}

// ---------------------------------------------------------------------------
//  Screen: Connecting MQTT
// ---------------------------------------------------------------------------
static void drawConnectingMQTT() {
  tft.setTextDatum(MC_DATUM);

  drawSpinner(tft, SCREEN_W / 2, SCREEN_H / 2 - 50, 30, CLR_ORANGE);

  tft.setTextFont(2);
  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.drawString("Connecting to Printer", SCREEN_W / 2, SCREEN_H / 2);

  int16_t tw = tft.textWidth("Connecting to Printer");
  drawAnimDots(tft, SCREEN_W / 2 + tw / 2, SCREEN_H / 2 - 6, CLR_TEXT);

  // Show connection mode + printer info
  PrinterSlot& p = displayedPrinter();
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setTextFont(1);

  const char* modeStr = isCloudMode(p.config.mode) ? "Cloud" : "LAN";
  char infoBuf[40];
  if (isCloudMode(p.config.mode)) {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s", modeStr,
             strlen(p.config.serial) > 0 ? p.config.serial : "no serial!");
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "[%s] %s",  modeStr,
             strlen(p.config.ip) > 0 ? p.config.ip : "no IP!");
  }
  tft.drawString(infoBuf, SCREEN_W / 2, SCREEN_H / 2 + 20);

  // Elapsed time
  if (connectScreenStart > 0) {
    unsigned long elapsed = (millis() - connectScreenStart) / 1000;
    char elBuf[16];
    snprintf(elBuf, sizeof(elBuf), "%lus", elapsed);
    tft.fillRect(SCREEN_W / 2 - 20, SCREEN_H / 2 + 28, 40, 12, CLR_BG);
    tft.drawString(elBuf, SCREEN_W / 2, SCREEN_H / 2 + 34);
  }

  // Diagnostics info
  const MqttDiag& d = getMqttDiag(rotState.displayIndex);
  if (d.attempts > 0) {
    tft.fillRect(0, SCREEN_H / 2 + 48, SCREEN_W, 72, CLR_BG);
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);

    // Attempt count
    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt: %u", d.attempts);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 56);

    // TCP status
    if (d.tcpOk) {
      tft.setTextColor(CLR_GREEN, CLR_BG);
      tft.drawString("TCP: OK", SCREEN_W / 2, SCREEN_H / 2 + 68);
    } else if (d.lastRc != 0) {
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString("TCP: fail", SCREEN_W / 2, SCREEN_H / 2 + 68);
    }

    // Last error
    if (d.lastRc != 0) {
      snprintf(buf, sizeof(buf), "Err: %s", mqttRcToString(d.lastRc));
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 80);
    }

    // Heap
    snprintf(buf, sizeof(buf), "Heap: %uK", d.freeHeap / 1024);
    tft.setTextColor(CLR_TEXT_DARK, CLR_BG);
    tft.drawString(buf, SCREEN_W / 2, SCREEN_H / 2 + 92);
  }
}

// ---------------------------------------------------------------------------
//  Screen: Idle (connected, not printing)
// ---------------------------------------------------------------------------
static void drawIdleNoPrinter() {
  if (!forceRedraw) return;

  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(CLR_GREEN, CLR_BG);
  tft.setTextFont(4);
  tft.drawString("BambuHelper", SCREEN_W / 2, 40);

  tft.setTextColor(CLR_TEXT, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("WiFi Connected", SCREEN_W / 2, 80);

  tft.fillCircle(SCREEN_W / 2, 105, 5, CLR_GREEN);

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.setTextFont(2);
  tft.drawString("No printer configured", SCREEN_W / 2, 140);
  tft.drawString("Open in browser:", SCREEN_W / 2, 165);

  tft.setTextColor(CLR_ORANGE, CLR_BG);
  tft.setTextFont(4);
  tft.drawString(WiFi.localIP().toString().c_str(), SCREEN_W / 2, 200);
}

static bool wasNoPrinter = false;

static void drawIdle() {
  if (!isAnyPrinterConfigured()) {
    wasNoPrinter = true;
    drawIdleNoPrinter();
    return;
  }

  // Transition from "no printer" to configured — clear stale screen
  if (wasNoPrinter) {
    wasNoPrinter = false;
    tft.fillScreen(dispSettings.bgColor);
    memset(&prevState, 0, sizeof(prevState));
    forceRedraw = true;
  }

  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool stateChanged = forceRedraw || (strcmp(s.gcodeState, prevState.gcodeState) != 0);
  bool tempChanged = forceRedraw ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);
  bool connChanged = forceRedraw || (s.connected != prevState.connected);
  bool wifiChanged = forceRedraw || (s.wifiSignal != prevState.wifiSignal);

  tft.setTextDatum(MC_DATUM);

  // Printer name (only on forceRedraw — name doesn't change)
  if (forceRedraw) {
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setTextFont(4);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, SCREEN_W / 2, 30);
  }

  // Status badge — only redraw when state changes
  if (stateChanged) {
    tft.setTextFont(2);
    uint16_t stateColor = CLR_TEXT_DIM;
    const char* stateStr = s.gcodeState;
    if (strcmp(s.gcodeState, "IDLE") == 0) {
      stateColor = CLR_GREEN;
      stateStr = "Ready";
    } else if (strcmp(s.gcodeState, "FAILED") == 0) {
      stateColor = CLR_RED;
      stateStr = "ERROR";
    } else if (strcmp(s.gcodeState, "UNKNOWN") == 0 || s.gcodeState[0] == '\0') {
      stateStr = "Waiting...";
    }
    tft.fillRect(0, 50, SCREEN_W, 20, CLR_BG);
    tft.setTextColor(stateColor, CLR_BG);
    tft.drawString(stateStr, SCREEN_W / 2, 60);
  }

  // Connected indicator
  if (connChanged) {
    tft.fillCircle(SCREEN_W / 2, 85, 5, s.connected ? CLR_GREEN : CLR_RED);
  }

  // Nozzle temp gauge
  if (tempChanged) {
    drawTempGauge(tft, SCREEN_W / 2 - 55, 140, 30,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle);

    // Bed temp gauge
    drawTempGauge(tft, SCREEN_W / 2 + 55, 140, 30,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed);
  }

  // WiFi signal at bottom
  if (wifiChanged) {
    tft.fillRect(0, SCREEN_H - 14, SCREEN_W, 14, CLR_BG);
    tft.setTextFont(1);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    tft.setTextDatum(BC_DATUM);
    char wifiBuf[24];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %d dBm", s.wifiSignal);
    tft.drawString(wifiBuf, SCREEN_W / 2, SCREEN_H - 5);
  }
}

// ---------------------------------------------------------------------------
//  Screen: Printing (main dashboard)
//  Layout: LED bar | header | 2x3 gauge grid | info line
// ---------------------------------------------------------------------------
static void drawPrinting() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool progChanged = forceRedraw || (s.progress != prevState.progress);
  bool tempChanged = forceRedraw ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);
  bool etaChanged = forceRedraw ||
                     (s.remainingMinutes != prevState.remainingMinutes);
  bool fansChanged = forceRedraw ||
                     (s.coolingFanPct != prevState.coolingFanPct) ||
                     (s.auxFanPct != prevState.auxFanPct) ||
                     (s.chamberFanPct != prevState.chamberFanPct);
  bool stateChanged = forceRedraw ||
                      (strcmp(s.gcodeState, prevState.gcodeState) != 0);

  // 2x3 gauge grid constants
  const int16_t gR = 32;       // radius for all gauges
  const int16_t gT = 6;        // thickness for progress arc
  const int16_t col1 = 42;     // left column center X
  const int16_t col2 = 120;    // middle column center X
  const int16_t col3 = 198;    // right column center X
  const int16_t row1Y = 60;    // row 1 center Y (progress, nozzle, bed)
  const int16_t row2Y = 148;   // row 2 center Y (part fan, aux fan, chamb fan)

  // === H2-style LED progress bar (y=0-5) ===
  if (progChanged) {
    drawLedProgressBar(tft, 0, s.progress);
  }

  // === Header bar (y=7-25) ===
  if (forceRedraw || stateChanged) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, 7, SCREEN_W, 20, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Bambu P1S";
    tft.drawString(name, 6, 17);

    // State badge (right)
    uint16_t badgeColor = CLR_TEXT_DIM;
    if (strcmp(s.gcodeState, "RUNNING") == 0) badgeColor = CLR_GREEN;
    else if (strcmp(s.gcodeState, "PAUSE") == 0) badgeColor = CLR_YELLOW;
    else if (strcmp(s.gcodeState, "FAILED") == 0) badgeColor = CLR_RED;
    else if (strcmp(s.gcodeState, "PREPARE") == 0) badgeColor = CLR_BLUE;

    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(badgeColor, hdrBg);
    tft.setTextFont(2);
    tft.fillCircle(SCREEN_W - 8 - tft.textWidth(s.gcodeState) - 10, 17, 4, badgeColor);
    tft.drawString(s.gcodeState, SCREEN_W - 8, 17);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(SCREEN_W / 2 - 5 + di * 10, 10, 3, dotClr);
      }
    }
  }

  // === Row 1: Progress | Nozzle | Bed (y=30-100) ===

  if (progChanged || forceRedraw) {
    drawProgressArc(tft, col1, row1Y, gR, gT,
                    s.progress, prevState.progress,
                    s.remainingMinutes, forceRedraw);
  }

  if (tempChanged) {
    drawTempGauge(tft, col2, row1Y, gR,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle);

    drawTempGauge(tft, col3, row1Y, gR,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed);
  }

  // === Row 2: Part Fan | Aux Fan | Chamber Fan (y=106-176) ===

  if (fansChanged) {
    drawFanGauge(tft, col1, row2Y, gR,
                 s.coolingFanPct, dispSettings.partFan.arc, "Part", forceRedraw,
                 &dispSettings.partFan);

    drawFanGauge(tft, col2, row2Y, gR,
                 s.auxFanPct, dispSettings.auxFan.arc, "Aux", forceRedraw,
                 &dispSettings.auxFan);

    drawFanGauge(tft, col3, row2Y, gR,
                 s.chamberFanPct, dispSettings.chamberFan.arc, "Chamber", forceRedraw,
                 &dispSettings.chamberFan);
  }

  // === Info line — ETA finish time or PAUSE/ERROR alert (below row 2 labels) ===
  if (etaChanged || stateChanged) {
    tft.fillRect(0, 190, SCREEN_W, 30, CLR_BG);
    tft.setTextDatum(MC_DATUM);

    if (strcmp(s.gcodeState, "PAUSE") == 0) {
      // Prominent PAUSE alert
      tft.setTextFont(4);
      tft.setTextColor(CLR_YELLOW, CLR_BG);
      tft.drawString("PAUSED", SCREEN_W / 2, 207);
    } else if (strcmp(s.gcodeState, "FAILED") == 0) {
      // Prominent ERROR alert
      tft.setTextFont(4);
      tft.setTextColor(CLR_RED, CLR_BG);
      tft.drawString("ERROR!", SCREEN_W / 2, 207);
    } else if (s.remainingMinutes > 0) {
      struct tm now;
      if (getLocalTime(&now, 0)) {
        // Calculate ETA: current time + remaining minutes
        time_t nowEpoch = mktime(&now);
        time_t etaEpoch = nowEpoch + (time_t)s.remainingMinutes * 60;
        struct tm etaTm;
        localtime_r(&etaEpoch, &etaTm);

        char etaBuf[32];
        int etaH = etaTm.tm_hour;
        const char* ampm = "";
        if (!netSettings.use24h) {
          ampm = etaH < 12 ? "AM" : "PM";
          etaH = etaH % 12;
          if (etaH == 0) etaH = 12;
        }
        // Show date only if finish is not today
        if (etaTm.tm_yday != now.tm_yday || etaTm.tm_year != now.tm_year) {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d.%02d %02d:%02d",
                     etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d.%02d %d:%02d%s",
                     etaTm.tm_mday, etaTm.tm_mon + 1, etaH, etaTm.tm_min, ampm);
        } else {
          if (netSettings.use24h)
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %02d:%02d", etaH, etaTm.tm_min);
          else
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d:%02d %s", etaH, etaTm.tm_min, ampm);
        }
        tft.setTextFont(4);
        tft.setTextColor(CLR_GREEN, CLR_BG);
        tft.drawString(etaBuf, SCREEN_W / 2, 207);
      } else {
        // NTP not synced yet — show remaining time only
        char remBuf[24];
        uint16_t h = s.remainingMinutes / 60;
        uint16_t m = s.remainingMinutes % 60;
        snprintf(remBuf, sizeof(remBuf), "Remaining: %dh %02dm", h, m);
        tft.setTextFont(4);
        tft.setTextColor(CLR_TEXT, CLR_BG);
        tft.drawString(remBuf, SCREEN_W / 2, 207);
      }
    } else {
      tft.setTextFont(4);
      tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
      tft.drawString("ETA: ---", SCREEN_W / 2, 207);
    }
  }

  // === Bottom status bar — WiFi | Layer | Speed (y=218-236) ===
  bool bottomChanged = forceRedraw ||
                       (s.wifiSignal != prevState.wifiSignal) ||
                       (s.speedLevel != prevState.speedLevel) ||
                       (s.layerNum != prevState.layerNum) ||
                       (s.totalLayers != prevState.totalLayers);
  if (bottomChanged) {
    tft.fillRect(0, 218, SCREEN_W, 22, CLR_BG);
    tft.setTextFont(1);

    // WiFi signal (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    char wifiBuf[20];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi %ddBm", s.wifiSignal);
    tft.drawString(wifiBuf, 4, 230);

    // Layer count (center)
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    char layerBuf[20];
    snprintf(layerBuf, sizeof(layerBuf), "L%d/%d", s.layerNum, s.totalLayers);
    tft.drawString(layerBuf, SCREEN_W / 2, 230);

    // Speed mode (right)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(speedLevelColor(s.speedLevel), CLR_BG);
    tft.drawString(speedLevelName(s.speedLevel), SCREEN_W - 4, 230);
  }
}

// ---------------------------------------------------------------------------
//  Screen: Finished (same layout as printing, but with 2 gauges + status)
// ---------------------------------------------------------------------------
static void drawFinished() {
  PrinterSlot& p = displayedPrinter();
  BambuState& s = p.state;

  bool tempChanged = forceRedraw ||
                     (s.nozzleTemp != prevState.nozzleTemp) ||
                     (s.nozzleTarget != prevState.nozzleTarget) ||
                     (s.bedTemp != prevState.bedTemp) ||
                     (s.bedTarget != prevState.bedTarget);

  const int16_t gR = 32;
  const int16_t gaugeLeft  = 72;   // two gauges centered on 240px
  const int16_t gaugeRight = 168;
  const int16_t gaugeY = 80;

  // === H2-style LED progress bar at 100% (y=0-5) ===
  if (forceRedraw) {
    drawLedProgressBar(tft, 0, 100);
  }

  // === Header bar (y=7-25) — same as printing screen ===
  if (forceRedraw) {
    uint16_t hdrBg = dispSettings.bgColor;
    tft.fillRect(0, 7, SCREEN_W, 20, hdrBg);

    // Printer name (left)
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT, hdrBg);
    const char* name = (p.config.name[0] != '\0') ? p.config.name : "Printer";
    tft.drawString(name, 6, 17);

    // FINISH badge (right)
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(CLR_GREEN, hdrBg);
    tft.setTextFont(2);
    tft.fillCircle(SCREEN_W - 8 - tft.textWidth("FINISH") - 10, 17, 4, CLR_GREEN);
    tft.drawString("FINISH", SCREEN_W - 8, 17);

    // Printer indicator dots (multi-printer)
    if (getActiveConnCount() > 1) {
      for (uint8_t di = 0; di < MAX_ACTIVE_PRINTERS; di++) {
        if (!isPrinterConfigured(di)) continue;
        uint16_t dotClr = (di == rotState.displayIndex) ? CLR_GREEN : CLR_TEXT_DARK;
        tft.fillCircle(SCREEN_W / 2 - 5 + di * 10, 10, 3, dotClr);
      }
    }
  }

  // === Row 1: Nozzle | Bed (two gauges centered) ===
  if (tempChanged) {
    drawTempGauge(tft, gaugeLeft, gaugeY, gR,
                  s.nozzleTemp, s.nozzleTarget, 300.0f,
                  dispSettings.nozzle.arc, nozzleLabel(s), nullptr, forceRedraw,
                  &dispSettings.nozzle);

    drawTempGauge(tft, gaugeRight, gaugeY, gR,
                  s.bedTemp, s.bedTarget, 120.0f,
                  dispSettings.bed.arc, "Bed", nullptr, forceRedraw,
                  &dispSettings.bed);
  }

  // === "Print Complete!" status ===
  if (forceRedraw) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_GREEN, CLR_BG);
    tft.setTextFont(4);
    tft.drawString("Print Complete!", SCREEN_W / 2, 148);
  }

  // === File name ===
  if (forceRedraw) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    if (s.subtaskName[0] != '\0') {
      char truncName[26];
      strncpy(truncName, s.subtaskName, 25);
      truncName[25] = '\0';
      tft.drawString(truncName, SCREEN_W / 2, 178);
    }
  }

  // === Bottom status bar — WiFi (y=218-236) ===
  if (forceRedraw) {
    tft.fillRect(0, 218, SCREEN_W, 22, CLR_BG);
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    char wifiBuf[20];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %d dBm", s.wifiSignal);
    tft.drawString(wifiBuf, SCREEN_W / 2, 230);
  }
}

// ---------------------------------------------------------------------------
//  Main update (called from loop)
// ---------------------------------------------------------------------------
void updateDisplay() {
  unsigned long now = millis();
  if (now - lastDisplayUpdate < DISPLAY_UPDATE_MS) return;
  lastDisplayUpdate = now;

  // Detect screen change
  if (currentScreen != prevScreen) {
    // Restore backlight when leaving SCREEN_OFF
    if (prevScreen == SCREEN_OFF && currentScreen != SCREEN_OFF) {
      setBacklight(brightness);
    }
    tft.fillScreen(currentScreen == SCREEN_OFF ? TFT_BLACK : dispSettings.bgColor);
    forceRedraw = true;
    if (currentScreen == SCREEN_CONNECTING_MQTT) {
      connectScreenStart = millis();
    }
    prevScreen = currentScreen;
  }

  switch (currentScreen) {
    case SCREEN_SPLASH:
      // Splash shown in initDisplay(), auto-advance handled by main.cpp
      break;

    case SCREEN_AP_MODE:
      if (forceRedraw) drawAPMode();
      break;

    case SCREEN_CONNECTING_WIFI:
      drawConnectingWiFi();
      break;

    case SCREEN_WIFI_CONNECTED:
      drawWiFiConnected();
      break;

    case SCREEN_CONNECTING_MQTT:
      drawConnectingMQTT();
      break;

    case SCREEN_IDLE:
      drawIdle();
      break;

    case SCREEN_PRINTING:
      drawPrinting();
      break;

    case SCREEN_FINISHED:
      drawFinished();
      break;

    case SCREEN_CLOCK:
      drawClock();
      break;

    case SCREEN_OFF:
      if (forceRedraw) {
        tft.fillScreen(TFT_BLACK);
        setBacklight(0);
      }
      break;
  }

  // Save state for next smart-redraw comparison
  memcpy(&prevState, &displayedPrinter().state, sizeof(BambuState));
  forceRedraw = false;
}
