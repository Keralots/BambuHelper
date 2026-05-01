#include "battery.h"

#if defined(BOARD_HAS_BATTERY)

#include <Arduino.h>

#if defined(BOARD_HAS_BAT_AXP2101)
  #include <Wire.h>
  #include <XPowersLib.h>
  static XPowersAXP2101 s_pmu;
  // Diagnostic state captured at begin() so it can be re-printed during tick()
  // (early-boot Serial logs are sometimes lost before USB CDC is enumerated).
  static bool    s_pmuBeginOk = false;
  static uint8_t s_pmuChipId  = 0;
  static uint8_t s_pmuI2cAck  = 0xFF;   // result of Wire.endTransmission to 0x34
  static bool    s_pmuDiagPrinted = false;
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
// Hysteresis: enter present at >= 3.0V (above realistic Li-ion BMS cutoff),
// drop at < 2.8V to avoid flicker, sanity-cap at 4.8V to reject noise/leakage
// readings on a floating ADC pin (charging Li-ion + USB rail can show ~4.3-4.5V).
constexpr float V_MIN_PRESENT  = 3.0f;
constexpr float V_DROP_PRESENT = 2.8f;
constexpr float V_MAX_PRESENT  = 4.8f;

float readVoltageOnceADC() {
  // Use the ESP-IDF eFuse-calibrated ADC reader: it returns linearised
  // pin millivolts that account for ESP32-S3's nonlinearity above ~1.1V
  // at 11dB attenuation, which our naive raw->volts math gets wrong.
  uint32_t mvSum = 0;
  for (int i = 0; i < 8; i++) {
    mvSum += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }
  float vPin = (mvSum / 8.0f) / 1000.0f;
  return vPin * BAT_VOLT_DIVIDER;
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
  uint32_t rawSum = 0, mvSum = 0;
  float vSum = 0.0f;
  for (int i = 0; i < 10; i++) {
    rawSum += analogRead(BAT_ADC_PIN);
    mvSum  += analogReadMilliVolts(BAT_ADC_PIN);
    vSum   += readVoltageOnceADC();
    delay(10);
  }
  float v = vSum / 10.0f;
  uint16_t rawAvg = rawSum / 10;
  uint32_t mvAvg  = mvSum  / 10;
  if (v >= V_MIN_PRESENT && v <= V_MAX_PRESENT) {
    s_present = true;
    s_voltageEma = v;
    s_percent = voltageToPercent(v);
  } else {
    s_present = false;
  }
  Serial.printf("[BAT] ADC pin=%d rawAvg=%u Vpin=%umV Vbat=%.3fV (divider=%.2fx) present=%d pct=%u\n",
                BAT_ADC_PIN, rawAvg, (unsigned)mvAvg, v, (float)BAT_VOLT_DIVIDER, s_present, s_percent);
#endif

#if defined(BOARD_HAS_BAT_AXP2101)
  // Bring up the shared I2C bus explicitly; ES8311 audio and CST816 touch
  // also call Wire.begin(42,41) and the order between drivers isn't fixed.
  Wire.begin(AXP2101_I2C_SDA, AXP2101_I2C_SCL);
  delay(10);

  // Direct I2C probe to verify the chip ACKs at AXP2101_SLAVE_ADDRESS (0x34).
  // Stored so tick() can re-print after USB CDC is fully enumerated.
  Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
  s_pmuI2cAck = Wire.endTransmission();   // 0=ACK, 2=NACK on address, 4=other

  s_pmuBeginOk = s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, AXP2101_I2C_SDA, AXP2101_I2C_SCL);
  s_pmuChipId  = s_pmuBeginOk ? s_pmu.getChipID() : 0;
  Serial.printf("[BAT] AXP probeAck=%u begin=%d chipID=0x%02X\n",
                s_pmuI2cAck, s_pmuBeginOk, s_pmuChipId);
  if (!s_pmuBeginOk) {
    s_present = false;
    return;
  }
  s_pmu.disableTSPinMeasure();
  s_pmu.enableBattDetection();
  s_pmu.enableVbusVoltageMeasure();
  s_pmu.enableBattVoltageMeasure();
  s_pmu.enableSystemVoltageMeasure();
  delay(200);
  s_present = s_pmu.isBatteryConnect();
  if (s_present) {
    s_voltageEma = s_pmu.getBattVoltage() / 1000.0f;
    int pct = s_pmu.getBatteryPercent();
    s_percent = (pct < 0) ? 0 : (pct > 100 ? 100 : (uint8_t)pct);
    s_charging = s_pmu.isCharging();
  }
  Serial.printf("[BAT] isBat=%d Vbat=%dmV Vbus=%dmV vbusIn=%d pct=%d charging=%d\n",
                s_pmu.isBatteryConnect(), s_pmu.getBattVoltage(), s_pmu.getVbusVoltage(),
                s_pmu.isVbusIn(), s_pmu.getBatteryPercent(), s_pmu.isCharging());
#endif
}

void tick() {
  unsigned long now = millis();
  if (s_lastTickMs != 0 && (now - s_lastTickMs) < TICK_INTERVAL_MS) return;
  s_lastTickMs = now;

#if defined(BOARD_HAS_BAT_ADC)
  float v = readVoltageOnceADC();
  // Hysteresis: only enter present at >= V_MIN_PRESENT, only leave at < V_DROP_PRESENT.
  // Sanity-cap at V_MAX_PRESENT to reject noise on a floating pin.
  bool inRange = (v >= (s_present ? V_DROP_PRESENT : V_MIN_PRESENT)) && (v <= V_MAX_PRESENT);
  if (inRange) {
    if (!s_present) { s_voltageEma = v; s_present = true; }
    else            { s_voltageEma = s_voltageEma * 0.8f + v * 0.2f; }
    s_percent = voltageToPercent(s_voltageEma);
  } else if (s_present) {
    s_present = false;
    s_voltageEma = 0.0f;
    s_percent = 0;
    s_charging = false;
  }
#endif

#if defined(BOARD_HAS_BAT_AXP2101)
  bool present = s_pmu.isBatteryConnect();
  static uint8_t debugTicks = 0;
  if (debugTicks < 3) {
    if (!s_pmuDiagPrinted) {
      // One-time replay of begin() diagnostics + full I2C bus scan so we can
      // tell from a single tick log whether the chip ACKs at 0x34 at all.
      s_pmuDiagPrinted = true;
      Serial.printf("[BAT] (begin) probeAck=%u begin=%d chipID=0x%02X\n",
                    s_pmuI2cAck, s_pmuBeginOk, s_pmuChipId);
      Serial.print("[BAT] I2C scan:");
      for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
      }
      Serial.println();
    }
    Serial.printf("[BAT] tick%d isBat=%d Vbat=%dmV Vbus=%dmV vbusIn=%d pct=%d charging=%d\n",
                  debugTicks, present, s_pmu.getBattVoltage(), s_pmu.getVbusVoltage(),
                  s_pmu.isVbusIn(), s_pmu.getBatteryPercent(), s_pmu.isCharging());
    debugTicks++;
  }
  if (present) {
    s_voltageEma = s_pmu.getBattVoltage() / 1000.0f;
    int pct = s_pmu.getBatteryPercent();
    if (pct >= 0 && pct <= 100) s_percent = (uint8_t)pct;
    s_charging = s_pmu.isCharging();
    s_present = true;
  } else if (s_present) {
    s_present = false;
    s_voltageEma = 0.0f;
    s_percent = 0;
    s_charging = false;
  }
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
