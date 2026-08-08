#ifndef HMS_LOOKUP_H
#define HMS_LOOKUP_H

#include <Arduino.h>
#include "bambu_state.h"
#include "config.h"

#if HAS_HMS_UI

// Formatted-code buffer sizes, terminator included.
//   HMS:         "0500_0600_0002_0070"
//   print_error: "0300_8007"
#define HMS_CODE_STR_LEN          20
#define PRINT_ERROR_CODE_STR_LEN  10

// Shown when a code has no text on this board - either the table is not
// compiled in, or Bambu's feed has no sentence for it (64 HMS and 9
// print_error codes ship blank).
#define HMS_FALLBACK_TEXT  "Other HMS error - check printer or app"

// "FATAL" / "SERIOUS" / "COMMON" / "INFO". `sev` is hmsSeverityOf(code);
// anything outside 1-3 reads as INFO and never alerts.
const char* hmsSeverityLabel(uint8_t sev);

// "AMS" / "MAIN" / "MC" / "XCAM" / "MODULE". Derived from the top byte of
// `attr`. Five module bytes exist in Bambu's live feed and all are named;
// MODULE is the catch-all for anything they add later.
const char* hmsModuleLabel(uint32_t attr);

// Bambu's own 16-hex-digit ecode, grouped: "0500_0600_0002_0070".
void hmsFormatCode(uint32_t attr, uint32_t code, char* out, size_t outSize);

// Bambu's 8-hex-digit print_error code, grouped: "0300_8007".
void printErrorFormatCode(uint32_t err, char* out, size_t outSize);

// Official Bambu text, or NULL when this board carries no table for the domain
// or the code is not in it. Callers fall back to HMS_FALLBACK_TEXT plus the
// module/severity labels above.
const char* hmsLookupText(uint32_t attr, uint32_t code);
const char* printErrorLookupText(uint32_t err);

// Feed version stamp of the compiled-in tables, or NULL when none is compiled
// in. Reported by the portal so a stale table is diagnosable.
const char* hmsTableVersion(void);

#endif  // HAS_HMS_UI

#endif  // HMS_LOOKUP_H
