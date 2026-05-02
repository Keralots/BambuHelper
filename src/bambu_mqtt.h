#ifndef BAMBU_MQTT_H
#define BAMBU_MQTT_H

#include <Arduino.h>
#include "config.h"

// Connection diagnostics exposed for display/web
struct MqttDiag {
  int      lastRc;          // PubSubClient state code from last attempt
  uint32_t attempts;        // total reconnect attempts since boot
  uint32_t messagesRx;      // total MQTT messages received
  uint32_t freeHeap;        // heap at last attempt
  uint32_t pushallTotal;    // all pushall requests sent since boot
  uint16_t recoveryPrint;    // recovery: core print data stale
  uint16_t recoveryConnDead; // recovery: connection dead during print
  uint16_t recoveryFinish;   // recovery: stale FINISH state
  uint16_t recoveryIdle;     // recovery: stale idle / UNKNOWN bootstrap
  uint16_t recoveryFailed;   // recovery: stuck in FAILED on cloud
  bool     tcpOk;           // last TCP reachability result
  unsigned long lastAttemptMs; // millis() of last attempt
  unsigned long connectDurMs;  // how long last connect() took
  unsigned long lastPushallMs; // millis() of last pushall request
  uint8_t  lastPushallReason;  // PushallReason code of the last request
};

enum PushallReason : uint8_t {
  PUSHALL_NONE = 0,
  PUSHALL_INITIAL,
  PUSHALL_RETRY_NO_DATA,
  PUSHALL_PERIODIC,
  PUSHALL_RECOVERY_PRINT,
  PUSHALL_RECOVERY_CONN_DEAD,
  PUSHALL_RECOVERY_FINISH,
  PUSHALL_RECOVERY_IDLE,
  PUSHALL_RECOVERY_FAILED,
  PUSHALL_MANUAL
};

extern bool mqttDebugLog;   // verbose Serial logging (toggled via web)

void initBambuMqtt();
void handleBambuMqtt();
void disconnectBambuMqtt();              // disconnect all connections
void disconnectBambuMqtt(uint8_t slot);  // disconnect specific slot

bool isPrinterConfigured(uint8_t slot);
bool isAnyPrinterConfigured();
uint8_t getActiveConnCount();            // how many connections are live
const MqttDiag& getMqttDiag(uint8_t slot = 0);
const char* pushallReasonToString(uint8_t reason);

void resetMqttBackoff();                 // reset backoff + force immediate reconnect
void deferMqttReconnect();               // skip reconnect attempts for one iteration
void requestCloudRefresh(uint8_t slot);  // manual pushall for cloud UNKNOWN/FAILED state (debounced)

// Human-readable error string for PubSubClient rc
const char* mqttRcToString(int rc);

#endif // BAMBU_MQTT_H
