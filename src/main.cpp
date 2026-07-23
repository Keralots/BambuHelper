#include <Arduino.h>
#include "display_ui.h"
#include "settings.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ssdp_discovery.h"
#include "bambu_mqtt.h"
#include "config.h"
#include "bambu_state.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "tasmota.h"
#include "battery.h"
#include "camera_client.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

static unsigned long splashEnd = 0;
static unsigned long finishScreenStart = 0;
static bool finishActive = false;          // guards finishScreenStart against millis() wrap
static unsigned long idleClockStart = 0;   // when all printers became idle
static bool idleClockActive = false;       // guards idleClockStart against millis() wrap
static bool finishDismissedByWake = false;  // true once user taps to wake while printer is GCODE_FINISH; cleared on printer state change
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
static unsigned long boardBtnPressStartMs = 0;
#endif

// --- Button-driven plug power control (#136) --------------------------------
static const uint32_t POWER_MULTICLICK_MS         = 450;   // double/triple-click window
static const uint32_t POWER_CONFIRM_HOLD_MS       = 1500;  // hold-to-confirm duration
static const uint32_t POWER_CONFIRM_INACTIVITY_MS = 10000; // auto-cancel on no input
static const uint32_t POWER_RESULT_MS             = 1200;  // success/fail flash duration

enum PowerConfirmPhase { PC_WAIT_RELEASE = 0, PC_ARMED = 1, PC_SENDING = 2, PC_RESULT = 3 };

// Multi-click buffer (only armed when a plug is mapped to the shown printer).
static uint8_t  pcPendingClicks = 0;
static uint8_t  pcPendingSlot   = 0;
static uint32_t pcLastTapMs     = 0;

// Confirm-modal snapshot (frozen at open, never recomputed per frame).
static PowerConfirmPhase pcPhase = PC_WAIT_RELEASE;
static uint8_t     pcSlot            = 0;
static uint8_t     pcPlug            = 0xFF;
static bool        pcDesiredOn       = false;
static bool        pcWasPrinting     = false;
static bool        pcResultOk        = false;
static ScreenState pcPriorScreen     = SCREEN_IDLE;
static uint32_t    pcPressStartMs    = 0;
static uint32_t    pcLastActivityMs  = 0;
static uint32_t    pcResultUntilMs   = 0;
static float       pcProgress        = 0.0f;
static bool        pcPrevHeld        = false;
static volatile bool pcSendingDrawn  = false;

// AMS drying peek (#150): tapping during a print brings the drying screen up for
// a few seconds, then it closes itself.
static const uint32_t DRY_PEEK_MS = 10000;
static uint32_t dryPeekUntilMs = 0;

static bool isPrinterActivityStateFresh(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS || !isPrinterConfigured(slot)) return false;
  BambuState& s = printers[slot].state;
  if (s.connected) return true;
  if (s.lastUpdate == 0) return false;

  unsigned long staleMs = isCloudMode(printers[slot].config.mode)
                          ? (unsigned long)BAMBU_STALE_TIMEOUT * 5UL
                          : (unsigned long)BAMBU_STALE_TIMEOUT;
  return millis() - s.lastUpdate <= staleMs;
}

static bool isPrinterActiveForDisplay(uint8_t slot) {
  if (!isPrinterActivityStateFresh(slot)) return false;
  BambuState& s = printers[slot].state;
  return s.printing || s.ams.anyDrying;
}

static bool anyPrinterPrinting() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (isPrinterActivityStateFresh(i) && printers[i].state.printing) {
      return true;
    }
  }
  return false;
}

static bool anyPrinterDrying() {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (isPrinterActivityStateFresh(i) && printers[i].state.ams.anyDrying) {
      return true;
    }
  }
  return false;
}

static bool isSleepStickyScreen(ScreenState state) {
  return state == SCREEN_CLOCK || state == SCREEN_OFF;
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
    initLed();
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
      unsigned long now = millis();
      rotState.lastRotateMs = now;  // reset auto-rotate timer
      // Suppress Smart snap-to-active so user can peek at an idle slot while
      // another printer is printing/drying. Window matches the rotate interval.
      rotState.displayHoldUntilMs = now + rotState.intervalMs;
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
  if (raw && !boardBtnStable) {
    result = true;
    boardBtnPressStartMs = millis();
  } else if (!raw && boardBtnStable) {
    boardBtnPressStartMs = 0;
  }
  boardBtnStable = raw;
  return result;
#else
  return false;
#endif
}

static bool isBoardButtonHeld() {
#if defined(BOARD_BTN_1)
  return boardBtnStable;
#else
  return false;
#endif
}

static uint32_t boardButtonHoldDurationMs() {
#if defined(BOARD_BTN_1)
  if (!boardBtnStable || boardBtnPressStartMs == 0) return 0;
  return (uint32_t)(millis() - boardBtnPressStartMs);
#else
  return 0;
#endif
}

static bool isBoardButton3Held() {
#if defined(BOARD_BTN_3)
  return digitalRead(BOARD_BTN_3) == LOW;
#else
  return false;
#endif
}

