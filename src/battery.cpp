#include "battery.h"

#if defined(BOARD_HAS_BATTERY)

#include <Arduino.h>

#if defined(BOARD_HAS_BAT_AXP2101)
  #include <Wire.h>
  #include <XPowersLib.h>
  static XPowersAXP2101 s_pmu;
#endif

namespace {

bool   s_present       = false;
float  s_voltageEma    = 0.0f;
uint8_t s_percent      = 0;
bool   s_charging      = false;
unsigned long s_lastTickMs = 0;

constexpr unsigned long TICK_INTERVAL_MS = 5000;

#if defined(BOARD_HAS_BAT_ADC)
constexpr float V_REF      = 3.3f;
constexpr float ADC_FS     = 4096.0f;
#ifndef BAT_VOLT_DIVIDER
#define BAT_VOLT_DIVIDER 3.0f
#endif
constexpr float V_MIN_PRESENT = 2.5f;

float readVoltageOnceADC() {
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) {
    acc += analogRead(BAT_ADC_PIN);
    delay(2);
  }
  float raw = acc / 8.0f;
  return (raw / ADC_FS) * V_REF * BAT_VOLT_DIVIDER;
}
#endif

uint8_t voltageToPercent(float v) {
  // Linear Li-ion approximation: 3.3V -> 0%, 4.2V -> 100%.
  if (v <= 3.30f) return 0;
  if (v >= 4.20f) return 100;
  return (uint8_t)(((v - 3.30f) / (4.20f - 3.30f)) * 100.0f + 0.5f);
}

}

namespace Battery {

void begin() {
  s_lastTickMs = 0;

#if defined(BOARD_HAS_BAT_ADC)
  pinMode(BAT_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  delay(20);
  float vSum = 0.0f;
  for (int i = 0; i < 10; i++) {
    vSum += readVoltageOnceADC();
    delay(10);
  }
  float v = vSum / 10.0f;
  if (v >= V_MIN_PRESENT) {
    s_present = true;
    s_voltageEma = v;
    s_percent = voltageToPercent(v);
  } else {
    s_present = false;
  }
#endif

#if defined(BOARD_HAS_BAT_AXP2101)
  if (!s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, AXP2101_I2C_SDA, AXP2101_I2C_SCL)) {
    s_present = false;
    return;
  }
  s_pmu.disableTSPinMeasure();
  s_pmu.enableBattDetection();
  s_pmu.enableVbusVoltageMeasure();
  s_pmu.enableBattVoltageMeasure();
  s_pmu.enableSystemVoltageMeasure();
  delay(50);
  s_present = s_pmu.isBatteryConnect();
  if (s_present) {
    s_voltageEma = s_pmu.getBattVoltage() / 1000.0f;
    int pct = s_pmu.getBatteryPercent();
    s_percent = (pct < 0) ? 0 : (pct > 100 ? 100 : (uint8_t)pct);
    s_charging = s_pmu.isCharging();
  }
#endif
}

void tick() {
  if (!s_present) return;
  unsigned long now = millis();
  if (s_lastTickMs != 0 && (now - s_lastTickMs) < TICK_INTERVAL_MS) return;
  s_lastTickMs = now;

#if defined(BOARD_HAS_BAT_ADC)
  float v = readVoltageOnceADC();
  s_voltageEma = s_voltageEma * 0.8f + v * 0.2f;
  s_percent = voltageToPercent(s_voltageEma);
#endif

#if defined(BOARD_HAS_BAT_AXP2101)
  s_voltageEma = s_pmu.getBattVoltage() / 1000.0f;
  int pct = s_pmu.getBatteryPercent();
  if (pct >= 0 && pct <= 100) s_percent = (uint8_t)pct;
  s_charging = s_pmu.isCharging();
#endif
}

bool isPresent()  { return s_present; }
uint8_t percent() { return s_percent; }
float voltage()   { return s_voltageEma; }
bool isCharging() { return s_charging; }
bool isLow()      { return s_present && s_percent < 20; }
bool isCritical() { return s_present && s_percent < 10; }

}

#endif
