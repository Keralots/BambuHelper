// BambuHelper-Mono: SH1106 128x64 por I2C con Adafruit GFX/SH110X.
// Estilo copiado del repo de referencia ESP-Spotify-OLED (ver REFERENCE_NOTES.md).
#ifdef BOARD_IS_MONO

#include "mono_display.h"
#include "bambu_state.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <time.h>

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
#ifndef MONO_MOCK_AUTO
#define MONO_MOCK_AUTO 0
#endif

// Layout verificado contra solapes (fuente default = 6x8 px/celda):
// titulo 1 linea: baseline ~y=20 (ocupa ~8..20)
// titulo 2 lineas: baselines y=12 / y=27 (ocupan ~0..12 y ~15..27)
// TEMP_Y=31 (31..38), LAYER_Y=40 (40..47), BAR_Y=50 (50..55), BOTTOM_Y=56 (56..63).
// Desvios de 1 px frente al boceto del plan (32/57) para no pisar ni recortar.
static const int TITLE_SINGLE_Y = 20;
static const int TITLE_Y1 = 12;
static const int TITLE_Y2 = 27;
static const int TEMP_Y = 31;
static const int LAYER_Y = 40;
static const int BAR_Y = 50;
static const int BAR_H = 6;
static const int BOTTOM_Y = 56;

static Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

static bool s_enabled = true;
static bool s_booted = false;