// Open the AMS drying peek (#150) for the shown printer, if it has anything to
// show. Single entry point on purpose: there are two call sites (tap from the
// print screen, and tapping out of the camera) and both MUST arm the deadline -
// setting the screen state without it leaves a stale value and the expiry check
// closes the peek on the very next loop.
//
// state.printing is required rather than just "the print screen is up": with
// keepPrintScreen the dashboard is also shown while idle, and the sticky branch
// in updateDisplayedPrinterScreenState() demands real printing, so mismatched
// predicates would open a peek that immediately closes.
static bool openDryPeek() {
  PrinterSlot& p = displayedPrinter();
  if (!p.state.printing || !p.state.ams.anyDrying) return false;
  dryPeekUntilMs = millis() + DRY_PEEK_MS;
  setScreenState(SCREEN_DRY_PEEK);
  return true;
}

// Leave the peek. Auto-rotation is frozen while it is up but lastRotateMs keeps
// aging, and the minimum rotate interval is also 10s - without this reset the
// user taps to check drying and lands on a different printer the moment it
// closes.
static void closeDryPeek() {
  rotState.lastRotateMs = millis();
  setScreenState(SCREEN_PRINTING);
}

// Existing on-press behavior, factored out so it can be invoked either on
// press-edge (LED disabled path: unchanged behavior) or on release-edge
// (LED enabled path: deferred until tap/hold disambiguation completes).
static void doTapActions() {
  ScreenState cur = getScreenState();

  if (isSleepStickyScreen(cur)) {
    setBacklight(getEffectiveBrightness());
    finishActive = false;
    idleClockActive = false;
    resetMqttBackoff();
    deferMqttReconnect();  // skip blocking reconnect this iteration so screen wakes instantly
    setScreenState(SCREEN_IDLE);  // state machine will correct on next loop
    // If the displayed printer is in GCODE_FINISH, user has now dismissed
    // the finished-print banner by waking — don't let the state machine
    // bounce IDLE → FINISHED → CLOCK in the next iteration. Cleared when
    // the printer moves away from GCODE_FINISH (new print starts).
    // Skip when keepPrintScreen is set: that mode wants the kept print
    // dashboard restored on wake, not the dismissed-finish idle screen.
    if (!dpSettings.keepPrintScreen &&
        isAnyPrinterConfigured() && isWiFiConnected() && !isAPMode()) {
      if (displayedPrinter().state.gcodeStateId == GCODE_FINISH) {
        finishDismissedByWake = true;
      }
    }
    return;
  }

#if defined(BOARD_HAS_CAMERA)
  // Camera tap toggle (#120). On a multi-printer setup the camera sits in the
  // tap cycle for the printer that carries the tile: tapping that printer opens
  // fullscreen, and tapping out advances to the next printer (so the cycle keeps
  // moving instead of stranding the user here until the next auto-rotate).
  if (cur == SCREEN_CAMERA) {
    // Keep the drying peek reachable on camera boards by making it the next
    // stop in the cycle: printing -> camera -> drying -> printing (+ next).
    if (openDryPeek()) return;
    setScreenState(SCREEN_PRINTING);
    if (getActiveConnCount() >= 2) cycleDisplayedPrinterFromButton();
    return;
  }
  if (cur == SCREEN_PRINTING && cameraDisplayedHasCameraTile() &&
      cameraCanStreamDisplayedPrinter()) {
    setScreenState(SCREEN_CAMERA);
    return;
  }
#endif

  // AMS drying peek (#150). During a print the tap has no job on a
  // single-printer setup, so it brings up the drying view for a few seconds.
  // Tapping out advances the printer when more than one is connected, matching
  // how the camera tile behaves.
  if (cur == SCREEN_DRY_PEEK) {
    closeDryPeek();
    if (getActiveConnCount() >= 2) cycleDisplayedPrinterFromButton();
    return;
  }
  if (cur == SCREEN_PRINTING && openDryPeek()) return;

  if (getActiveConnCount() >= 2) {
    cycleDisplayedPrinterFromButton();
    return;
  }

  if (isCloudMode(displayedPrinter().config.mode) &&
      !displayedPrinter().state.printing) {
    requestCloudRefresh(rotState.displayIndex);
  }
}

// --- Button-driven plug power control (#136) --------------------------------
// The multi-click watcher is armed only when the setting is on, a plug is mapped
// to the shown printer, and the screen is a single-printer browseable one. When
// disarmed the tap path stays bit-for-bit unchanged (zero latency).
static bool powerControlAvailableForSlot(uint8_t slot) {
  if (!dispSettings.buttonPowerControl) return false;
  if (tasmotaControlPlugForSlot(slot) == 0xFF) return false;
  ScreenState s = getScreenState();
  // SCREEN_CONNECTING_MQTT is the screen shown for a powered-OFF local printer -
  // the primary "turn the plug on" case (#136), so it must be armed there too.
  return (s == SCREEN_IDLE || s == SCREEN_PRINTING || s == SCREEN_FINISHED ||
          s == SCREEN_CONNECTING_MQTT);
}

