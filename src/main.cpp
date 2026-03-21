#include <Arduino.h>
#include "display_ui.h"
#include "settings.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "bambu_mqtt.h"
#include "config.h"
#include "bambu_state.h"
#include "button.h"

static unsigned long splashEnd = 0;
static unsigned long finishScreenStart = 0;

// ---------------------------------------------------------------------------
//  Display rotation logic (multi-printer)
// ---------------------------------------------------------------------------
static void handleRotation() {
  if (rotState.mode == ROTATE_OFF) return;
  if (getActiveConnCount() < 2) return;

  // Don't rotate when display is in clock or off state,
  // UNLESS a printer is actively printing (wake up to show it)
  ScreenState scr = getScreenState();
  if (scr == SCREEN_CLOCK || scr == SCREEN_OFF) {
    bool anyPrinting = false;
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
        anyPrinting = true;
        break;
      }
    }
    if (!anyPrinting) return;
    // A printer started printing — wake display and let rotation proceed
    setBacklight(brightness);
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
        setBacklight(brightness);
        finishScreenStart = 0;
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
            finishScreenStart = 0;
            break;
          }
        }
      }
    }

    // Auto-select screen based on displayed printer state
    BambuState& s = displayedPrinter().state;
    ScreenState current = getScreenState();

    if (!isAnyPrinterConfigured()) {
      if (current != SCREEN_IDLE && current != SCREEN_OFF) {
        setScreenState(SCREEN_IDLE);
        finishScreenStart = 0;
      }
    } else if (!s.connected && current != SCREEN_CONNECTING_MQTT &&
               current != SCREEN_OFF && current != SCREEN_CLOCK) {
      setScreenState(SCREEN_CONNECTING_MQTT);
      finishScreenStart = 0;
    } else if (!s.connected && (current == SCREEN_OFF || current == SCREEN_CLOCK)) {
      // Stay off/clock when printer is disconnected/off
    } else if (s.connected && s.printing) {
      if (current != SCREEN_PRINTING) {
        setScreenState(SCREEN_PRINTING);
        finishScreenStart = 0;
      }
    } else if (s.connected && !s.printing &&
               strcmp(s.gcodeState, "FINISH") == 0) {
      if (current != SCREEN_FINISHED && current != SCREEN_OFF && current != SCREEN_CLOCK) {
        setScreenState(SCREEN_FINISHED);
        finishScreenStart = millis();
      }
      // Only turn off/clock after timeout if NO printer is still printing
      if (current == SCREEN_FINISHED && !dpSettings.keepDisplayOn &&
          dpSettings.finishDisplayMins > 0 && finishScreenStart > 0 &&
          millis() - finishScreenStart > (unsigned long)dpSettings.finishDisplayMins * 60000UL) {
        bool anyPrinting = false;
        for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
          if (isPrinterConfigured(i) && printers[i].state.connected && printers[i].state.printing) {
            anyPrinting = true;
            break;
          }
        }
        if (!anyPrinting) {
          // Never go to SCREEN_OFF without a physical button — no way to wake up
          if (dpSettings.showClockAfterFinish || buttonType == BTN_DISABLED) {
            setScreenState(SCREEN_CLOCK);
          } else {
            setScreenState(SCREEN_OFF);
          }
        }
      }
    } else if (s.connected && !s.printing &&
               strcmp(s.gcodeState, "FINISH") != 0) {
      if (current == SCREEN_OFF || current == SCREEN_CLOCK) {
        setBacklight(brightness);
      }
      if (current != SCREEN_IDLE) {
        setScreenState(SCREEN_IDLE);
        finishScreenStart = 0;
      }
    }
  }

  updateDisplay();
}