// --- Transliteracion (equivale a replaceSpecialCharacters del repo de ----
// --- referencia: la GFX no trae acentos, se pasa a ASCII base) -------------
static void deaccent(char *dst, size_t n, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j + 1 < n;) {
    unsigned char c = (unsigned char)src[i];
    if (c < 0x80) {
      dst[j++] = (char)c;
      i++;
    } else if (c == 0xC3 && src[i + 1]) {
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

static bool endsWith(const char *s, const char *suffix) {
  size_t sl = strlen(s), xl = strlen(suffix);
  return sl >= xl && strcmp(s + sl - xl, suffix) == 0;
}

// --- Paso 2: helpers --------------------------------------------------------
String monoCleanJobName(const char *raw) {
  if (!raw) return String("");
  char tmp[128];
  strlcpy(tmp, raw, sizeof(tmp));
  // Sufijos del slicer: orden importa (.gcode.3mf antes que .3mf).
  if (endsWith(tmp, ".gcode.3mf")) tmp[strlen(tmp) - 10] = 0;
  else if (endsWith(tmp, ".3mf")) tmp[strlen(tmp) - 4] = 0;
  else if (endsWith(tmp, ".gcode")) tmp[strlen(tmp) - 6] = 0;
  for (char *p = tmp; *p; p++)
    if (*p == '_') *p = ' ';
  char out[128];
  deaccent(out, sizeof(out), tmp);
  // Recorta espacios de los bordes.
  String s(out);
  s.trim();
  return s;
}

String monoCleanJobName(const String &raw) { return monoCleanJobName(raw.c_str()); }

String monoFormatRemaining(int minutes) {
  char buf[16];
  if (minutes < 0) return String("--");
  if (minutes < 60) {
    snprintf(buf, sizeof(buf), "%dm", minutes);
  } else {
    snprintf(buf, sizeof(buf), "%dh%02dm", minutes / 60, minutes % 60);
  }
  return String(buf);
}

void monoDrawProgressBar(int y, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  // Misma barra que Spotify: drawRect + fillRect con map().
  oled.drawRect(0, y, 128, BAR_H, SH110X_WHITE);
  oled.fillRect(0, y, map(percent, 0, 100, 0, 126), BAR_H, SH110X_WHITE);
}

void monoDrawStatusIcon(int x, int y, MonoScreen s) {
  switch (s) {
    case MONO_SCR_PRINTING:
    case MONO_SCR_PREPARE:
      // Triangulo "play".
      oled.fillTriangle(x, y, x, y + 7, x + 6, y + 3, SH110X_WHITE);
      break;
    case MONO_SCR_PAUSE:
      oled.fillRect(x, y, 2, 8, SH110X_WHITE);
      oled.fillRect(x + 4, y, 2, 8, SH110X_WHITE);
      break;
    case MONO_SCR_FAILED:
      // Triangulo de aviso + "!" con primitivas (sin glifos).
      oled.drawTriangle(x + 3, y, x, y + 7, x + 7, y + 7, SH110X_WHITE);
      oled.drawFastVLine(x + 3, y + 2, 3, SH110X_WHITE);
      oled.drawPixel(x + 3, y + 6, SH110X_WHITE);
      break;
    case MONO_SCR_FINISH:
      // Check con dos lineas (grosor simple, dentro del cajon 8x8).
      oled.drawLine(x, y + 4, x + 3, y + 7, SH110X_WHITE);
      oled.drawLine(x + 3, y + 7, x + 8, y, SH110X_WHITE);
      break;
    case MONO_SCR_OFFLINE:
      // X de "sin conexion".
      oled.drawLine(x, y, x + 7, y + 7, SH110X_WHITE);
      oled.drawLine(x + 7, y, x, y + 7, SH110X_WHITE);
      break;
    case MONO_SCR_IDLE:
    default:
      break;
  }
}

// --- Titulo con wrap / marquee ----------------------------------------------
static String s_mqText = "";
static int s_mqX = 0;
static unsigned long s_mqLastMs = 0;
static uint8_t s_mqHold = 0;  // ticks de pausa en cada extremo
static bool s_marqueeActive = false;

static void marqueeReset(const String &t) {
  s_mqText = t;
  s_mqX = 0;
  s_mqLastMs = millis();
  s_mqHold = 8;  // pausa inicial para leer el principio
}

void monoDrawTitle(const String &clean) {
  oled.setFont(&FreeMonoBold9pt7b);
  const int maxLine = MONO_MAX_CHAR_TITLE_PER_LINE;
  int len = (int)clean.length();
  if (len <= maxLine) {
    s_marqueeActive = false;
    oled.setCursor(0, TITLE_SINGLE_Y);
    oled.print(clean);
  } else if (len <= 2 * maxLine) {
    s_marqueeActive = false;
    // Corte en el ultimo espacio dentro del limite, o corte duro.
    int cut = maxLine;
    int sp = clean.lastIndexOf(' ', maxLine);
    if (sp > maxLine - 5 && sp > 0) cut = sp;
    String l1 = clean.substring(0, cut);
    String l2 = clean.substring(cut);
    l2.trim();
    l1.trim();
    oled.setCursor(0, TITLE_Y1);
    oled.print(l1);
    oled.setCursor(0, TITLE_Y2);
    oled.print(l2);
  } else {
    // Marquee horizontal en una sola linea.
    s_marqueeActive = true;
    if (s_mqText != clean) marqueeReset(clean);
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(clean, 0, 0, &x1, &y1, &w, &h);
    (void)x1;
    (void)y1;
    (void)h;
    unsigned long now = millis();
    if (now - s_mqLastMs >= (unsigned long)MONO_MARQUEE_MS) {
      s_mqLastMs = now;
      if (s_mqHold > 0) {
        s_mqHold--;
      } else {
        s_mqX -= 3;
        if (s_mqX + (int)w <= 128) {
          // Fin visible: pausa y vuelta al principio.
          s_mqX = 0;
          s_mqHold = 8;
        }
      }
    }
    oled.setCursor(s_mqX, TITLE_SINGLE_Y);
    oled.print(clean);
  }
  oled.setFont();
  oled.setTextSize(1);
}

// --- Paso 4: seleccion de pantalla -------------------------------------------
const char *monoScreenName(MonoScreen s) {
  switch (s) {
    case MONO_SCR_OFFLINE: return "OFFLINE";
    case MONO_SCR_PREPARE: return "PREPARE";
    case MONO_SCR_PRINTING: return "PRINTING";
    case MONO_SCR_PAUSE: return "PAUSE";
    case MONO_SCR_FINISH: return "FINISH";
    case MONO_SCR_FAILED: return "FAILED";
    case MONO_SCR_IDLE:
    default: return "IDLE";
  }
}

MonoScreen monoSelectScreen(const BambuState &st) {
  if (!st.connected) return MONO_SCR_OFFLINE;
#if HAS_HMS_UI
  if (st.printError != 0) return MONO_SCR_FAILED;
#endif
  if (st.gcodeStateId == GCODE_FAILED) return MONO_SCR_FAILED;
  switch (st.gcodeStateId) {
    case GCODE_FINISH: return MONO_SCR_FINISH;
    case GCODE_PAUSE: return MONO_SCR_PAUSE;
    case GCODE_PREPARE: return MONO_SCR_PREPARE;
    case GCODE_RUNNING: return MONO_SCR_PRINTING;
    case GCODE_IDLE:
    case GCODE_UNKNOWN:
    case GCODE_OTHER:
    default: return MONO_SCR_IDLE;
  }
}

// --- Utilidades de formato ----------------------------------------------------
static int tempInt(float v) { return (int)(v + 0.5f); }

// "N 215/220" o "N 215" si el objetivo es 0.
static void fmtTemp(char *dst, size_t n, char label, float cur, float target) {
  if (target < 0.5f)
    snprintf(dst, n, "%c %d", label, tempInt(cur));
  else
    snprintf(dst, n, "%c %d/%d", label, tempInt(cur), tempInt(target));
}

static bool ntpOk() { return time(nullptr) > (time_t)NTP_SYNCED_EPOCH; }

static void fmtClock(char *dst, size_t n) {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  snprintf(dst, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

// Fila inferior: tiempo restante o (opcional) hora de fin estimada.
static String bottomTime(const BambuState &st) {
#if MONO_SHOW_END_TIME
  if (ntpOk() && (int)st.remainingMinutes >= 0 && st.remainingMinutes < 60 * 48) {
    time_t end = time(nullptr) + (time_t)st.remainingMinutes * 60;
    struct tm tmv;
    localtime_r(&end, &tmv);
    char buf[16];
    snprintf(buf, sizeof(buf), "fin %02d:%02d", tmv.tm_hour, tmv.tm_min);
    return String(buf);
  }
#endif
  if (!st.connected) return String("--");
  return monoFormatRemaining((int)st.remainingMinutes);
}

// --- Render de cada pantalla (todas terminan con display()) --------------------
static void renderPrinting(const BambuState &st, MonoScreen scr, bool blinkOn) {
  oled.clearDisplay();
  monoDrawTitle(monoCleanJobName(jobDisplayName(st)));
  char nb[20], bb[20], line[40];
  fmtTemp(nb, sizeof(nb), 'N', st.nozzleTemp, st.nozzleTarget);
  fmtTemp(bb, sizeof(bb), 'B', st.bedTemp, st.bedTarget);
  snprintf(line, sizeof(line), "%s  %s", nb, bb);
  oled.setCursor(0, TEMP_Y);
  oled.print(line);
  oled.setCursor(0, LAYER_Y);
  if (st.totalLayers > 0) {
    snprintf(line, sizeof(line), "Capa %u/%u", st.layerNum, st.totalLayers);
    oled.print(line);
  } else if (st.layerNum > 0) {
    snprintf(line, sizeof(line), "Capa %u", st.layerNum);
    oled.print(line);
  }
  monoDrawProgressBar(BAR_Y, st.progress);
  // Fila inferior: icono + % a la izquierda, tiempo a la derecha.
  String pct = String((int)st.progress) + "%";
  String t = bottomTime(st);
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  (void)x1;
  (void)y1;
  (void)h;
  if (blinkOn) {
    monoDrawStatusIcon(0, BOTTOM_Y, scr);
    oled.setCursor(11, BOTTOM_Y);
    oled.print(pct);
  }
  oled.setCursor(128 - (int)w, BOTTOM_Y);
  oled.print(t);
  oled.display();
}

static void renderFinish(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(monoCleanJobName(jobDisplayName(st)));
  monoDrawProgressBar(BAR_Y, 100);
  oled.setCursor(0, TEMP_Y);
  oled.print("Completada");
  oled.setCursor(0, LAYER_Y);
  oled.print("100%");
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_FINISH);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("fin");
  oled.display();
}

static void renderIdle(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(String("Idle"));
  // Temps en grande.
  char line[24];
  oled.setFont(&FreeMonoBold9pt7b);
  snprintf(line, sizeof(line), "N%d B%d", tempInt(st.nozzleTemp), tempInt(st.bedTemp));
  oled.setCursor(0, 44);
  oled.print(line);
  oled.setFont();
  oled.setTextSize(1);
  // Avisos + hora en la fila inferior.
  oled.setCursor(0, BOTTOM_Y);
  if (st.nozzleTemp > (float)MONO_HOT_THRESHOLD_C) {
    oled.print("HOT ");
  }
  if (ntpOk()) {
    char clk[8];
    fmtClock(clk, sizeof(clk));
    if (st.nozzleTemp > (float)MONO_HOT_THRESHOLD_C)
      oled.print(clk);
    else {
      int16_t x1, y1;
      uint16_t w, h;
      String s(clk);
      oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
      oled.setCursor(128 - (int)w, BOTTOM_Y);
      oled.print(s);
    }
  }
  oled.display();
}

static void renderFailed(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(String("ERR"));
  oled.setCursor(0, TEMP_Y);
#if HAS_HMS_UI
  char buf[24];
  snprintf(buf, sizeof(buf), "code %08X", (unsigned)st.printError);
  oled.print(buf);
#else
  oled.print(st.gcodeState);
#endif
  oled.setCursor(0, LAYER_Y);
  String name = monoCleanJobName(jobDisplayName(st));
  if (name.length() > 21) name = name.substring(0, 21);
  oled.print(name);
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_FAILED);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("error");
  oled.display();
}

static void renderOffline() {
  oled.clearDisplay();
  monoDrawTitle(String("Sin conexion"));
  oled.setCursor(0, TEMP_Y);
  oled.print("printer IP:");
  oled.setCursor(0, LAYER_Y);
  const char *ip = "";
  if (activePrinterIndex < MAX_PRINTERS) ip = printers[activePrinterIndex].config.ip;
  oled.print((ip && ip[0]) ? ip : "--");
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_OFFLINE);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("offline");
  oled.display();
}

static void renderPrepare(const BambuState &st) {
  oled.clearDisplay();
  monoDrawTitle(String("Calentando"));
  char nb[20], bb[20], line[40];
  fmtTemp(nb, sizeof(nb), 'N', st.nozzleTemp, st.nozzleTarget);
  fmtTemp(bb, sizeof(bb), 'B', st.bedTemp, st.bedTarget);
  snprintf(line, sizeof(line), "%s  %s", nb, bb);
  oled.setCursor(0, TEMP_Y);
  oled.print(line);
  oled.setCursor(0, LAYER_Y);
  oled.print(monoCleanJobName(jobDisplayName(st)).substring(0, 21));
  monoDrawProgressBar(BAR_Y, 0);
  monoDrawStatusIcon(0, BOTTOM_Y, MONO_SCR_PREPARE);
  oled.setCursor(11, BOTTOM_Y);
  oled.print("0%");
  oled.display();
}

// --- Deteccion de cambios (Paso 5) ---------------------------------------------
struct MonoSig {
  MonoScreen scr;
  int progress;
  int remaining;
  unsigned layer;
  unsigned total;
  int nzl, nzlT, bed, bedT;
  unsigned long err;
  bool connected;
  char job[40];
  int clockMin;  // para el reloj de idle / hora de fin
};

static MonoSig s_last;
static bool s_haveLast = false;
static unsigned long s_lastFrameMs = 0;

static MonoSig makeSig(MonoScreen scr, const BambuState &st) {
  MonoSig s;
  s.scr = scr;
  s.progress = (int)st.progress;
  s.remaining = (int)st.remainingMinutes;
  s.layer = st.layerNum;
  s.total = st.totalLayers;
  s.nzl = tempInt(st.nozzleTemp);
  s.nzlT = tempInt(st.nozzleTarget);
  s.bed = tempInt(st.bedTemp);
  s.bedT = tempInt(st.bedTarget);
#if HAS_HMS_UI
  s.err = st.printError;
#else
  s.err = 0;
#endif
  s.connected = st.connected;
  String j = monoCleanJobName(jobDisplayName(st));
  strlcpy(s.job, j.c_str(), sizeof(s.job));
  if (ntpOk()) {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    s.clockMin = tmv.tm_hour * 60 + tmv.tm_min;
  } else {
    s.clockMin = -1;
  }
  return s;
}

static bool sigSame(const MonoSig &a, const MonoSig &b) {
  return a.scr == b.scr && a.progress == b.progress && a.remaining == b.remaining &&
         a.layer == b.layer && a.total == b.total && a.nzl == b.nzl &&
         a.nzlT == b.nzlT && a.bed == b.bed && a.bedT == b.bedT && a.err == b.err &&
         a.connected == b.connected && strcmp(a.job, b.job) == 0 &&
         a.clockMin == b.clockMin;
}

static void renderNow(MonoScreen scr, const BambuState &st) {
  bool blinkOn = true;
  if (scr == MONO_SCR_PAUSE) blinkOn = ((millis() / 500) % 2) == 0;
  switch (scr) {
    case MONO_SCR_PRINTING:
    case MONO_SCR_PAUSE: renderPrinting(st, scr, blinkOn); break;
    case MONO_SCR_FINISH: renderFinish(st); break;
    case MONO_SCR_FAILED: renderFailed(st); break;
    case MONO_SCR_OFFLINE: renderOffline(); break;
    case MONO_SCR_PREPARE: renderPrepare(st); break;
    case MONO_SCR_IDLE:
    default: renderIdle(st); break;
  }
}

// --- Modo pruebas (Paso 6) ------------------------------------------------------
#if MONO_MOCK
static bool s_mockOn = false;
static bool s_mockCycle = false;
static int s_mockIdx = 0;
static unsigned long s_mockLastMs = 0;

static void mockBuild(int idx, BambuState &o) {
  memset(&o, 0, sizeof(o));
  o.connected = true;
  o.progress = 0;
  o.remainingMinutes = 0;
  switch (idx) {
    case 0:  // imprimiendo 0%, nombre corto
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Cubo", sizeof(o.subtaskName));
      o.progress = 0;
      o.remainingMinutes = 45;
      o.layerNum = 1;
      o.totalLayers = 200;
      o.nozzleTemp = 215;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 1:  // imprimiendo 50%, ~20 caracteres
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Soporte_movil_20ch", sizeof(o.subtaskName));
      o.progress = 50;
      o.remainingMinutes = 83;
      o.layerNum = 123;
      o.totalLayers = 456;
      o.nozzleTemp = 215;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 2:  // imprimiendo 100%-> marquee largo
      setPrinterGcodeStateCanonical(o, GCODE_RUNNING);
      o.printing = true;
      strlcpy(o.subtaskName, "Pieza_muy_larga_para_marquee_con_detalles_que_no_cabe.gcode.3mf",
              sizeof(o.subtaskName));
      o.progress = 87;
      o.remainingMinutes = 125;
      o.layerNum = 300;
      o.totalLayers = 345;
      o.nozzleTemp = 220;
      o.nozzleTarget = 220;
      o.bedTemp = 65;
      o.bedTarget = 65;
      break;
    case 3:  // pausa
      setPrinterGcodeStateCanonical(o, GCODE_PAUSE);
      o.printing = true;
      strlcpy(o.subtaskName, "Soporte_movil_20ch", sizeof(o.subtaskName));
      o.progress = 50;
      o.remainingMinutes = 83;
      o.layerNum = 123;
      o.totalLayers = 456;
      o.nozzleTemp = 210;
      o.nozzleTarget = 220;
      o.bedTemp = 60;
      o.bedTarget = 60;
      break;
    case 4:  // terminada
      setPrinterGcodeStateCanonical(o, GCODE_FINISH);
      o.printing = false;
      strlcpy(o.subtaskName, "Cubo", sizeof(o.subtaskName));
      o.progress = 100;
      o.nozzleTemp = 40;
      o.bedTemp = 35;
      break;
    case 5:  // error
      setPrinterGcodeStateCanonical(o, GCODE_FAILED);
      o.printing = false;
      strlcpy(o.subtaskName, "Pieza_fallida", sizeof(o.subtaskName));
#if HAS_HMS_UI
      o.printError = 0x03008007UL;
      o.printErrorSeen = true;
#endif
      o.nozzleTemp = 180;
      o.bedTemp = 50;
      break;
    case 6:  // idle frio
      setPrinterGcodeStateCanonical(o, GCODE_IDLE);
      o.printing = false;
      o.nozzleTemp = 24;
      o.bedTemp = 24;
      break;
    case 7:  // idle caliente -> HOT
      setPrinterGcodeStateCanonical(o, GCODE_IDLE);
      o.printing = false;
      o.nozzleTemp = 62;
      o.bedTemp = 45;
      break;
    case 8:  // sin conexion
    default:
      o.connected = false;
      setPrinterGcodeStateCanonical(o, GCODE_UNKNOWN);
      break;
  }
}

static void mockHelp() {
  Serial.println(F("mock: 0 imp.0% corto | 1 imp.50% 20ch | 2 imp.marquee largo | "
                   "3 pausa | 4 fin | 5 error | 6 idle frio | 7 idle HOT | 8 offline"));
  Serial.println(F("uso: mock <n> | mock cycle | mock off"));
}

static String s_serLine = "";

void monoMockHandleSerial() {
  // Lee solo lineas que empiecen por 'm' (peek) para no robar el resto de
  // protocolos serie (p. ej. Improv en primer arranque).
  while (Serial.available()) {
    if (Serial.peek() != 'm' && s_serLine.length() == 0) return;
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_serLine.trim();
      if (s_serLine.startsWith(F("mock"))) {
        String arg = s_serLine.substring(4);
        arg.trim();
        if (arg.length() == 0) {
          mockHelp();
        } else if (arg == "off") {
          s_mockOn = false;
          s_mockCycle = false;
          s_haveLast = false;  // fuerza repintado con datos reales
          Serial.println(F("mock off"));
        } else if (arg == "cycle") {
          s_mockOn = true;
          s_mockCycle = true;
          s_mockLastMs = millis();
          s_haveLast = false;
          Serial.println(F("mock cycle: rotando 9 pantallas cada 3 s"));
        } else {
          int idx = arg.toInt();
          if (idx >= 0 && idx <= 8) {
            s_mockOn = true;
            s_mockCycle = false;
            s_mockIdx = idx;
            s_haveLast = false;
            Serial.printf("mock %d\n", idx);
          } else {
            mockHelp();
          }
        }
      }
      s_serLine = "";
    } else {
      s_serLine += c;
      if (s_serLine.length() > 32) s_serLine = "";  // linea ajena: descarta
    }
  }
}

