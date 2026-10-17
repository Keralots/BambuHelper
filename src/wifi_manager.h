#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

void startWiFiDuringSplash();
void initWiFi();
void handleWiFi();
bool isWiFiConnected();
bool isAPMode();
String getAPSSID();

// Why the stored network could not be joined, worked out (disconnect reasons +
// a scan) when the device falls back to AP mode. Shown on the AP screen and on
// the AP portal page so the user can fix it without a serial log.
enum WifiFailKind : uint8_t {
  WIFI_FAIL_NONE = 0,
  WIFI_FAIL_NOT_FOUND,      // SSID not in the scan (wrong name, 5 GHz only, out of range)
  WIFI_FAIL_NAME_MISMATCH,  // a network with a case/space-different name is nearby
  WIFI_FAIL_WEAK,           // found, but too far away to hold a connection
  WIFI_FAIL_PASS_SHORT,     // stored password is 1-7 chars, invalid for WPA2
  WIFI_FAIL_BAD_PASS,       // found with usable signal, handshake rejected
  WIFI_FAIL_ENTERPRISE,     // WPA2-Enterprise network, not supported
  WIFI_FAIL_NO_IP,          // joined, but DHCP never handed out an address
  WIFI_FAIL_OTHER,
};

struct WifiFailInfo {
  WifiFailKind kind;
  int8_t rssi;              // best RSSI of the stored SSID, 0 = not seen
  uint8_t channel;
  uint8_t reason;           // last STA disconnect reason (wifi_err_reason_t)
  char nearName[33];        // WIFI_FAIL_NAME_MISMATCH: the name actually seen
};

const WifiFailInfo& getWifiFailInfo();
const char* wifiFailKindName(WifiFailKind k);

#endif // WIFI_MANAGER_H
