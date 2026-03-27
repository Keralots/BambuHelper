#include "wifi_manager.h"
#include "settings.h"
#include "display_ui.h"
#include "config.h"
#include <WiFi.h>
#include <DNSServer.h>

static bool apMode = false;
static DNSServer* dnsServer = nullptr;
static unsigned long disconnectTime = 0;
static unsigned long lastReconnectAttempt = 0;
static uint8_t reconnectAttempts = 0;
static const uint8_t MAX_RECONNECT_ATTEMPTS = 5;
static const unsigned long RECONNECT_INTERVAL = 10000;  // 10s between attempts
static String apSSID;

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool isAPMode() {
  return apMode;
}

String getAPSSID() {
  return apSSID;
}

static void startAP() {
  // Build SSID from MAC
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char ssidBuf[32];
  snprintf(ssidBuf, sizeof(ssidBuf), "%s%04X", WIFI_AP_PREFIX, mac);
  apSSID = ssidBuf;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidBuf, WIFI_AP_PASSWORD);

  // Captive portal: redirect all DNS to our IP
  if (!dnsServer) {
    dnsServer = new DNSServer();
  }
  dnsServer->start(53, "*", WiFi.softAPIP());

  apMode = true;
  disconnectTime = 0;

  Serial.printf("AP started: %s (IP: %s)\n", ssidBuf,
                WiFi.softAPIP().toString().c_str());
  setScreenState(SCREEN_AP_MODE);
}

void initWiFi() {
  // If we have stored credentials, try STA mode
  if (strlen(wifiSSID) > 0) {
    WiFi.mode(WIFI_STA);

    // Apply static IP if configured
    if (!netSettings.useDHCP && netSettings.staticIP[0] != '\0') {
      IPAddress ip, gw, sn, dns;
      if (ip.fromString(netSettings.staticIP) &&
          gw.fromString(netSettings.gateway) &&
          sn.fromString(netSettings.subnet)) {
        if (netSettings.dns[0] != '\0') dns.fromString(netSettings.dns);
        else dns = gw;
        WiFi.config(ip, gw, sn, dns);
        Serial.printf("Static IP: %s GW: %s\n", netSettings.staticIP, netSettings.gateway);
      }
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
            millis() - start < WIFI_RECONNECT_TIMEOUT) {
      unsigned long startAttempt = millis();
      WiFi.begin(wifiSSID, wifiPass);
      setScreenState(SCREEN_CONNECTING_WIFI);

      Serial.printf("Connecting to WiFi: %s\n", wifiSSID);
      while (WiFi.status() != WL_CONNECTED &&
            millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {
        delay(100);
        updateDisplay();
      }

      Serial.println("Retrying WiFi connection");
      delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected! IP: %s\n",
                    WiFi.localIP().toString().c_str());
      apMode = false;
      disconnectTime = 0;

      // Sync time via NTP with automatic DST
      configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
      Serial.printf("NTP configured: %s\n", netSettings.timezoneStr);

      // Show IP screen for 3 seconds if enabled
      if (netSettings.showIPAtStartup) {
        setScreenState(SCREEN_WIFI_CONNECTED);
        unsigned long ipStart = millis();
        while (millis() - ipStart < 3000) {
          updateDisplay();
          delay(50);
        }
      }
      return;
    }

    Serial.println("WiFi connection failed, starting AP");
  }

  // No credentials or connection failed — start AP
  startAP();
}

void handleWiFi() {
  if (apMode) {
    if (dnsServer) dnsServer->processNextRequest();
    return;
  }

  // In STA mode: check for disconnection
  if (WiFi.status() != WL_CONNECTED) {
    if (disconnectTime == 0) {
      disconnectTime = millis();
      lastReconnectAttempt = 0;
      reconnectAttempts = 0;
      Serial.println("WiFi disconnected, will try to reconnect...");
    }

    // Actively try to reconnect at intervals
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      reconnectAttempts++;
      Serial.printf("WiFi reconnect attempt %d/%d\n", reconnectAttempts, MAX_RECONNECT_ATTEMPTS);
      WiFi.disconnect();
      WiFi.begin(wifiSSID, wifiPass);
    }

    // Only fall back to AP after all attempts exhausted
    if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
      Serial.println("WiFi reconnect failed, switching to AP mode");
      startAP();
    }
  } else {
    if (disconnectTime > 0) {
      Serial.println("WiFi reconnected!");
    }
    disconnectTime = 0;
    reconnectAttempts = 0;
  }
}
