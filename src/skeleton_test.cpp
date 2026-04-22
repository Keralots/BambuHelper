// Clean-slate JC3248W535 diagnostic using the moononournation/Arduino_GFX
// library's built-in Arduino_AXS15231B panel class and Arduino_ESP32QSPI
// databus. Per jc3248w535 skill lines 623-640, this is "production-grade"
// on this chip and requires no driver code.
//
// If this binary paints solid colors correctly, the hardware + toolchain are
// proven good and every previous symptom we chased was a bug in our driver.
// If it doesn't, something else is going on with this particular board.
//
// Pin map from skill (verified against vendor pincfg.h):
//   CS=45, SCK=47, D0=21, D1=48, D2=40, D3=39, BL=1
//
// Guarded so only the jc3248w535_skel env compiles this translation unit.
#ifdef BOARD_IS_JC3248W535_SKEL

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /*CS*/, 47 /*SCK*/, 21 /*D0*/, 48 /*D1*/, 40 /*D2*/, 39 /*D3*/);

// IPS=true sends INVON (0x21) during init, which double-inverts this panel
// (observed: every color appeared as its bitwise complement). Setting IPS=false
// skips INVON so colors display correctly.
static Arduino_GFX *gfx = new Arduino_AXS15231B(
    bus, -1 /*RST*/, 0 /*rotation*/, false /*IPS*/, 320, 480);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== AXS15231B Arduino_GFX baseline ===");

  // Backlight on (LEDC not strictly needed; simple GPIO-high works for test)
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);

  // 32 MHz pclk per skill's recommended starting point (skill line 42).
  if (!gfx->begin(32000000UL)) {
    Serial.println("gfx->begin() FAILED");
    return;
  }
  Serial.println("gfx begin OK");

  gfx->fillScreen(RED);     Serial.println("RED");    delay(1500);
  gfx->fillScreen(GREEN);   Serial.println("GREEN");  delay(1500);
  gfx->fillScreen(BLUE);    Serial.println("BLUE");   delay(1500);
  gfx->fillScreen(WHITE);   Serial.println("WHITE");  delay(1500);
  gfx->fillScreen(BLACK);   Serial.println("BLACK");  delay(1500);

  Serial.println("Diagnostic halted");
}

void loop() {
  delay(1000);
}

#endif  // BOARD_IS_JC3248W535_SKEL
