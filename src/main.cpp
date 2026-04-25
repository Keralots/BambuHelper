#include <Arduino.h>
#include "display_ui.h"
#include "settings.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "bambu_mqtt.h"
#include "config.h"
#include "bambu_state.h"
#include "button.h"
#include "buzzer.h"
#include "tasmota.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

static unsigned long splashEnd = 0;
static unsigned long finishScreenStart = 0;
static bool finishActive = false;          // guards finishScreenStart against millis() wrap
static unsigned long idleClockStart = 0;   // when all printers became idle
static bool idleClockActive = false;       // guards idleClockStart against millis() wrap
static unsigned long connectingScreenStart = 0;  // for stuck-state timeout
static PrinterGcodeState prevGcodeStateId[MAX_ACTIVE_PRINTERS] = { GCODE_UNKNOWN };
static bool prevGcodeStateSeen[MAX_ACTIVE_PRINTERS] = { false };
#if defined(BAT_EN) && defined(BOARD_BTN_1) && defined(BOARD_BTN_3)
static unsigned long boardShutdownHoldStart = 0;
#endif
#if defined(BOARD_BTN_1)
static bool lastBoardBtn = false;
static bool boardBtnStable = false;
static unsigned long boardBtnChangeMs = 0;
#endif

static bool anyPrinterPrinting() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
      return true;
    }
  }
  return false;
}

static bool anyPrinterDrying() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (isPrinterConfigured(i) && printers[i].state.ams.anyDrying) {
      return true;
    }
  }
  return false;
}

static bool isSleepStickyScreen(ScreenState state) {
  return state == SCREEN_CLOCK || state == SCREEN_OFF;
}

static bool isDisplayedPrinterAssignedToTasmota() {
  return tasmotaSettings.assignedSlot == 255 ||
         tasmotaSettings.assignedSlot == rotState.displayIndex;
}

static void transitionToClockOrOff() {
  if (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED) {
    setScreenState(SCREEN_CLOCK);
  } else {
    setScreenState(SCREEN_OFF);
  }
}

static bool handleSplashPhase() {
  // Hold splash for 2s
  if (splashEnd > 0 && millis() > splashEnd) {
    splashEnd = 0;
    initWiFi();
    initWebServer();
    initBambuMqtt();
    initButton();
    initBuzzer();
    tasmotaInit();
  }

  if (splashEnd > 0) {
    delay(10);
    return true;
  }

  return false;
}

static void cycleDisplayedPrinterFromButton() {
  uint8_t idx = rotState.displayIndex;
  for (uint8_t a = 1; a <= MAX_ACTIVE_PRINTERS; a++) {
    uint8_t next = (idx + a) % MAX_ACTIVE_PRINTERS;
    if (isPrinterConfigured(next) && next != idx) {
      rotState.displayIndex = next;
      triggerDisplayTransition();
      rotState.lastRotateMs = millis();  // reset auto-rotate timer
      finishActive = false;
      // If switching to a cloud printer in UNKNOWN state, try a refresh
      requestCloudRefresh(next);
      break;
    }
  }
}

static bool wasBoardButtonPressed() {
#if defined(BOARD_BTN_1)
  bool raw = (digitalRead(BOARD_BTN_1) == LOW);
  if (raw != lastBoardBtn) {
    boardBtnChangeMs = millis();
    lastBoardBtn = raw;
  }
  if ((millis() - boardBtnChangeMs) < 50) return false;
  bool result = false;
  if (raw && !boardBtnStable) result = true;
  boardBtnStable = raw;
  return result;
#else
  return false;
#endif
}

static void handleWakeButton() {
  if (!wasButtonPressed() && !wasBoardButtonPressed()) return;

  // Handle physical button press before MQTT so screen wakes instantly
  // without waiting for a potentially blocking TLS reconnect.
  buzzerPlayClick();
  ScreenState cur = getScreenState();

  if (isSleepStickyScreen(cur)) {
    // Wake from sleep + reset backoff for immediate reconnect
    setBacklight(getEffectiveBrightness());
    finishActive = false;
    idleClockActive = false;
    resetMqttBackoff();
    deferMqttReconnect();  // skip blocking reconnect this iteration so screen wakes instantly
    setScreenState(SCREEN_IDLE);  // state machine will correct on next loop
    return;
  }

  if (getActiveConnCount() >= 2) {
    cycleDisplayedPrinterFromButton();
    return;
  }

  if (cur == SCREEN_IDLE &&
      isCloudMode(displayedPrinter().config.mode) &&
      displayedPrinter().state.gcodeStateId == GCODE_UNKNOWN) {
    // Single printer, cloud, UNKNOWN - manual refresh
    requestCloudRefresh(rotState.displayIndex);
  }
}