static void openPowerConfirm(uint8_t slot) {
  pcSlot = slot;
  pcPlug = tasmotaControlPlugForSlot(slot);
  TasmotaPlugStatsView v;
  tasmotaGetStats(pcPlug, &v);
  // Mirror the web-UI inference: Shelly reports relay state, Tasmota infers from watts.
  bool currentOn = v.powerStateKnown ? v.powerOn : (v.online && v.watts > 0.5f);
  pcDesiredOn      = !currentOn;
  pcWasPrinting    = isPrintingGcodeState(printers[slot].state.gcodeStateId);
  pcPriorScreen    = getScreenState();
  pcPhase          = PC_WAIT_RELEASE;   // require a finger release before arming
  pcPrevHeld       = true;
  pcProgress       = 0.0f;
  pcLastActivityMs = millis();
  pcSendingDrawn   = false;
  setScreenState(SCREEN_POWER_CONFIRM);
}

// Replaces the direct doTapActions() call at both tap dispatch sites. Buffers
// clicks only when power control is armed for the shown printer; otherwise the
// tap is dispatched immediately with no pending state and no rotation-hold.
static void registerTap() {
  uint8_t slot = rotState.displayIndex;
  if (!powerControlAvailableForSlot(slot)) {
    doTapActions();
    return;
  }
  uint32_t now = millis();
  if (pcPendingClicks == 0) {
    pcPendingSlot = slot;   // frozen at click 1
    // Hold the shown printer for the window so rotation/snap can't drift the target.
    rotState.displayHoldUntilMs = now + POWER_MULTICLICK_MS + 200;
  }
  if (pcPendingClicks < 255) pcPendingClicks++;
  pcLastTapMs = now;
}

// Modal input state machine. Called every loop while SCREEN_POWER_CONFIRM is up.
static void handlePowerConfirmInput(bool held) {
  uint32_t now = millis();
  switch (pcPhase) {
    case PC_WAIT_RELEASE:
      pcProgress = 0.0f;
      if (!held) {                  // consume the opening release, then arm
        pcPhase          = PC_ARMED;
        pcPrevHeld       = false;
        pcLastActivityMs = now;
      }
      return;

    case PC_ARMED: {
      bool pressEdge   = (held && !pcPrevHeld);
      bool releaseEdge = (!held && pcPrevHeld);
      pcPrevHeld = held;
      if (pressEdge) {
        pcPressStartMs   = now;
        pcLastActivityMs = now;
      }
      if (held) {
        uint32_t hm = now - pcPressStartMs;
        pcProgress = (float)hm / (float)POWER_CONFIRM_HOLD_MS;
        if (pcProgress > 1.0f) pcProgress = 1.0f;
        if (hm >= POWER_CONFIRM_HOLD_MS) {     // hold-to-confirm: fire while held
          pcProgress     = 1.0f;
          pcSendingDrawn = false;
          pcPhase        = PC_SENDING;
        }
      } else {
        pcProgress = 0.0f;
        if (releaseEdge) {                      // short tap -> cancel
          setScreenState(pcPriorScreen);
          pcPhase = PC_WAIT_RELEASE;
          return;
        }
        if (now - pcLastActivityMs > POWER_CONFIRM_INACTIVITY_MS) {
          setScreenState(pcPriorScreen);        // inactivity timeout -> cancel
          pcPhase = PC_WAIT_RELEASE;
        }
      }
      return;
    }

    case PC_SENDING:
    case PC_RESULT:
      return;   // driven by handlePowerConfirmService() after the frame flush
  }
}

static void handleWakeButton() {
  // Both edge pollers MUST be called every loop unconditionally - each owns its
  // own debounce + held-state machine, and skipping a call would freeze it.
  bool touchPress = wasButtonPressed();
  bool boardPress = wasBoardButtonPressed();

  bool held = isButtonHeld() || isBoardButtonHeld();
  uint32_t touchHoldMs = buttonHoldDurationMs();
  uint32_t boardHoldMs = boardButtonHoldDurationMs();
  uint32_t holdMs = (touchHoldMs > boardHoldMs) ? touchHoldMs : boardHoldMs;
  bool suppressDim = isBoardButton3Held();
#if defined(USE_XPT2046)
  // Resistive panels (CYD, TZT) register a wake touch as a long press that
  // easily crosses the 300ms hold threshold. That would ramp the LED (default
  // direction is up, toward max), save it, and consume the press so the screen
  // never wakes. Suppress dimming while asleep so the touch only wakes.
  // Capacitive panels get crisp short taps and keep hold-to-dim on the
  // screensaver, so this gate is XPT2046-only.
  if (isSleepStickyScreen(getScreenState())) suppressDim = true;
#endif

  // Tap/hold disambiguation state (LED-enabled path). Hoisted so the power-confirm
  // intercept can keep them in sync and avoid a stray release-edge tap on close.
  static bool wasHeldPrev = false;
  static bool holdConsumedThisPress = false;

  // Plug power-confirm modal (#136) owns all input while up. Still tick the dimmer
  // (suppressed) so its 2 s save-debounce keeps draining and no dim session starts,
  // then hand off and return before the normal tap/hold logic.
  if (getScreenState() == SCREEN_POWER_CONFIRM) {
    ledHoldDimUpdate(held, holdMs, /*suppressDim=*/true);
    if (touchPress || boardPress) { buzzerPlayClick(); ledOnUserInteraction(); }
    handlePowerConfirmInput(held);
    wasHeldPrev = held;
    holdConsumedThisPress = false;
    return;
  }

  // Flush a buffered multi-click once the window closes: 1 click = the normal tap
  // action, 2+ = open the power-confirm modal for the frozen target slot.
  if (pcPendingClicks > 0 && (millis() - pcLastTapMs) > POWER_MULTICLICK_MS) {
    uint8_t n = pcPendingClicks;
    pcPendingClicks = 0;
    if (n >= 2) { openPowerConfirm(pcPendingSlot); return; }
    doTapActions();
  }

  // Tick the dimmer every loop regardless of state - it owns the 2 s save debounce.
  bool holdConsumed = ledHoldDimUpdate(held, holdMs, suppressDim);

  // LED disabled or unconfigured: take the ORIGINAL press-edge path (with the new
  // multi-click shim). The dimmer's entry guard prevents any dim session, so
  // holdConsumed is always false here.
  if (!ledSettings.enabled) {
    if (touchPress || boardPress) {
      buzzerPlayClick();
      ledOnUserInteraction();
      registerTap();
    }
    return;
  }

  // LED enabled: tap/hold disambiguation.
  if (touchPress || boardPress) {
    // Press edge - immediate feedback (preserves today's snappy feel).
    buzzerPlayClick();
    ledOnUserInteraction();
    holdConsumedThisPress = false;
  }

  if (holdConsumed) holdConsumedThisPress = true;

  bool releaseEdge = (wasHeldPrev && !held);
  wasHeldPrev = held;

  if (releaseEdge && !holdConsumedThisPress) {
    // Was a tap - fire deferred actions (sub-100 ms perceived delay).
    registerTap();
  }
}