bool monoMockActive() { return s_mockOn; }
#else
void monoMockHandleSerial() {}
bool monoMockActive() { return false; }
#endif  // MONO_MOCK

// --- API -----------------------------------------------------------------------
void monoDisplayInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);  // 400 kHz: el marquee a ~10 fps lo necesita
  delay(250);
  oled.begin(OLED_I2C_ADDR, true);
  oled.setTextColor(SH110X_WHITE);
  oled.setTextWrap(false);  // el wrap del titulo es manual; el resto no desborda
  s_booted = true;
  s_enabled = true;
  Serial.printf("MONO heap=%u\n", (unsigned)ESP.getFreeHeap());
  monoShowMessage("BambuHelper", "Mono starting");
#if MONO_MOCK && MONO_MOCK_AUTO
  s_mockOn = true;
  s_mockCycle = true;
  s_mockLastMs = millis();
  Serial.println(F("mock cycle (MONO_MOCK_AUTO)"));
#endif
}

void monoShowBoot(const char *ssid, const char *ip) {
  if (!s_booted || !s_enabled) return;
  oled.clearDisplay();
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 15);
  oled.print("Bambu");
  oled.setFont();
  oled.setTextSize(1);
  oled.setCursor(0, 26);
  oled.print("Helper Mono");
  oled.setCursor(0, 40);
  oled.print(ssid ? ssid : "");
  oled.setCursor(0, 52);
  oled.print(ip ? ip : "");
  oled.display();
}