static void handleBoardPowerOff() {
#if defined(BAT_EN) && defined(BOARD_BTN_1) && defined(BOARD_BTN_3)
  bool leftPressed = (digitalRead(BOARD_BTN_1) == LOW);
  bool rightPressed = (digitalRead(BOARD_BTN_3) == LOW);

  if (!leftPressed || !rightPressed) {
    boardShutdownHoldStart = 0;
    return;
  }

  if (boardShutdownHoldStart == 0) {
    boardShutdownHoldStart = millis();
    return;
  }

  if (millis() - boardShutdownHoldStart < 1500) return;

  Serial.println("Power off requested by built-in buttons");
  setBacklight(0);
  delay(50);

  // Drive BAT_EN low and hold it through deep sleep
  digitalWrite(BAT_EN, LOW);
  gpio_hold_en((gpio_num_t)BAT_EN);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  delay(200);
  esp_deep_sleep_start();
#endif
}

static void handleDisplayedPrinterFinishState(ScreenState current, BambuState& s) {
  if (current != SCREEN_FINISHED && !isSleepStickyScreen(current) &&
      !(current == SCREEN_IDLE && s.ams.anyDrying) &&
      !(current == SCREEN_PRINTING && finishActive)) {
    if (tasmotaSettings.enabled && isDisplayedPrinterAssignedToTasmota()) {
      tasmotaMarkPrintEnd();
    }
    setScreenState(dpSettings.keepPrintScreen ? SCREEN_PRINTING : SCREEN_FINISHED);
    finishScreenStart = millis();
    finishActive = true;
    if (!s.finishBuzzerPlayed) {
      buzzerPlay(BUZZ_PRINT_FINISHED);
      s.finishBuzzerPlayed = true;
    }
  }

  // Door acknowledge: wait for door open before starting timeout
  bool waitingForDoor = dpSettings.doorAckEnabled && s.doorSensorPresent &&
                        !s.doorAcknowledged;
  if (waitingForDoor && s.doorOpen) {
    s.doorAcknowledged = true;
    finishScreenStart = millis();  // restart timeout from door open moment
    finishActive = true;
    Serial.println("Door opened - print removal acknowledged, starting timeout");
  }

  // AMS drying started while on finish/kept-print screen - switch to idle so
  // drawIdleDrying() can take over.
  if ((current == SCREEN_FINISHED ||
       (current == SCREEN_PRINTING && dpSettings.keepPrintScreen && finishActive)) &&
      s.ams.anyDrying) {
    setScreenState(SCREEN_IDLE);
    finishActive = false;
    idleClockActive = false;
  }
}

static void handleDisplayedPrinterIdleState(ScreenState current, const BambuState& s) {
  // SCREEN_CLOCK and SCREEN_OFF are sticky - only button press or
  // new print (s.printing -> SCREEN_PRINTING) exits them.
  if (isSleepStickyScreen(current)) return;

  ScreenState target = (dpSettings.keepPrintScreen && !s.ams.anyDrying)
                       ? SCREEN_PRINTING : SCREEN_IDLE;
  if (current != target) {
    if (current == SCREEN_CONNECTING_MQTT) buzzerPlay(BUZZ_CONNECTED);
    setScreenState(target);
    finishActive = false;
    idleClockActive = false;
  }
}

static void handleDisplayedPrinterConnectedState(ScreenState current, BambuState& s) {
  if (s.printing) {
    if (current != SCREEN_PRINTING) {
      setScreenState(SCREEN_PRINTING);
      finishActive = false;
      if (isDisplayedPrinterAssignedToTasmota()) {
        tasmotaMarkPrintStart();
      }
    }
    s.finishBuzzerPlayed = false;  // reset for next finish event
    s.doorAcknowledged = false;    // reset door ack for next finish
    return;
  }

  if (s.gcodeStateId == GCODE_FINISH) {
    handleDisplayedPrinterFinishState(current, s);
    return;
  }

  handleDisplayedPrinterIdleState(current, s);
}

