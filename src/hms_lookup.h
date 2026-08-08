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

// ---------------------------------------------------------------------------
//  Cancel is not a failure
// ---------------------------------------------------------------------------
// A user-initiated cancel drives gcode_state to FAILED and raises print_error
// like any real fault, so every status surface reads "FAILED" / "ERROR!" for
// something the user did on purpose. These are the only two cancel-class codes
// in Bambu's feed; matching on the code rather than the text keeps this working
// on boards that carry no text table.
//
//   0300400C  "The task was canceled."     (seen on an H2 manual cancel)
//   0500400E  "Printing was cancelled."
#define PRINT_ERROR_CANCEL_MC    0x0300400CUL
#define PRINT_ERROR_CANCEL_MAIN  0x0500400EUL

// ---------------------------------------------------------------------------
//  Error badge
// ---------------------------------------------------------------------------
// The header state badge is replaced by this while an error is active. Severity
// drives the colour; the code identifies which error, so a repaint predicate can
// tell one error from the next without diffing the whole array.
#define ERROR_BADGE_TEXT   "ERR"
#define CANCELED_STATE_TEXT "CANCELED"

struct ErrorBadge {
  bool     active;
  uint8_t  severity;   // 1 fatal, 2 serious, 3 common; 0 = print_error/unknown
  uint32_t attr;       // 0 on a print_error badge
  uint32_t code;       // HMS code, or the print_error value
};

#if HAS_HMS_UI

// Severity filter. Becomes the user-facing "all priorities" vs "important only"
// setting in the settings phase; until then this is the plan's documented
// default. Baseline codes (standing at connect) never reach the badge either
// way - that rule is orthogonal and always on.
#define ERROR_BADGE_SEVERITY_ALL  0

// Worst error worth showing on this slot, or an inactive badge. print_error
// outranks HMS: it means the job actually stopped. A cancel is deliberately not
// an error badge - it gets the CANCELED state word instead.
ErrorBadge errorBadgeFor(const BambuState& s);

// Stable identity of the badge above, packed so a redraw predicate is one
// comparison and prevState carries it for free. 0 = nothing to show.
uint32_t errorBadgeId(const BambuState& s);

// Severity colour from the existing RGB565 palette.
uint16_t errorSeverityColor(uint8_t sev);

inline bool printErrorIsCancel(uint32_t err) {
  return err == PRINT_ERROR_CANCEL_MC || err == PRINT_ERROR_CANCEL_MAIN;
}

// The printer stopped, and the reason is a cancel rather than a fault.
inline bool printerWasCanceled(const BambuState& s) {
  return s.gcodeStateId == GCODE_FAILED && printErrorIsCancel(s.printError);
}

inline bool errorBadgeActive(const BambuState& s) {
  return errorBadgeFor(s).active;
}

#else   // !HAS_HMS_UI - nothing is parsed, so nothing can be shown

inline ErrorBadge errorBadgeFor(const BambuState&) { return ErrorBadge{}; }
inline uint32_t   errorBadgeId(const BambuState&)  { return 0; }
inline uint16_t   errorSeverityColor(uint8_t)      { return CLR_RED; }
inline bool       printErrorIsCancel(uint32_t)     { return false; }
inline bool       printerWasCanceled(const BambuState&) { return false; }
inline bool       errorBadgeActive(const BambuState&)   { return false; }

#endif  // HAS_HMS_UI

#endif  // HMS_LOOKUP_H
