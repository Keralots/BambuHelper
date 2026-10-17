// Mono weather: OpenWeatherMap fetch + cache for the SH1106 idle clock.
// Mirrors the ESP-Spotify-OLED reference (getWeatherData/GET_Request) with
// ArduinoJson v7, which this firmware already links.
#ifdef BOARD_IS_MONO

#include "mono_weather.h"
#include "mono_weather_icons.h"
#include "settings.h"
#include "wifi_manager.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static MonoWeatherData s_wx = {false, 0, ""};
static unsigned long s_lastPollMs = 0;
static char s_demoIcon[4] = "";
static const float DEMO_TEMP_C = 21.0f;

// The fetch runs in a one-shot FreeRTOS task: a TLS handshake + GET can take
// anywhere from ~1 s to the 10 s timeout, and doing that inline in loop()
// froze MQTT keepalive and the web server while the idle clock was up.
// One task at a time is enforced with a busy flag, and the key/query strings
// are snapshotted so the web UI cannot change them mid-fetch.
static volatile bool s_fetchBusy = false;
static char s_fetchKey[65];
static char s_fetchQuery[64];
static TaskHandle_t s_fetchTask = nullptr;

static void urlEncodeAppend(String &dst, const char *src) {
  // RFC 3986 unreserved + comma (the City,CC separator) pass through;
  // everything else - spaces, UTF-8 city names - goes per byte as %XX.
  for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
    unsigned char c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ',') {
      dst += (char)c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", c);
      dst += b;
    }
  }
}

static bool fetchWeather() {
  String url = "https://api.openweathermap.org/data/2.5/weather?units=metric&q=";
  urlEncodeAppend(url, s_fetchQuery);
  url += "&APPID=";
  // Key is hex-only, sanitized on every load; append as-is.
  url += s_fetchKey;

  WiFiClientSecure client;
  client.setInsecure();  // same as the reference (HTTP_INSECURE 1)
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println(F("weather: begin failed"));
    return false;
  }
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      const char *icon = doc["weather"][0]["icon"];
      if (icon && icon[0] != '\0' && !doc["main"]["temp"].isNull()) {
        // Torn reads are benign: single writer (this task), value only draws.
        s_wx.tempC = doc["main"]["temp"].as<float>();
        strlcpy(s_wx.icon, icon, sizeof(s_wx.icon));
        s_wx.ok = true;
        ok = true;
        Serial.printf("weather: %.0fC %s\n", s_wx.tempC, s_wx.icon);
      } else {
        Serial.println(F("weather: missing fields"));
      }
    } else {
      Serial.println(F("weather: parse error"));
    }
  } else {
    Serial.printf("weather: HTTP %d\n", code);
  }
  http.end();
  return ok;
}

static void fetchTaskThunk(void *) {
  fetchWeather();
  s_fetchBusy = false;
  s_fetchTask = nullptr;
  vTaskDelete(nullptr);
}

void monoWeatherPoll() {
  if (!monoSettings.weatherEnabled) return;
  if (!isWiFiConnected() || isAPMode()) return;
  if (s_demoIcon[0] != '\0') return;  // mock demo short-circuits the network
  if (monoSettings.weatherKey[0] == '\0' || monoSettings.weatherQuery[0] == '\0')
    return;  // free check: don't even consume the cadence stamp
  unsigned long now = millis();
  if (now < 15000UL) return;  // let WiFi/MQTT/NTP settle after boot
  // Cadence applies between attempts, INCLUDING retries after a failure
  // (success or HTTP/TLS error retry at the same 15 min rhythm, never a hot
  // loop) - but s_lastPollMs == 0 means nothing was ever attempted yet, and
  // gating the first fetch a full period after boot left the clock on "--"
  // for 15 minutes every boot. First attempt goes right after the 15 s
  // settle; every attempt after that waits the cadence.
  if (s_lastPollMs != 0 && now - s_lastPollMs < (unsigned long)MONO_WEATHER_POLL_MS)
    return;
  if (s_fetchBusy) return;  // previous attempt still draining (up to 10 s)
  strlcpy(s_fetchKey, monoSettings.weatherKey, sizeof(s_fetchKey));
  strlcpy(s_fetchQuery, monoSettings.weatherQuery, sizeof(s_fetchQuery));
  s_lastPollMs = now;
  s_fetchBusy = true;
  // ~12 KB of stack for MbedTLS handshake + HTTPClient + ~1 KB JSON payload.
  if (xTaskCreate(fetchTaskThunk, "wx", 12288, nullptr,
                  1,  // loopTask prio: only CPU slices while crypto runs
                  &s_fetchTask) != pdPASS) {
    s_fetchBusy = false;
    Serial.println(F("weather: task spawn failed"));
    return;
  }
}

bool monoWeatherGet(MonoWeatherData &out) {
  if (!monoSettings.weatherEnabled) return false;
  if (s_demoIcon[0] != '\0') {
    out.ok = true;
    out.tempC = DEMO_TEMP_C;
    strlcpy(out.icon, s_demoIcon, sizeof(out.icon));
    return true;
  }
  if (!s_wx.ok) return false;
  out = s_wx;
  return true;
}

void monoWeatherSetDemo(const char *iconCode) {
  if (!iconCode || iconCode[0] == '\0') {
    s_demoIcon[0] = '\0';
    Serial.println(F("mock weather demo cleared"));
    return;
  }
  strlcpy(s_demoIcon, iconCode, sizeof(s_demoIcon));
  Serial.printf("mock weather demo: %s\n", s_demoIcon);
}

const unsigned char *monoWeatherIcon(const char *code) {
  return getWeatherIcon(code ? code : "");
}

const unsigned char *monoDegreeBmp() { return degree_bmp; }

#endif  // BOARD_IS_MONO