// Accessor for the renderer (display_ui.cpp). Read-only snapshot; never mutates
// the confirm state. offline is read live for the badge only.
bool powerConfirmGetView(PowerConfirmView* out) {
  if (!out || getScreenState() != SCREEN_POWER_CONFIRM) return false;
  out->name      = printers[pcSlot].config.name;
  out->desiredOn = pcDesiredOn;
  out->warn      = pcWasPrinting;
  out->progress  = pcProgress;
  out->phase     = (int)pcPhase;
  out->resultOk  = pcResultOk;
  TasmotaPlugStatsView v;
  tasmotaGetStats(pcPlug, &v);
  out->offline = !v.online;
  return true;
}

void powerConfirmMarkSendingDrawn() { pcSendingDrawn = true; }

// Runs after updateDisplay()+flushFrame() so the "Sending..." frame is committed
// before the blocking relay command, and the result flash shows before close.
static void handlePowerConfirmService() {
  if (getScreenState() != SCREEN_POWER_CONFIRM) return;
  uint32_t now = millis();
  if (pcPhase == PC_SENDING) {
    if (!pcSendingDrawn) return;   // wait until the frame is on screen
    pcResultOk = tasmotaSetPower(pcPlug, pcDesiredOn);
    buzzerPlay(pcResultOk ? BUZZ_CONNECTED : BUZZ_ERROR);
    pcPhase         = PC_RESULT;
    pcResultUntilMs = now + POWER_RESULT_MS;
  } else if (pcPhase == PC_RESULT) {
    if ((long)(now - pcResultUntilMs) >= 0) {
      setScreenState(pcPriorScreen);
      pcPhase = PC_WAIT_RELEASE;
    }
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
  // Fire the print-finished notification on the GCODE_FINISH transition itself,
  // independent of the screen-state gate below. Otherwise sleep-sticky screens
  // (clock / display off) silently swallow the alert.
  if (!s.finishBuzzerPlayed) {
    buzzerPlay(BUZZ_PRINT_FINISHED);
    ledStartFinishEffect();
    s.finishBuzzerPlayed = true;
  }

  if (current != SCREEN_FINISHED && !isSleepStickyScreen(current) &&
      !(current == SCREEN_IDLE && s.ams.anyDrying) &&
      !(current == SCREEN_PRINTING && finishActive) &&
      !(current == SCREEN_IDLE && finishDismissedByWake)) {
    setScreenState(dpSettings.keepPrintScreen ? SCREEN_PRINTING : SCREEN_FINISHED);
    finishScreenStart = millis();
    finishActive = true;
  }
  ledSetActivity(LED_ACT_FINISHED);

  // Door acknowledge: wait for door open before starting timeout
  bool waitingForDoor = dpSettings.doorAckEnabled && s.doorSensorPresent &&
                        !s.doorAcknowledged;
  if (waitingForDoor && s.doorOpen) {
    s.doorAcknowledged = true;
    finishScreenStart = millis();  // restart timeout from door open moment
    finishActive = true;
    ledStopFinishEffect();  // user came to grab the print, kill the alert
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
  if (s.gcodeStateId != GCODE_FINISH) {
    finishDismissedByWake = false;
  }
  if (s.printing) {
    if (current != SCREEN_PRINTING) {
      setScreenState(SCREEN_PRINTING);
    }
    if (finishActive) {
      finishActive = false;
      idleClockActive = false;
    }
    s.finishBuzzerPlayed = false;  // reset for next finish event
    s.doorAcknowledged = false;    // reset door ack for next finish
    ledStopFinishEffect();         // new print starts - kill any leftover alert
    ledSetActivity(s.gcodeStateId == GCODE_PAUSE ? LED_ACT_PAUSED : LED_ACT_PRINTING);
    return;
  }

  if (s.gcodeStateId == GCODE_FINISH) {
    handleDisplayedPrinterFinishState(current, s);
    return;
  }

  // Idle / failed / unknown
  if (s.gcodeStateId == GCODE_FAILED) {
    ledSetActivity(LED_ACT_FAILED);
  } else {
    ledSetActivity(LED_ACT_IDLE);
  }
  handleDisplayedPrinterIdleState(current, s);
}

static void updateDisplayedPrinterScreenState() {
  ScreenState current = getScreenState();

  // Default activity for early-return paths (no printer / OTA / disconnected).
  // handleDisplayedPrinterConnectedState() overrides for live-state branches.
  ledSetActivity(LED_ACT_IDLE);

  if (!isAnyPrinterConfigured()) {
    if (current != SCREEN_IDLE && current != SCREEN_OFF) {
      setScreenState(SCREEN_IDLE);
      finishActive = false;
    }
    return;
  }

  if (isOtaAutoInProgress()) {
    if (current != SCREEN_OTA_UPDATE) {
      setScreenState(SCREEN_OTA_UPDATE);
    }
    return;
  }

  // Plug power-confirm modal (#136) is sticky: hold it against the auto state
  // machine (dismissed only by the modal's own input / timeout). Placed after OTA
  // so an auto-OTA still preempts it.
  if (current == SCREEN_POWER_CONFIRM) return;

#if defined(BOARD_HAS_CAMERA)
  // Camera fullscreen (#120) is sticky: entered/exited only by tap. Hold it
  // until the user taps out or the printer can no longer stream, then drop back
  // to the normal flow (which re-derives the screen next loop).
  if (current == SCREEN_CAMERA) {
    if (cameraCanStreamDisplayedPrinter()) return;
    setScreenState(SCREEN_PRINTING);
    return;
  }
#endif

  // AMS drying peek (#150) is sticky for its timeout: hold it against the auto
  // state machine, then drop back and let the normal flow re-derive next loop.
  // Placed above the drying-wake and split branches so neither steals it.
  // Note this is not the only expiry check - handleDisplaySleepTimeouts() runs
  // even when WiFi is down, where this function is never called at all.
  if (current == SCREEN_DRY_PEEK) {
    bool expired = (long)(millis() - dryPeekUntilMs) >= 0;
    if (!expired &&
        isPrinterActivityStateFresh(rotState.displayIndex) &&
        displayedPrinter().state.printing &&
        displayedPrinter().state.ams.anyDrying) return;
    closeDryPeek();
    return;
  }

  // Global drying-wake: if any fresh printer state is drying, leave sleep-sticky
  // screens regardless of which slot is currently displayed. Point displayIndex
  // at the dryer so the rendered drying screen reflects real state.
  // Suppressed while a display hold is in effect (manual peek or fresh finish) so
  // the held slot is not stolen by a dryer in another slot.
  unsigned long nowMs = millis();
  bool displayHold =
      rotState.displayHoldUntilMs != 0 &&
      (long)(rotState.displayHoldUntilMs - nowMs) > 0;
  if (!displayHold && isSleepStickyScreen(current) && anyPrinterDrying()) {
    uint8_t displayed = rotState.displayIndex < MAX_ACTIVE_PRINTERS
                        ? rotState.displayIndex : 0;
    if (!isPrinterActivityStateFresh(displayed) ||
        !printers[displayed].state.ams.anyDrying) {
      for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
        if (isPrinterActivityStateFresh(i) && printers[i].state.ams.anyDrying) {
          rotState.displayIndex = i;
          triggerDisplayTransition();
          break;
        }
      }
    }
    setScreenState(SCREEN_IDLE);
    setBacklight(getEffectiveBrightness());
    finishActive = false;
    idleClockActive = false;
    return;
  }

  // Split (dual-printer) screen: when the checkbox is on and the layout
  // supports it, show two active printers together instead of rotating. Composes
  // with the rotation mode - below 2 active printers the normal single-printer
  // logic runs. Suppressed during a display hold so a fresh finish / manual peek
  // keeps its single-printer screen for one interval.
  if ((rotState.splitEnabled || rotState.splitForce) && displaySupportsSplit() && !displayHold) {
    uint8_t a = 0, b = 0;
    bool engage = false;
    if (rotState.splitForce) {
      // Testing override: always split the first two configured slots, even
      // when idle/disconnected, so the layout can be checked without two prints.
      uint8_t cfg[MAX_ACTIVE_PRINTERS];
      uint8_t cfgCount = 0;
      for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
        if (isPrinterConfigured(i)) cfg[cfgCount++] = i;
      }
      if (cfgCount >= 2) { a = cfg[0]; b = cfg[1]; engage = true; }
    } else {
      uint8_t active[MAX_ACTIVE_PRINTERS];
      uint8_t activeCount = 0;
      for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
        if (isPrinterActiveForDisplay(i)) active[activeCount++] = i;
      }
      if (activeCount >= 2) { a = active[0]; b = active[1]; engage = true; }
    }
    if (engage) {
      bool pairChanged = (current != SCREEN_SPLIT) ||
                         (rotState.displayIndex != a) || (rotState.splitIndexB != b);
      rotState.displayIndex = a;
      rotState.splitIndexB  = b;
      if (pairChanged) triggerDisplayTransition();  // clear caches for the new pair
      // Wake the panel only on entry (night dimming stays handled by
      // checkNightMode()); avoids a redundant backlight write every loop.
      if (current != SCREEN_SPLIT) setBacklight(getEffectiveBrightness());
      setScreenState(SCREEN_SPLIT);
      finishActive = false;
      idleClockActive = false;
      return;
    }
  }

  BambuState& s = displayedPrinter().state;
  if (!s.connected) {
    uint8_t displayed = rotState.displayIndex < MAX_ACTIVE_PRINTERS
                        ? rotState.displayIndex : 0;
    if (isPrinterActiveForDisplay(displayed)) {
      handleDisplayedPrinterConnectedState(current, s);
      return;
    }
    // A WiFi/MQTT blip during/after a finish would otherwise leave the
    // "user dismissed the post-finish banner" flag sticky forever — and
    // when the printer reconnects on the NEXT print (still GCODE_FINISH
    // briefly at handover), the finish screen would be silently skipped.
    finishDismissedByWake = false;
    if (current != SCREEN_CONNECTING_MQTT && !isSleepStickyScreen(current)) {
      setScreenState(SCREEN_CONNECTING_MQTT);
      finishActive = false;
      connectingScreenStart = millis();
    }
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

  // AMS drying peek (#150) auto-close. This runs unconditionally, unlike
  // updateDisplayedPrinterScreenState() which is gated on WiFi being up - a
  // normal STA drop does not change the screen and AP fallback can be 15 min
  // away, so without this the peek would sit there for the whole outage.
  if (cur == SCREEN_DRY_PEEK && (long)(millis() - dryPeekUntilMs) >= 0) {
    closeDryPeek();
    cur = getScreenState();
  }

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

// Schedule a chamber-light-off for a slot if the given automation flag is set.
// The off is published later by processLightTimers() once the delay elapses.
// A delay of 0 fires on the next loop (due = now, but never the 0 sentinel).
static void scheduleLightOff(uint8_t slot, uint8_t flag) {
  PrinterConfig& cfg = printers[slot].config;
  if (!(cfg.lightFlags & flag)) return;
  unsigned long due = millis() + (unsigned long)cfg.lightOffDelayMin * 60000UL;
  if (due == 0) due = 1;  // 0 means "none pending"
  printers[slot].state.lightOffDueMs = due;
}

// Fire any chamber-light-off whose delay has elapsed. Called once per loop.
static void processLightTimers() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    BambuState& ps = printers[i].state;
    if (ps.lightOffDueMs != 0 && (long)(now - ps.lightOffDueMs) >= 0) {
      ps.lightOffDueMs = 0;
      requestLightCommand(i, false);
    }
  }
}

