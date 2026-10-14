// BambuHelper-Mono: capa Display para SH1106 128x64 por I2C.
// Estilo copiado de ESP-Spotify-OLED (ver REFERENCE_NOTES.md):
// Adafruit_SH1106G, FreeMonoBold9pt7b para el titulo, fuente por defecto para
// el resto, barra con drawRect+fillRect y nombres transliterados.
// Solo existe con BOARD_IS_MONO (env mono-c3).
//
// Modelo de datos (Paso 1 del plan): NO hay struct PrinterState propio.
// BambuState (include/bambu_state.h) ya trae cada campo pedido y el parser
// MQTT (src/bambu_mqtt.cpp) solo actualiza los campos presentes en cada
// push parcial, conservando el resto:
//
//   gcodeState/gcodeStateId <- gcode_state, jobName=subtask_name,
//   percent=mc_percent, remainingMin=mc_remaining_time (minutos),
//   layer/totalLayers=layer_num/total_layer_num,
//   nozzle(+Target)=nozzle_temper(+_target), bed(+Target)=bed_temper(+_target),
//   errorCode=print_error, connected=estado MQTT.
//
// Constantes ajustables (todas sobrescribibles con -D en platformio.ini):
//   MONO_MAX_CHAR_TITLE_PER_LINE  corte wrap del titulo (def. 11, como Spotify)
//   MONO_HOT_THRESHOLD_C          aviso HOT de boquilla en idle (def. 50 C)
//   MONO_REFRESH_MS               refresco estatico (def. 1000 ms)
//   MONO_MARQUEE_MS               paso del marquee (~10 fps, def. 100 ms)
//   MONO_SHOW_END_TIME            1 = hora de fin (NTP) en vez de restante
//   MONO_MOCK                     1 = comandos "mock ..." por Serial
//   MONO_MOCK_AUTO                1 = arranca en modo mock cycle
#pragma once

#include <Arduino.h>

struct BambuState;  // include/bambu_state.h

// --- Constantes ajustables -------------------------------------------------
#ifndef MONO_MAX_CHAR_TITLE_PER_LINE
#define MONO_MAX_CHAR_TITLE_PER_LINE 11
#endif
#ifndef MONO_HOT_THRESHOLD_C
#define MONO_HOT_THRESHOLD_C 50
#endif
#ifndef MONO_REFRESH_MS
#define MONO_REFRESH_MS 1000
#endif
#ifndef MONO_MARQUEE_MS
#define MONO_MARQUEE_MS 100
#endif
#ifndef MONO_SHOW_END_TIME
#define MONO_SHOW_END_TIME 0
#endif
#ifndef MONO_MOCK
#define MONO_MOCK 1
#endif

// Pantallas (Paso 4). Prioridad en monoSelectScreen():
// !connected > FAILED/print_error != 0 > FINISH > PAUSE > PREPARE > RUNNING > IDLE
enum MonoScreen : uint8_t {
  MONO_SCR_OFFLINE = 0,
  MONO_SCR_PREPARE,
  MONO_SCR_PRINTING,
  MONO_SCR_PAUSE,
  MONO_SCR_FINISH,
  MONO_SCR_FAILED,
  MONO_SCR_IDLE,
};

MonoScreen monoSelectScreen(const BambuState &st);
const char *monoScreenName(MonoScreen s);

// --- Helpers (Paso 2, sin dibujar salvo los draw*) -------------------------
String monoCleanJobName(const String &raw);
String monoCleanJobName(const char *raw);
String monoFormatRemaining(int minutes);  // <0 = sin dato -> "--"

// Primitivas de dibujo (usan el oled interno; iconos sin glifos de fuente).
void monoDrawProgressBar(int y, int percent);          // drawRect(0,y,128,6)+fillRect map()
void monoDrawStatusIcon(int x, int y, MonoScreen s);   // triangulo / pausa / error / check
void monoDrawTitle(const String &clean);               // wrap 2 lineas o marquee 1 linea

// --- API de pantallas -------------------------------------------------------
void monoDisplayInit();
void monoShowBoot(const char *ssid, const char *ip);
void monoShowMessage(const char *line1, const char *line2);
void monoShowStatus(const BambuState &st);  // render inmediato (compat)
void monoShowError(unsigned long code);

// Bucle: llamar cada loop(). Redibuja solo si cambia el estado o cada
// MONO_REFRESH_MS; el marquee/parpadeo usan MONO_MARQUEE_MS (~10 fps).
// Respeta monoDisplaySetEnabled(false) (apagado por inactividad).
void monoDisplayTick();
void monoDisplaySetEnabled(bool on);
bool monoDisplayIsEnabled();
inline void oled_display(bool on) { monoDisplaySetEnabled(on); }

// --- Modo pruebas (Paso 6) --------------------------------------------------
// Serial 115200: "mock" (ayuda), "mock off", "mock cycle", "mock <n>".
// Casos: 0 imp.0% corto, 1 imp.50% ~20ch, 2 imp.100% largo (marquee),
// 3 pausa, 4 terminada, 5 error, 6 idle frio, 7 idle caliente (HOT),
// 8 sin conexion. Con -D MONO_MOCK_AUTO=1 arranca en "cycle".
void monoMockHandleSerial();
bool monoMockActive();