static void updateDisplayedPrinterScreenState() {
  ScreenState current = getScreenState();

  if (!isAnyPrinterConfigured()) {
    if (current != SCREEN_IDLE && current != SCREEN_OFF) {
      setScreenState(SCREEN_IDLE);
      finishActive = false;
    }
    return;
  }

  if (isOtaAutoInProgress()) {
    if (current != SCREEN_OTA_UPDATE) setScreenState(SCREEN_OTA_UPDATE);
    return;
  }

  BambuState& s = displayedPrinter().state;
  if (!s.connected) {
    if (current != SCREEN_CONNECTING_MQTT && !isSleepStickyScreen(current)) {
      setScreenState(SCREEN_CONNECTING_MQTT);
      finishActive = false;
      connectingScreenStart = millis();
    }
    return;
  }

  if (isSleepStickyScreen(current) && s.ams.anyDrying) {
    setScreenState(SCREEN_IDLE);
    setBacklight(getEffectiveBrightness());
    finishActive = false;
    idleClockActive = false;
    return;
  }

  handleDisplayedPrinterConnectedState(current, s);
}

static void handleDisplaySleepTimeouts() {
  // Idle/Connecting -> Clock/Off: if all printers are idle or disconnected,
  // transition to clock or off after finishDisplayMins timeout.
  // Covers both SCREEN_IDLE (printer connected but not printing) and
  // SCREEN_CONNECTING_MQTT (printer offline/unreachable at startup).
  ScreenState cur = getScreenState();
  if ((cur == SCREEN_FINISHED || (cur == SCREEN_PRINTING && dpSettings.keepPrintScreen)) &&
      !dpSettings.keepDisplayOn && finishActive) {
    BambuState& fs = displayedPrinter().state;
    bool waitingForDoor = dpSettings.doorAckEnabled && fs.doorSensorPresent &&
                          !fs.doorAcknowledged;
    if (!waitingForDoor) {
      bool timeoutReached = (dpSettings.finishDisplayMins > 0) &&
          (millis() - finishScreenStart > (unsigned long)dpSettings.finishDisplayMins * 60000UL);
      bool immediateClockTransition = (dpSettings.finishDisplayMins == 0) &&
          (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED);
      if ((timeoutReached || immediateClockTransition) && !anyPrinterPrinting()) {
        transitionToClockOrOff();
        finishActive = false;
      }
    }
  }

  if ((cur == SCREEN_IDLE || cur == SCREEN_CONNECTING_MQTT ||
       (cur == SCREEN_PRINTING && dpSettings.keepPrintScreen)) &&
      !dpSettings.keepDisplayOn && dpSettings.finishDisplayMins > 0) {
    // Don't sleep while AMS is drying - the drying screen is useful
    if (!anyPrinterPrinting() && !anyPrinterDrying()) {
      if (!idleClockActive) {
        idleClockStart = millis();
        idleClockActive = true;
      }
      if (millis() - idleClockStart > (unsigned long)dpSettings.finishDisplayMins * 60000UL) {
        transitionToClockOrOff();
      }
    } else {
      idleClockActive = false;
    }
  } else if (cur != SCREEN_IDLE && cur != SCREEN_CONNECTING_MQTT) {
    idleClockActive = false;
  }
}

static void handleConnectingScreenRecovery() {
  // Stuck-state timeout: recover if stuck in a connecting screen too long
  ScreenState curConn = getScreenState();
  if (curConn == SCREEN_CONNECTING_WIFI || curConn == SCREEN_CONNECTING_MQTT) {
    if (connectingScreenStart == 0) connectingScreenStart = millis();
    if (millis() - connectingScreenStart > DISPLAY_STATE_TIMEOUT_MS) {
      Serial.println("[MAIN] State timeout, recovering from connecting screen");
      connectingScreenStart = 0;
      if (dpSettings.showClockAfterFinish) {
        setScreenState(SCREEN_CLOCK);
      } else {
        setScreenState(SCREEN_IDLE);
      }
    }
  } else {
    connectingScreenStart = 0;
  }
}