static void handleGcodeStateTransitions() {
  // Per-slot transition tracking. All transition checks must happen BEFORE
  // updating prevGcodeStateId[i] at the end - so a single combined helper
  // is the only safe way to handle multiple transition-driven behaviors.
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    BambuState& ps = printers[i].state;

    if (prevGcodeStateSeen[i]) {
      if (ps.gcodeStateId == GCODE_FAILED && prevGcodeStateId[i] != GCODE_FAILED) {
        buzzerPlay(BUZZ_ERROR);
        scheduleLightOff(i, LIGHT_OFF_ON_FAILED);
      }
      if (ps.gcodeStateId == GCODE_FINISH && prevGcodeStateId[i] != GCODE_FINISH) {
        if (buzzerSettings.bedCooldownAlert) {
          ps.bedCooldownAlertArmed = true;
        }
        // Announce the finish even if this slot is not the one on screen: fire the
        // one-shot per-slot alert, snap the display to the finished slot, and hold
        // it there for one rotation interval so the finish screen is seen. The
        // finish screen itself is set by the displayed-printer path next loop.
        if (!ps.finishBuzzerPlayed) {
          buzzerPlay(BUZZ_PRINT_FINISHED);
          ledStartFinishEffect();
          ps.finishBuzzerPlayed = true;
        }
        // Suppress the display-snap side effects while the power-confirm modal is
        // up (they would steal the frozen target / drop the modal), and while the
        // display is in a sleep-sticky screen (clock / off). In sleep-sticky the
        // finish screen is intentionally kept asleep (handleDisplayedPrinterFinishState),
        // so waking the backlight here would light a blank panel that nothing turns
        // back off - the "turn display off after finish" case, most visible when a
        // second printer finishes after the first already put the panel to sleep.
        // The one-shot alert + light + Tasmota bookkeeping below still run.
        if (getScreenState() != SCREEN_POWER_CONFIRM &&
            !isSleepStickyScreen(getScreenState())) {
          // A finish outranks the drying peek (#150): this branch moves
          // displayIndex, and leaving the peek up would render another
          // printer's drying data under it. Close it and let the state machine
          // land on the finish screen.
          if (getScreenState() == SCREEN_DRY_PEEK) closeDryPeek();
          if (rotState.displayIndex != i) {
            rotState.displayIndex = i;
            triggerDisplayTransition();
          }
          setBacklight(getEffectiveBrightness());
          rotState.displayHoldUntilMs = millis() + rotState.intervalMs;
        }
        scheduleLightOff(i, LIGHT_OFF_ON_FINISH);
      }
      if (isPrintingGcodeState(ps.gcodeStateId) &&
          !isPrintingGcodeState(prevGcodeStateId[i])) {
        ps.bedCooldownAlertArmed = false;
        ps.finishBuzzerPlayed = false;  // per-slot reset so a hidden printer's next finish still beeps
        if (printers[i].config.lightFlags & LIGHT_ON_AT_START)
          requestLightCommand(i, true);  // also cancels any pending auto-off
      }

      // Per-slot Tasmota print start/end edges — independent of which printer
      // is on screen, so dual-plug stats stay accurate even when displaying
      // the other printer.
      uint8_t plug = tasmotaPlugForPrinterSlot(i);
      if (plug != 0xFF) {
        bool wasPrinting = isPrintingGcodeState(prevGcodeStateId[i]);
        bool isPrinting  = isPrintingGcodeState(ps.gcodeStateId);
        if (isPrinting && !wasPrinting) {
          tasmotaMarkPrintStart(plug);
        }
        if (ps.gcodeStateId == GCODE_FINISH && prevGcodeStateId[i] != GCODE_FINISH) {
          tasmotaMarkPrintEnd(plug);
        }
      }
    }
    prevGcodeStateId[i] = ps.gcodeStateId;
    prevGcodeStateSeen[i] = true;
  }
}

