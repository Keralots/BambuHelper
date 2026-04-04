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
  bool     tcpOk;           // last TCP reachability result
  unsigned long lastAttemptMs; // millis() of last attempt
  unsigned long connectDurMs;  // how long last connect() took
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

void resetMqttBackoff();                 // reset backoff + force immediate reconnect
void requestCloudRefresh(uint8_t slot);  // manual pushall for cloud UNKNOWN state (debounced)

// Human-readable error string for PubSubClient rc
const char* mqttRcToString(int rc);

#endif // BAMBU_MQTT_H
