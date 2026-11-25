// Serial prefix -> model index - GENERATED, DO NOT EDIT.
// Regenerate with: python tools/gen_error_tables.py
//
// Feed ver: 202611242216
// The index is a bit position in the model masks of the other tables.
// Models whose feeds are identical share one.
#ifndef ERROR_MODELS_H
#define ERROR_MODELS_H

#include <Arduino.h>

#define ERROR_MODEL_COUNT 16

static const char ERROR_MODEL_SN[ERROR_MODEL_COUNT][4] PROGMEM = {
  "00M",  // X1C
  "00W",  // X1
  "03W",  // X1E
  "01S",  // P1P
  "01P",  // P1S
  "22E",  // P2S
  "331",  // P3C
  "030",  // A1 mini
  "039",  // A1
  "26A",  // A2L
  "093",  // H2S
  "094",  // H2D
  "239",  // H2D Pro
  "31B",  // H2C
  "32X",  // H3C
  "20P",  // X2D
};
static const uint8_t ERROR_MODEL_IDX[ERROR_MODEL_COUNT] PROGMEM = {
  0, 0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
};

#endif  // ERROR_MODELS_H