static void handleBedCooldownBuzzers() {
  // Option disabled - clear all armed flags so re-enabling later doesn't
  // fire a stale alert (e.g. user toggles off, bed cools below threshold,
  // user toggles back on -> would otherwise trigger immediately).
  if (!buzzerSettings.bedCooldownAlert) {
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      printers[i].state.bedCooldownAlertArmed = false;
    }
    return;
  }
  if (buzzerIsPlaying()) return;  // wait for finish melody to clear

  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    BambuState& ps = printers[i].state;
    if (!ps.bedCooldownAlertArmed) continue;
    if (!ps.connected) continue;

    // Door-open ack per slot: user came to grab the print, cancel pending alert.
    if (dpSettings.doorAckEnabled && ps.doorSensorPresent && ps.doorOpen) {
      ps.bedCooldownAlertArmed = false;
      continue;
    }

    // Defensive: a new print is running, kill the armed state.
    if (isPrintingGcodeState(ps.gcodeStateId)) {
      ps.bedCooldownAlertArmed = false;
      continue;
    }

    if (ps.bedTemp <= 0.5f) continue;  // MQTT data sanity
    if (ps.bedTemp > (float)buzzerSettings.bedCooldownThresholdC) continue;

    buzzerPlay(BUZZ_BED_COOLDOWN);
    ps.bedCooldownAlertArmed = false;  // fire-and-forget (quiet hours = silent skip)
    break;  // one alert per loop tick
  }
}

