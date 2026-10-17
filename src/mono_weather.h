// Mono weather: OpenWeatherMap polling for the SH1106 idle clock.
// Same provider and icons as the ESP-Spotify-OLED reference repo, same NTP
// clock as the rest of the firmware (no ezTime). Only compiled on
// BOARD_IS_MONO; other boards only carry the settings fields.
#pragma once

#include <Arduino.h>

#define MONO_WX_ICON_W 30
#define MONO_WX_ICON_H 30

// Poll cadence, overridable with -D (reference polls every 5 min; this repo
// settles for 15 min - weather barely moves faster than that).
#ifndef MONO_WEATHER_POLL_MS
#define MONO_WEATHER_POLL_MS (15UL * 60UL * 1000UL)
#endif

struct MonoWeatherData {
  bool ok;        // true once a fetch succeeded (or while a mock demo is set)
  float tempC;
  char icon[4];   // OpenWeatherMap code ("01d") + NUL; "" = none yet
};

// Call every mono tick while the idle clock is up. No-op when disabled,
// unconfigured, offline, in mock mode, or before the poll is due. When due,
// it snapshots key/query and spawns a one-shot "wx" FreeRTOS task (12 KB
// stack) so the TLS handshake never blocks loop(); only one fetch at a
// time, retried on the same cadence on failure.
void monoWeatherPoll();

// Last reading. Returns false when weather is disabled or nothing was ever
// fetched (caller then shows the error icon + "--").
bool monoWeatherGet(MonoWeatherData &out);

// Mock review helper ("mock w<code>" over Serial, "" clears): forces Get to
// report this icon with a fixed demo temperature, without network.
void monoWeatherSetDemo(const char *iconCode);

// Bitmap accessors (single copy of the icon data lives in mono_weather.cpp).
const unsigned char *monoWeatherIcon(const char *code);  // 30x30
const unsigned char *monoDegreeBmp();                    // 8x10
