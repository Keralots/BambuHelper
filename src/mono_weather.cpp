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

static MonoWeatherData s_wx = {false, 0, ""};
static unsigned long s_lastPollMs = 0;
static char s_demoIcon[4] = "";
static const float DEMO_TEMP_C = 21.0f;

static bool fetchWeather() {
  if (monoSettings.weatherKey[0] == '\0' || monoSettings.weatherQuery[0] == '\0')
    return false;
  String url = "https://api.openweathermap.org/data/2.5/weather?units=metric&q=";
  url += monoSettings.weatherQuery;
  url += "&APPID=";
  url += monoSettings.weatherKey;

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

void monoWeatherPoll() {
  if (!monoSettings.weatherEnabled) return;
  if (!isWiFiConnected() || isAPMode()) return;
  if (s_demoIcon[0] != '\0') return;  // mock demo short-circuits the network
  unsigned long now = millis();
  if (now < 15000UL) return;  // let WiFi/MQTT/NTP settle after boot
  if (s_wx.ok && now - s_lastPollMs < (unsigned long)MONO_WEATHER_POLL_MS) return;
  s_lastPollMs = now;  // stamp even on failure: polite retry, not a hot loop
  fetchWeather();
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