static void handleErrorBuzzers() {
  // Check for error state transition on any printer
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    BambuState& ps = printers[i].state;
    if (ps.gcodeStateId == GCODE_FAILED &&
        prevGcodeStateId[i] != GCODE_FAILED &&
        prevGcodeStateSeen[i]) {
      buzzerPlay(BUZZ_ERROR);
    }
    prevGcodeStateId[i] = ps.gcodeStateId;
    prevGcodeStateSeen[i] = true;
  }
}

// ---------------------------------------------------------------------------
//  Display rotation logic (multi-printer)
// ---------------------------------------------------------------------------
static void handleRotation() {
  if (rotState.mode == ROTATE_OFF) return;
  if (getActiveConnCount() < 2) return;

  // Don't rotate when display is in clock, off, or finished state,
  // UNLESS a printer is actively printing (wake up to show it)
  ScreenState scr = getScreenState();
  if (scr == SCREEN_CLOCK || scr == SCREEN_OFF || scr == SCREEN_FINISHED) {
    if (!anyPrinterPrinting()) return;
    // A printer started printing - wake display and let rotation proceed
    setBacklight(getEffectiveBrightness());
  }

  unsigned long now = millis();
  if (now - rotState.lastRotateMs < rotState.intervalMs) return;

  // Gather candidates
  uint8_t candidates[MAX_ACTIVE_PRINTERS];
  uint8_t candidateCount = 0;
  uint8_t printingCount = 0;
  uint8_t printingSlot = 0xFF;

  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    if (!printers[i].state.connected) continue;
    candidates[candidateCount++] = i;
    if (printers[i].state.printing) {
      printingCount++;
      printingSlot = i;
    }
  }

  if (candidateCount == 0) return;

  if (rotState.mode == ROTATE_SMART) {
    if (printingCount == 1) {
      // Only one printing - show it, no cycling
      if (rotState.displayIndex != printingSlot) {
        rotState.displayIndex = printingSlot;
        triggerDisplayTransition();
      }
      rotState.lastRotateMs = now;
      return;
    }
    // 0 or 2 printing: fall through to cycling
  }

  // Cycle to next candidate
  uint8_t current = rotState.displayIndex;
  for (uint8_t attempt = 1; attempt <= MAX_ACTIVE_PRINTERS; attempt++) {
    uint8_t next = (current + attempt) % MAX_ACTIVE_PRINTERS;
    for (uint8_t c = 0; c < candidateCount; c++) {
      if (candidates[c] == next && next != current) {
        rotState.displayIndex = next;
        triggerDisplayTransition();
        rotState.lastRotateMs = now;
        return;
      }
    }
  }

  rotState.lastRotateMs = now;
}

// ---------------------------------------------------------------------------
void setup() {
#if defined(BAT_EN)
  // Waveshare ESP32-S3-Touch-LCD-1.54 needs BAT_EN high to keep running
  // after releasing the center PWR button when booting from battery.
  gpio_hold_dis((gpio_num_t)BAT_EN);  // release hold from previous deep sleep
  pinMode(BAT_EN, OUTPUT);
  digitalWrite(BAT_EN, HIGH);
#endif
#if defined(BOARD_BTN_1)
  pinMode(BOARD_BTN_1, INPUT_PULLUP);
#endif
#if defined(BOARD_BTN_2)
  pinMode(BOARD_BTN_2, INPUT_PULLUP);
#endif
#if defined(BOARD_BTN_3)
  pinMode(BOARD_BTN_3, INPUT_PULLUP);
#endif
  Serial.begin(115200);
  Serial.printf("\n=== BambuHelper %s Starting ===\n", FW_VERSION);

  loadSettings();
  initDisplay();
  splashEnd = millis() + 2000;
  startWiFiDuringSplash();
  setBacklight(brightness);
}

void loop() {
  if (handleSplashPhase()) return;

  handleWiFi();
  handleWebServer();
  handleBoardPowerOff();
  handleWakeButton();

  if (isWiFiConnected() && !isAPMode()) {
    updateDisplayedPrinterScreenState();
  }

  handleDisplaySleepTimeouts();
  handleConnectingScreenRecovery();
  handleErrorBuzzers();

  buzzerTick();
  checkNightMode();
  updateDisplay();

  // MQTT and rotation after display update - TLS reconnect can block for
  // several seconds so we handle it last to keep UI responsive
  if (isWiFiConnected() && !isAPMode() && isAnyPrinterConfigured()) {
    handleBambuMqtt();
    handleRotation();
  }
}