void monoShowMessage(const char *line1, const char *line2) {
  if (!s_booted || !s_enabled) return;
  oled.clearDisplay();
  oled.setFont(&FreeMonoBold9pt7b);
  oled.setCursor(0, 15);
  oled.print(line1 ? line1 : "");
  oled.setFont();
  oled.setTextSize(1);
  oled.setCursor(0, 34);
  oled.print(line2 ? line2 : "");
  oled.display();
}

void monoShowStatus(const BambuState &st) {
  if (!s_booted || !s_enabled) return;
  MonoScreen scr = monoSelectScreen(st);
  renderNow(scr, st);
  s_last = makeSig(scr, st);
  s_haveLast = true;
  s_lastFrameMs = millis();
}

void monoShowError(unsigned long code) {
  if (!s_booted || !s_enabled) return;
  BambuState st;
  memset(&st, 0, sizeof(st));
  st.connected = true;
  setPrinterGcodeStateCanonical(st, GCODE_FAILED);
#if HAS_HMS_UI
  st.printError = (uint32_t)code;
  st.printErrorSeen = true;
#endif
  renderFailed(st);
}

void monoDisplaySetEnabled(bool on) {
  s_enabled = on;
  if (s_booted && !on) {
    oled.clearDisplay();
    oled.display();
    s_haveLast = false;  // al reencender, repinta aunque nada cambie
  } else {
    s_haveLast = false;
  }
}

