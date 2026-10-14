// BambuHelper-Mono: SH1106 128x64 por I2C con Adafruit GFX/SH110X.
// Inicialización y estilo copiados del repo de referencia ESP-Spotify-OLED:
// Adafruit_SH1106G 128x64 en 0x3c, Wire.begin(SDA,SCL), begin(addr,true),
// texto blanco, clearDisplay()+display() por refresco, barra con
// drawRect+fillRect y nombres transliterados (la GFX no tiene acentos).
#ifdef BOARD_IS_MONO

#include "mono_display.h"
#include "bambu_state.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#include <stdio.h>

#ifndef OLED_SDA
#define OLED_SDA 8
#endif
#ifndef OLED_SCL
#define OLED_SCL 9
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3c
#endif
#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif
#ifndef OLED_RESET
#define OLED_RESET -1
#endif

static Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Sin acentos: la fuente GFX no los trae (igual que replaceSpecialCharacters
// del repo de referencia). Solo lo necesario para nombres de fichero Bambu.
static void deaccent(char *dst, size_t n, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j + 1 < n;) {
    unsigned char c = (unsigned char)src[i];
    if (c < 0x80) {
      dst[j++] = (char)c;
      i++;
    } else if (c == 0xC3 && src[i + 1]) {
      // U+00C0..U+00FF en UTF-8: vocales acentuadas y enie -> ASCII base.
      unsigned char d = (unsigned char)src[i + 1];
      char r = 0;
      if (d >= 0x80 && d <= 0x85) r = 'A';
      else if (d == 0x87) r = 'C';
      else if (d >= 0x88 && d <= 0x8B) r = 'E';
      else if (d >= 0x8C && d <= 0x8F) r = 'I';
      else if (d == 0x91) r = 'N';
      else if (d >= 0x92 && d <= 0x96) r = 'O';
      else if (d >= 0x99 && d <= 0x9C) r = 'U';
      else if (d == 0x9F) r = 's';
      else if (d >= 0xA0 && d <= 0xA5) r = 'a';
      else if (d == 0xA7) r = 'c';
      else if (d >= 0xA8 && d <= 0xAB) r = 'e';
      else if (d >= 0xAC && d <= 0xAF) r = 'i';
      else if (d == 0xB1) r = 'n';
      else if (d >= 0xB2 && d <= 0xB6) r = 'o';
      else if (d >= 0xB9 && d <= 0xBC) r = 'u';
      if (r) dst[j++] = r;
      i += 2;
    } else {
      i++;  // otro multibyte: se descarta
    }
  }
  dst[j] = 0;
}

static void title(const char *t) {
  oled.clearDisplay();
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 15);
  oled.print(t);
  oled.setFont();
  oled.setTextSize(1);
}

void monoDisplayInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(250);
  oled.begin(OLED_I2C_ADDR, true);
  oled.setTextColor(SH110X_WHITE);
  monoShowMessage("BambuHelper", "Mono starting");
}

void monoShowBoot(const char *ssid, const char *ip) {
  title("Bambu");
  oled.setCursor(0, 26);
  oled.print("Helper Mono");
  oled.setCursor(0, 40);
  oled.print(ssid);
  oled.setCursor(0, 52);
  oled.print(ip);
  oled.display();
}

void monoShowMessage(const char *line1, const char *line2) {
  title(line1);
  oled.setCursor(0, 34);
  oled.print(line2);
  oled.display();
}

void monoShowStatus(const BambuState &st) {
  title(st.connected ? st.gcodeState : "Offline");
  char buf[24];
  if (st.connected && st.printing) {
    int px = (int)((st.progress * 126) / 100);
    oled.setCursor(0, 26);
    oled.print(st.progress);
    oled.print("% ");
    snprintf(buf, sizeof(buf), "%d/%d", st.layerNum, st.totalLayers);
    oled.print(buf);
    oled.drawRect(0, 36, 128, 6, SH110X_WHITE);
    oled.fillRect(0, 36, px, 6, SH110X_WHITE);
    oled.setCursor(0, 52);
    snprintf(buf, sizeof(buf), "N:%d B:%d", (int)st.nozzleTemp, (int)st.bedTemp);
    oled.print(buf);
  } else if (st.connected) {
    oled.setCursor(0, 30);
    snprintf(buf, sizeof(buf), "N:%d B:%d", (int)st.nozzleTemp, (int)st.bedTemp);
    oled.print(buf);
    oled.setCursor(0, 44);
    char name[22];
    deaccent(name, sizeof(name), st.subtaskName);
    oled.print(name);
  } else {
    oled.setCursor(0, 34);
    oled.print("No printer");
  }
  oled.display();
}

void monoShowError(unsigned long code) {
  title("ERR");
  oled.setCursor(0, 34);
  char buf[24];
  snprintf(buf, sizeof(buf), "code %lu", code);
  oled.print(buf);
  oled.display();
}

#endif  // BOARD_IS_MONO
