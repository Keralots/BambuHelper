#ifndef HMS_LOOKUP_H
#define HMS_LOOKUP_H

#include <Arduino.h>
#include "bambu_state.h"
#include "config.h"

#if HAS_HMS_UI

// Formatted-code buffer sizes, terminator included.
//   HMS bare:    "0500-0600-0002-0070"
//   HMS full:    "HMS_0500-0600-0002-0070"
//   print_error: "0300-8007"
//
// The separator is a hyphen and the full form carries an "HMS_" prefix because
// that is how Bambu itself writes these codes - the wiki renders
// "HMS_0300-1A00-0002-0002" even on pages whose own URL spells the code with
// underscores. Issue #164: our "0500_0100_0002_000B" matched nothing a user
// could search for.
#define HMS_CODE_STR_LEN          20
#define HMS_CODE_FULL_STR_LEN     24
#define PRINT_ERROR_CODE_STR_LEN  10

// Shown when an HMS code has no text on this board because the table is not
// compiled in - every HMS code on a 4 MB board. It does not mean "unknown
// code": codes Bambu ships no wording for are dropped before they get here
// (hmsIsDescribed), so the only thing missing is this board's copy of the
// sentence, and the portal really does have it in the published mirror.
#define HMS_FALLBACK_TEXT  "Description in the web portal"

// The print_error equivalent, and deliberately worded differently. print_error
// is exempt from the undescribed-code rule - a stopped job is worth reporting
// with or without wording - so this line is reachable for a code Bambu ships
// blank, and the mirror drops those blanks too. Promising the portal has the
// text would be a lie in exactly that case, which is the only case a C3 (no
// print_error table at all) and a blank feed entry can produce together.
#define PRINT_ERROR_FALLBACK_TEXT  "No description published for this code"

// "FATAL" / "SERIOUS" / "COMMON" / "INFO". `sev` is hmsSeverityOf(code);
// anything outside 1-3 reads as INFO and never alerts.
const char* hmsSeverityLabel(uint8_t sev);

// "AMS" / "MAIN" / "MC" / "XCAM" / "MODULE". Derived from the top byte of
// `attr`. Five module bytes exist in Bambu's live feed and all are named;
// MODULE is the catch-all for anything they add later.
const char* hmsModuleLabel(uint32_t attr);

// Bambu's own 16-hex-digit ecode, grouped: "0500-0600-0002-0070". The panel
// form: an entry has 218 px to draw in on a 240 px screen, and a one-line
// "<MODULE> <SEVERITY>  HMS_<code>" measures 252-314 px across every label pair
// (worst-case glyph widths from inter_10) - so the screen drops the prefix and
// puts the bare code, 162 px at worst, on a line of its own.
void hmsFormatCode(uint32_t attr, uint32_t code, char* out, size_t outSize);

// The same code in Bambu's own full notation: "HMS_0500-0600-0002-0070". Used
// everywhere the value is meant to be copied or searched - the portal card, the
// JSON APIs, the diagnostic dump.
void hmsFormatCodeFull(uint32_t attr, uint32_t code, char* out, size_t outSize);

// Bambu's 8-hex-digit print_error code, grouped: "0300-8007". No "HMS_" prefix:
// print_error is a different domain and wearing that prefix would send anyone
// searching for it to the wrong index.
void printErrorFormatCode(uint32_t err, char* out, size_t outSize);

// Official Bambu text, or NULL when this board carries no table for the domain
// or the code is not in it. Callers fall back to HMS_FALLBACK_TEXT plus the
// module/severity labels above.
const char* hmsLookupText(uint32_t attr, uint32_t code);
const char* printErrorLookupText(uint32_t err);

// True when Bambu's error feed carries this HMS code with a description.
//
// A code the feed does not describe is one the printer's own screen and Bambu
// Studio both stay silent about - Bambu registers the number and publishes no
// sentence for it. Issue #164 was an X2D standing permanently on two of them
// (0500_0100_0002_000B, absent from the feed entirely, and 0503_0000_0003_0027,
// present with an empty description), which we rendered as two alarming rows
// reading "Other HMS error" while the printer itself reported nothing wrong.
// Undescribed codes are dropped at parse time, so they never reach the badge,
// the error screen, the alerts, or the portal card. BambuState.hmsSuppressed
// counts them and /debug reports it, so a "why is my code missing" question is
// still answerable.
//
// Deliberately NOT applied to print_error: that means the job actually stopped,
// which is worth surfacing even with no wording to go with it.
bool hmsIsDescribed(uint32_t attr, uint32_t code);

// Codes hidden even though Bambu does describe them, because BambuHelper
// already shows the same condition somewhere that does not read as a fault.
//
// This is an editorial choice, not a fact about the feed, which is why it is a
// separate predicate from hmsIsDescribed() rather than another row in a table.
// Today it is the printer's door: the dashboard has a door indicator, so an
// open door is a state, not an error. Both spellings the printers use are
// listed, including the one Bambu ships no wording for - relying on that code
// staying undescribed would leave the rule one feed refresh from breaking.
//
// A true answer here is NOT on its own a reason to drop the code. The caller
// must also know that the surface this defers to exists on that printer -
// BambuState.doorSensorPresent for the door - because "shown elsewhere" is
// false on a printer that reports the door through the advisory alone.
//
// The door SENSOR fault (0300_9600_0001_0003, severity 1, "the front door Hall
// sensor is abnormal") is a real defect with no other surface, and stays.
bool hmsIsMuted(uint32_t attr, uint32_t code);

// Feed version stamp of the compiled-in tables, or NULL when none is compiled
// in. Reported by the portal so a stale table is diagnosable.
const char* hmsTableVersion(void);

// Feed version stamp behind hmsIsDescribed(), which every board has whether or
// not it carries sentences. NULL only where no key set exists at all.
const char* hmsKnownVersion(void);

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

// Worst error worth showing on this slot, or an inactive badge. Reads the
// master opt-out and the severity filter, so a settings change is reflected
// without waiting for the next report. Baseline codes (standing at connect)
// never reach the badge whatever the filter says - that rule is orthogonal and
// always on. print_error outranks HMS: it means the job actually stopped. A
// cancel is deliberately not an error badge - it gets the CANCELED state word.
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