bool monoDisplayIsEnabled() { return s_enabled; }

void monoDisplayTick() {
  if (!s_booted || !s_enabled) return;
#if MONO_MOCK
  BambuState mock;
  const BambuState *src = nullptr;
  if (s_mockOn) {
    if (s_mockCycle && millis() - s_mockLastMs >= 3000) {
      s_mockLastMs = millis();
      s_mockIdx = (s_mockIdx + 1) % 9;
      Serial.printf("mock %d\n", s_mockIdx);
    }
    mockBuild(s_mockIdx, mock);
    src = &mock;
  } else {
    if (activePrinterIndex < MAX_PRINTERS)
      src = &printers[activePrinterIndex].state;
    else
      src = &printers[0].state;
  }
#else
  const BambuState *src = (activePrinterIndex < MAX_PRINTERS)
                              ? &printers[activePrinterIndex].state
                              : &printers[0].state;
#endif
  const BambuState &st = *src;
  MonoScreen scr = monoSelectScreen(st);
  MonoSig sig = makeSig(scr, st);
  bool changed = !s_haveLast || !sigSame(sig, s_last);
  unsigned long now = millis();
  // Marquee/parpadeo => ~10 fps; estatico => 1 s.
  bool animated = s_marqueeActive || scr == MONO_SCR_PAUSE ||
                  (s_haveLast && s_last.scr != scr);
  if (scr == MONO_SCR_PAUSE) animated = true;
  unsigned long interval =
      animated ? (unsigned long)MONO_MARQUEE_MS : (unsigned long)MONO_REFRESH_MS;
  if (changed || now - s_lastFrameMs >= interval) {
    renderNow(scr, st);
    s_last = sig;
    s_haveLast = true;
    s_lastFrameMs = now;
  }
}

#endif  // BOARD_IS_MONO
