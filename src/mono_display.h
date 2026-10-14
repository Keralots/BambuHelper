// BambuHelper-Mono: capa Display mínima para SH1106 128x64 por I2C.
// API del plan: init, showBoot, showStatus(PrinterState), showError,
// showMessage. Estilo copiado del repo de referencia (ver REFERENCE_NOTES.md).
// Solo existe con BOARD_IS_MONO (env mono-c3); el resto de envs ni la ven.
#pragma once

struct BambuState;  // include/bambu_state.h

void monoDisplayInit();
void monoShowBoot(const char *ssid, const char *ip);
void monoShowMessage(const char *line1, const char *line2);
void monoShowStatus(const BambuState &st);
void monoShowError(unsigned long code);