// ---------------------------------------------------------------------------
//  Display rotation logic (multi-printer)
// ---------------------------------------------------------------------------
static bool slotListContains(const uint8_t slots[], uint8_t count, uint8_t slot) {
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i] == slot) return true;
  }
  return false;
}

static void rotateWithinSlots(const uint8_t slots[], uint8_t count, unsigned long now) {
  uint8_t current = rotState.displayIndex;
  for (uint8_t attempt = 1; attempt <= MAX_ACTIVE_PRINTERS; attempt++) {
    uint8_t next = (current + attempt) % MAX_ACTIVE_PRINTERS;
    for (uint8_t c = 0; c < count; c++) {
      if (slots[c] == next && next != current) {
        rotState.displayIndex = next;
        triggerDisplayTransition();
        rotState.lastRotateMs = now;
        return;
      }
    }
  }

  rotState.lastRotateMs = now;
}

static void handleRotation() {
  // Split view owns the screen while it is active - never rotate underneath it.
  if (getScreenState() == SCREEN_SPLIT) return;
  if (rotState.mode == ROTATE_OFF) return;
  if (getActiveConnCount() < 2) return;

  // Don't rotate when display is in clock, off, or finished state,
  // UNLESS a printer is actively printing or drying (wake up to show it)
  ScreenState scr = getScreenState();
  if (scr == SCREEN_CLOCK || scr == SCREEN_OFF || scr == SCREEN_FINISHED) {
    if (!anyPrinterPrinting() && !anyPrinterDrying()) return;
    // A printer became active - wake display and let rotation proceed
    setBacklight(getEffectiveBrightness());
  }

  unsigned long now = millis();

  // Keep a manually-peeked or freshly-finished slot on screen for one interval:
  // suppress both the Smart snap-to-active and within-active rotation.
  bool displayHold =
      rotState.displayHoldUntilMs != 0 &&
      (long)(rotState.displayHoldUntilMs - now) > 0;
  if (displayHold) return;

  // Gather candidates
  uint8_t connectedCandidates[MAX_ACTIVE_PRINTERS];
  uint8_t connectedCount = 0;
  uint8_t activeCandidates[MAX_ACTIVE_PRINTERS];
  uint8_t activeCount = 0;

  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    if (printers[i].state.connected) connectedCandidates[connectedCount++] = i;
    if (isPrinterActiveForDisplay(i)) activeCandidates[activeCount++] = i;
  }

  if (rotState.mode == ROTATE_SMART) {
    if (activeCount > 0) {
      // Active slots (printing or AMS drying) hide idle slots in Smart mode.
      // A single active printer snaps immediately, independent of the timer.
      if (activeCount == 1 ||
          !slotListContains(activeCandidates, activeCount, rotState.displayIndex)) {
        if (rotState.displayIndex != activeCandidates[0]) {
          rotState.displayIndex = activeCandidates[0];
          triggerDisplayTransition();
        }
        rotState.lastRotateMs = now;
        return;
      }

      if (now - rotState.lastRotateMs < rotState.intervalMs) return;
      rotateWithinSlots(activeCandidates, activeCount, now);
      return;
    }
  }

  if (connectedCount == 0) return;
  if (now - rotState.lastRotateMs < rotState.intervalMs) return;

  // No active Smart candidates, or ROTATE_AUTO: cycle connected printers.
  rotateWithinSlots(connectedCandidates, connectedCount, now);
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
  Battery::begin();
  splashEnd = millis() + 2000;
  startWiFiDuringSplash();
  setBacklight(brightness);
}

