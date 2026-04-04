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
static unsigned long lastStaProbe = 0;
static unsigned long probeStartTime = 0;
static bool staProbing = false;
static unsigned long phase3StartTime = 0;
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

static void stopAP() {
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
}

static void startAP() {
  // Build SSID from MAC
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char ssidBuf[32];
  snprintf(ssidBuf, sizeof(ssidBuf), "%s%04X", WIFI_AP_PREFIX, mac);
  apSSID = ssidBuf;

  // Use AP+STA so we can probe STA while serving the config portal
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssidBuf, WIFI_AP_PASSWORD);

  // Captive portal: redirect all DNS to our IP
  if (!dnsServer) {
    dnsServer = new DNSServer();
  }
  dnsServer->start(53, "*", WiFi.softAPIP());

  apMode = true;
  disconnectTime = 0;
  lastStaProbe = millis();
  staProbing = false;

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

    setScreenState(SCREEN_CONNECTING_WIFI);

    for (int attempt = 1; attempt <= 3; attempt++) {
      Serial.printf("Connecting to WiFi: %s (attempt %d/3)\n", wifiSSID, attempt);
      WiFi.begin(wifiSSID, wifiPass);

      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED &&
             millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(100);
        updateDisplay();
      }
      if (WiFi.status() == WL_CONNECTED) break;

      Serial.println("WiFi attempt failed, retrying...");
      WiFi.disconnect(true);
      delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected! IP: %s\n",
                    WiFi.localIP().toString().c_str());
      stopAP();
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

// Return the reconnect interval for the current attempt count.
static unsigned long reconnectInterval() {
  if (reconnectAttempts < WIFI_BACKOFF_PHASE2_START) {
    return WIFI_BACKOFF_PHASE1_MS;      // phase 1: 10s
  }
  if (reconnectAttempts < WIFI_BACKOFF_PHASE3_START) {
    return WIFI_BACKOFF_PHASE2_MS;      // phase 2: 30s
  }
  return WIFI_BACKOFF_PHASE3_MS;        // phase 3: 60s, indefinite
}

void handleWiFi() {
  if (apMode) {
    if (dnsServer) dnsServer->processNextRequest();

    // Periodically probe STA to recover without user interaction
    unsigned long now = millis();
    if (!staProbing && now - lastStaProbe >= WIFI_STA_PROBE_INTERVAL) {
      // Start a non-blocking STA probe; result checked after WIFI_STA_PROBE_CHECK_MS
      Serial.println("AP mode: probing STA connection...");
      WiFi.begin(wifiSSID, wifiPass);
      staProbing = true;
      probeStartTime = now;
    } else if (staProbing && now - probeStartTime >= WIFI_STA_PROBE_CHECK_MS) {
      // Check result of the in-flight probe
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("STA probe succeeded, IP: %s — leaving AP mode\n",
                      WiFi.localIP().toString().c_str());
        stopAP();
        WiFi.mode(WIFI_STA);
        apMode = false;
        staProbing = false;
        disconnectTime = 0;
        reconnectAttempts = 0;
        lastReconnectAttempt = 0;
        phase3StartTime = 0;

        configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
        setScreenState(SCREEN_WIFI_CONNECTED);
      } else {
        Serial.println("STA probe failed, staying in AP mode");
        WiFi.disconnect(false);
        staProbing = false;
        lastStaProbe = now;  // reset the 2-min cycle
      }
    }
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

    // Try to reconnect using the current backoff interval (non-blocking)
    if (lastReconnectAttempt == 0 ||
        millis() - lastReconnectAttempt > reconnectInterval()) {
      lastReconnectAttempt = millis();
      reconnectAttempts++;

      const char* phase = reconnectAttempts <= WIFI_BACKOFF_PHASE2_START ? "phase1"
                        : reconnectAttempts <= WIFI_BACKOFF_PHASE3_START ? "phase2"
                        : "phase3";
      Serial.printf("WiFi reconnect attempt %d (%s, interval %lus)\n",
                    reconnectAttempts, phase, reconnectInterval() / 1000UL);

      // Record when we first enter phase 3
      if (reconnectAttempts == WIFI_BACKOFF_PHASE3_START) {
        phase3StartTime = millis();
      }

      // After phase 3 has run for WIFI_AP_FALLBACK_MS, fall back to AP so
      // the user can reconfigure the device if credentials have changed
      if (reconnectAttempts > WIFI_BACKOFF_PHASE3_START &&
          phase3StartTime > 0 &&
          millis() - phase3StartTime >= WIFI_AP_FALLBACK_MS) {
        Serial.println("Phase 3 timeout — falling back to AP mode");
        startAP();
        return;
      }

      WiFi.disconnect();
      WiFi.begin(wifiSSID, wifiPass);
    }
  } else {
    if (disconnectTime > 0) {
      Serial.println("WiFi reconnected!");
    }
    disconnectTime = 0;
    reconnectAttempts = 0;
    lastReconnectAttempt = 0;
    phase3StartTime = 0;
  }
}
