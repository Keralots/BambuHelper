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

static unsigned long splashEnd = 0;
static unsigned long finishScreenStart = 0;
static bool finishActive = false;          // guards finishScreenStart against millis() wrap
static unsigned long idleClockStart = 0;  // when all printers became idle
static bool idleClockActive = false;      // guards idleClockStart against millis() wrap
static unsigned long connectingScreenStart = 0;  // for stuck-state timeout
static char prevGcodeState[MAX_ACTIVE_PRINTERS][16] = {{0}};

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
    bool anyPrinting = false;
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
        anyPrinting = true;
        break;
      }
    }
    if (!anyPrinting) return;
    // A printer started printing — wake display and let rotation proceed
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
      // Only one printing — show it, no cycling
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
  Serial.begin(115200);
  Serial.printf("\n=== BambuHelper %s Starting ===\n", FW_VERSION);

  loadSettings();
  initDisplay();
  splashEnd = millis() + 2000;
  setBacklight(brightness);
}

void loop() {
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
    return;
  }

  handleWiFi();
  handleWebServer();

  if (isWiFiConnected() && !isAPMode()) {
    if (isAnyPrinterConfigured()) {
      handleBambuMqtt();
      handleRotation();
    }

    // Handle physical button press
    if (wasButtonPressed()) {
      ScreenState cur = getScreenState();
      if (cur == SCREEN_OFF || cur == SCREEN_CLOCK) {
        // Wake from sleep + reset backoff for immediate reconnect
        setBacklight(getEffectiveBrightness());
        finishActive = false;
        idleClockActive = false;
        resetMqttBackoff();
        setScreenState(SCREEN_IDLE);  // state machine will correct on next loop
      } else if (getActiveConnCount() >= 2) {
        // Cycle to next configured printer
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
      } else if (cur == SCREEN_IDLE &&
                 isCloudMode(displayedPrinter().config.mode) &&
                 strcmp(displayedPrinter().state.gcodeState, "UNKNOWN") == 0) {
        // Single printer, cloud, UNKNOWN - manual refresh
        requestCloudRefresh(rotState.displayIndex);
      }
    }

    // Auto-select screen based on displayed printer state
    BambuState& s = displayedPrinter().state;
    ScreenState current = getScreenState();

    if (!isAnyPrinterConfigured()) {
      if (current != SCREEN_IDLE && current != SCREEN_OFF) {
        setScreenState(SCREEN_IDLE);
        finishActive = false;
      }
    } else if (isOtaAutoInProgress()) {
      if (current != SCREEN_OTA_UPDATE) setScreenState(SCREEN_OTA_UPDATE);
    } else if (!s.connected && current != SCREEN_CONNECTING_MQTT &&
               current != SCREEN_OFF && current != SCREEN_CLOCK) {
      setScreenState(SCREEN_CONNECTING_MQTT);
      finishActive = false;
      connectingScreenStart = millis();
    } else if (!s.connected && (current == SCREEN_OFF || current == SCREEN_CLOCK)) {
      // Stay off/clock when printer is disconnected/off
    } else if (s.connected && s.printing) {
      if (current != SCREEN_PRINTING) {
        setScreenState(SCREEN_PRINTING);
        finishActive = false;
        if (tasmotaSettings.assignedSlot == 255 ||
            tasmotaSettings.assignedSlot == rotState.displayIndex)
          tasmotaMarkPrintStart();
      }
      s.finishBuzzerPlayed = false;  // reset for next finish event
      s.doorAcknowledged = false;    // reset door ack for next finish
    } else if (s.connected && !s.printing &&
               strcmp(s.gcodeState, "FINISH") == 0) {
      if (current != SCREEN_FINISHED && current != SCREEN_OFF && current != SCREEN_CLOCK) {
        if (tasmotaSettings.enabled &&
            (tasmotaSettings.assignedSlot == 255 ||
             tasmotaSettings.assignedSlot == rotState.displayIndex))
          tasmotaMarkPrintEnd();
        setScreenState(SCREEN_FINISHED);
        finishScreenStart = millis();
        finishActive = true;
        if (!s.finishBuzzerPlayed) {
          buzzerPlay(BUZZ_PRINT_FINISHED);
          s.finishBuzzerPlayed = true;
        }
      }

      // Door acknowledge: wait for door open before starting timeout
      bool waitingForDoor = dpSettings.doorAckEnabled && s.doorSensorPresent
                            && !s.doorAcknowledged;
      if (waitingForDoor && s.doorOpen) {
        s.doorAcknowledged = true;
        finishScreenStart = millis();  // restart timeout from door open moment
        finishActive = true;
        Serial.println("Door opened - print removal acknowledged, starting timeout");
      }

      // Transition from finish screen to clock/off
      // If door ack is enabled and door not yet opened, block the transition
      if (current == SCREEN_FINISHED && !dpSettings.keepDisplayOn &&
          !waitingForDoor && finishActive) {
        // finishDisplayMins==0: go to clock immediately if enabled, otherwise stay on finish
        // finishDisplayMins>0: wait for timeout before transitioning
        bool timeoutReached = (dpSettings.finishDisplayMins > 0) &&
            (millis() - finishScreenStart > (unsigned long)dpSettings.finishDisplayMins * 60000UL);
        bool immediateClockTransition = (dpSettings.finishDisplayMins == 0) &&
            (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED);

        if (timeoutReached || immediateClockTransition) {
          bool anyPrinting = false;
          for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
            if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
              anyPrinting = true;
              break;
            }
          }
          if (!anyPrinting) {
            if (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED) {
              setScreenState(SCREEN_CLOCK);
            } else {
              setScreenState(SCREEN_OFF);
            }
          }
        }
      }
    } else if (s.connected && !s.printing &&
               strcmp(s.gcodeState, "FINISH") != 0) {
      // SCREEN_CLOCK and SCREEN_OFF are sticky — only button press or
      // new print (s.printing → SCREEN_PRINTING) exits them
      if (current == SCREEN_CLOCK || current == SCREEN_OFF) {
        // nothing — stay asleep while printer is idle
      } else if (current != SCREEN_IDLE) {
        if (current == SCREEN_CONNECTING_MQTT) buzzerPlay(BUZZ_CONNECTED);
        setScreenState(SCREEN_IDLE);
        finishActive = false;
        idleClockActive = false;
      }
    }
  }

  // Idle/Connecting → Clock/Off: if all printers are idle or disconnected,
  // transition to clock or off after finishDisplayMins timeout.
  // Covers both SCREEN_IDLE (printer connected but not printing) and
  // SCREEN_CONNECTING_MQTT (printer offline/unreachable at startup).
  ScreenState cur = getScreenState();
  if ((cur == SCREEN_IDLE || cur == SCREEN_CONNECTING_MQTT) &&
      !dpSettings.keepDisplayOn && dpSettings.finishDisplayMins > 0) {
    bool anyBusy = false;
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
        anyBusy = true;
        break;
      }
    }
    if (!anyBusy) {
      if (!idleClockActive) { idleClockStart = millis(); idleClockActive = true; }
      if (millis() - idleClockStart > (unsigned long)dpSettings.finishDisplayMins * 60000UL) {
        if (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED) {
          setScreenState(SCREEN_CLOCK);
        } else {
          setScreenState(SCREEN_OFF);
        }
      }
    } else {
      idleClockActive = false;
    }
  } else if (cur != SCREEN_IDLE && cur != SCREEN_CONNECTING_MQTT) {
    idleClockActive = false;
  }

  // Stuck-state timeout: recover if stuck in a connecting screen too long
  {
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

  // Check for error state transition on any printer
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    BambuState& ps = printers[i].state;
    if (strcmp(ps.gcodeState, "FAILED") == 0 &&
        strcmp(prevGcodeState[i], "FAILED") != 0 &&
        prevGcodeState[i][0] != '\0') {
      buzzerPlay(BUZZ_ERROR);
    }
    strlcpy(prevGcodeState[i], ps.gcodeState, sizeof(prevGcodeState[i]));
  }

  buzzerTick();
  checkNightMode();
  updateDisplay();
}