void loop() {
  if (handleSplashPhase()) {
    flushFrame();  // commit splash draws to panel (no-op on non-JC boards)
    return;
  }

  handleWiFi();
  handleWebServer();
  ssdpTick();  // closes SSDP scan sockets when the window elapses (no-op otherwise)
  handleBoardPowerOff();
  handleWakeButton();

  if (isWiFiConnected() && !isAPMode()) {
    updateDisplayedPrinterScreenState();
  }

  handleDisplaySleepTimeouts();
  handleConnectingScreenRecovery();
  handleGcodeStateTransitions();
  processLightTimers();
  handleBedCooldownBuzzers();

  buzzerTick();
  Battery::tick();
  // LED follows the panel into sleep: any path into SCREEN_OFF (after-finish
  // "turn display off", printer powered off) suspends the status LED; any wake
  // restores it. Synced on the boundary crossing only.
  {
    static bool ledSleepSynced = false;
    bool screenOff = (getScreenState() == SCREEN_OFF);
    if (screenOff != ledSleepSynced) {
      ledSleepSynced = screenOff;
      ledSetSuspended(screenOff);
    }
  }
  ledTick();
  checkNightMode();

#if defined(BOARD_HAS_CAMERA)
  // Camera (#120) lifecycle (cheap, non-blocking): open the 2nd TLS socket only
  // while the camera UI is on screen (fullscreen, or printing screen with a
  // visible camera tile) and the printer can stream; close it otherwise so heap
  // returns to baseline, or if rotation moved the displayed printer out from
  // under an open stream. The blocking socket work runs in the tail below.
  {
    bool wantCamera = cameraCanStreamDisplayedPrinter() &&
        (getScreenState() == SCREEN_CAMERA ||
         (getScreenState() == SCREEN_PRINTING && cameraDisplayedHasCameraTile()));
    if (cameraActive() && (!wantCamera || !cameraStreamingDisplayed())) cameraStop();
    if (wantCamera && !cameraActive())                                  cameraBegin();
  }
#endif

  updateDisplay();

  // MQTT and rotation after display update - TLS reconnect can block for
  // several seconds so we handle it last to keep UI responsive.
  // Skip during auto-OTA: that path already holds a TLS session to GitHub
  // and a concurrent second TLS session to Bambu Cloud is unsupported.
  if (isWiFiConnected() && !isAPMode() && isAnyPrinterConfigured() && !isOtaAutoInProgress()) {
    handleBambuMqtt();
    // Freeze auto-rotation while the camera fullscreen or the power-confirm modal
    // is up so the displayed printer (and its stream / target) does not drift.
    if (getScreenState() != SCREEN_CAMERA &&
        getScreenState() != SCREEN_POWER_CONFIRM &&
        getScreenState() != SCREEN_DRY_PEEK) handleRotation();
  }

  // Commit the framebuffer sprite to the panel. On JC3248W535 this is a
  // ~20ms QSPI push (300 KB @ 32MHz QIO); on all other boards it's a no-op
  // since draws go directly to the panel.
  flushFrame();

  // Plug power-confirm (#136): fire the blocking relay command only after the
  // "Sending..." frame is committed, and time out the result flash. Runs after
  // flushFrame() for the same reason cameraService() does.
  handlePowerConfirmService();

#if defined(BOARD_HAS_CAMERA)
  // Blocking camera socket work (connect can stall up to the TLS timeout) runs
  // dead last - AFTER flushFrame() - so on the framebuffer board (JC3248W535,
  // where draws are only visible once pushed) a stalled connect never delays the
  // already-rendered frame, only the start of the next loop.
  cameraService();
#endif
}
