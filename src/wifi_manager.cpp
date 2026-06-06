#include "wifi_manager.h"
#include "settings.h"
#include "display_ui.h"
#include "config.h"
#include "improv_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

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
static bool splashStaStarted = false;
static unsigned long splashStaStartMs = 0;

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
  MDNS.end();  // tear down any responder bound to the now-dead STA interface

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

static void applyStaticNetworkConfig() {
  if (netSettings.useDHCP || netSettings.staticIP[0] == '\0') return;

  IPAddress ip, gw, sn, dns;
  bool ipOk = ip.fromString(netSettings.staticIP);
  bool gwOk = gw.fromString(netSettings.gateway);
  bool snOk = sn.fromString(netSettings.subnet);

  if (!ipOk || !gwOk || !snOk) {
    Serial.printf("Static IP config invalid, falling back to DHCP (ip=%d gw=%d sn=%d)\n",
                  (int)ipOk, (int)gwOk, (int)snOk);
    return;
  }

  bool customDns = (netSettings.dns[0] != '\0');
  if (customDns) {
    if (!dns.fromString(netSettings.dns)) {
      dns = gw;
      Serial.printf("Static IP: invalid DNS '%s', falling back to gateway %s\n",
                    netSettings.dns, netSettings.gateway);
    }
  } else {
    dns = gw;
  }

  String dnsStr = dns.toString();
  bool applied = WiFi.config(ip, gw, sn, dns);
  if (applied) {
    Serial.printf("Static IP: %s GW: %s SN: %s DNS: %s\n",
                  netSettings.staticIP, netSettings.gateway,
                  netSettings.subnet, dnsStr.c_str());
  } else {
    Serial.printf("Static IP: WiFi.config() failed for IP=%s GW=%s SN=%s DNS=%s\n",
                  netSettings.staticIP, netSettings.gateway,
                  netSettings.subnet, dnsStr.c_str());
  }
}

// Start (or restart) the mDNS responder so the device is reachable at
// <hostname>.local. No-op unless the user enabled it. Safe to call repeatedly:
// MDNS.end() first lets us re-announce after a WiFi drop/reconnect, which the
// ESP32 responder does not always recover on its own.
static void startMDNS() {
  if (!netSettings.mdnsEnabled || netSettings.hostname[0] == '\0') return;

  // The web UI validates too, but never trust stored input.
  char host[32];
  sanitizeHostname(netSettings.hostname, host, sizeof(host));

  MDNS.end();
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started: http://%s.local\n", host);
  } else {
    Serial.println("mDNS start failed");
  }
}

static void completeWiFiStartup() {
  Serial.printf("WiFi connected! IP: %s\n",
                WiFi.localIP().toString().c_str());
  stopAP();
  apMode = false;
  disconnectTime = 0;
  reconnectAttempts = 0;
  splashStaStarted = false;

  // Sync time via NTP with automatic DST
  configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
  Serial.printf("NTP configured: %s\n", netSettings.timezoneStr);

  startMDNS();

  // Show IP screen for 1.5 seconds if enabled
  if (netSettings.showIPAtStartup) {
    setScreenState(SCREEN_WIFI_CONNECTED);
    unsigned long ipStart = millis();
    while (millis() - ipStart < 1500) {
      updateDisplay();
      flushFrame();  // JC3248W535: push sprite to panel during blocking loop
      delay(50);
    }
  }
}

static void beginStaConnectAttempt() {
  WiFi.mode(WIFI_STA);
  applyStaticNetworkConfig();
  WiFi.begin(wifiSSID, wifiPass);
}

void startWiFiDuringSplash() {
  if (strlen(wifiSSID) == 0) return;

  Serial.printf("WiFi: starting background connect during splash: %s\n", wifiSSID);
  beginStaConnectAttempt();
  splashStaStarted = true;
  splashStaStartMs = millis();
}

void initWiFi() {
  // If we have stored credentials, try STA mode
  if (strlen(wifiSSID) > 0) {
    setScreenState(SCREEN_CONNECTING_WIFI);

    if (splashStaStarted && WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected during splash");
      completeWiFiStartup();
      return;
    }

    for (int attempt = 1; attempt <= 3; attempt++) {
      unsigned long start = millis();

      if (attempt == 1 && splashStaStarted) {
        Serial.printf("Connecting to WiFi: %s (attempt 1/3, continued after splash)\n", wifiSSID);
        start = splashStaStartMs;
      } else {
        Serial.printf("Connecting to WiFi: %s (attempt %d/3)\n", wifiSSID, attempt);
        beginStaConnectAttempt();
      }

      while (WiFi.status() != WL_CONNECTED &&
             millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(100);
        updateDisplay();
        flushFrame();  // JC3248W535: push sprite to panel during blocking loop
      }
      if (WiFi.status() == WL_CONNECTED) break;

      Serial.println("WiFi attempt failed, retrying...");
      WiFi.disconnect(true);
      splashStaStarted = false;
      delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED) {
      completeWiFiStartup();
      return;
    }

    splashStaStarted = false;
    Serial.println("WiFi connection failed, starting AP");
  }

  // No credentials or connection failed — bring AP up immediately so the
  // captive portal is reachable without any delay. This matches the legacy
  // behaviour exactly; web-flasher / Improv users get an additional path
  // in parallel (see below) without anyone having to wait for a timeout.
  startAP();

  // For genuinely fresh devices (no SSID stored at all), also open an
  // Improv-Serial listening window in the background. Pumped from
  // handleWiFi() while AP is up, so it costs nothing if no browser is
  // listening; if the user did flash via the web flasher, ESP Web Tools
  // probes Improv ~15s after the install and presents its "Configure
  // WiFi" dialog. Skipped when the user just failed to connect with
  // stored credentials - they need the AP to fix what they typed, not a
  // browser dialog over a port that may not even be connected anymore.
  if (strlen(wifiSSID) == 0) {
    improvSetupBegin(IMPROV_SETUP_WINDOW_MS);
  }
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

    // Pump the Improv-Serial listener if its setup window is still open.
    // On success the library has already saved credentials AND established
    // STA via our custom connect callback; restart so the device boots
    // cleanly into the configured WiFi without the dual-mode hangover.
    if (improvSetupTick()) {
      Serial.println("Improv: credentials received, restarting");
      Serial.flush();
      delay(200);  // let any pending serial response reach the browser
      ESP.restart();
    }
    if (improvSetupExpired()) {
      improvSetupEnd();
    }

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
        startMDNS();
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
      startMDNS();  // re-announce; responder may not survive a link drop
    }
    disconnectTime = 0;
    reconnectAttempts = 0;
    lastReconnectAttempt = 0;
    phase3StartTime = 0;
  }
}
