#include "hms_lookup.h"
#include "settings.h"   // dispSettings: master opt-out + severity filter

#if HAS_HMS_UI

// The generated tables are huge PROGMEM blobs with internal linkage, so they
// are included exactly once - here - the same way fonts.cpp owns the VLW data.
// Regenerate both with: python tools/gen_error_tables.py
#if HAS_ERROR_TEXT_TABLE
#include "error_tables/print_error_table.h"
#endif
#if HAS_FULL_HMS_TABLE
#include "error_tables/hms_table.h"
#endif
#if HAS_HMS_KNOWN_TABLE
#include "error_tables/hms_known.h"
// hmsIsDescribed() indexes both arrays with uint16_t. The feed would have to
// roughly double for either to matter, but a silent wrap would turn the
// suppression rule into a coin toss - so it is a build failure instead.
static_assert(HMS_KNOWN_ATTR_COUNT <= 0xFFFE,
              "HMS_KNOWN_ATTR_COUNT no longer fits the uint16_t search bounds");
static_assert(HMS_KNOWN_CODE_COUNT <= 0xFFFF,
              "HMS_KNOWN_CODE_COUNT no longer fits the uint16_t run bounds");
// And bounded from below, because the failure is silent in the other direction
// too: an empty table compiles (GCC takes zero-length arrays as an extension)
// and would suppress every non-fatal HMS code on every board that carries it.
static_assert(HMS_KNOWN_ATTR_COUNT > 0, "hms_known.h generated an empty table");
static_assert(HMS_KNOWN_CODE_COUNT > 0, "hms_known.h generated an empty table");
#endif

// ---------------------------------------------------------------------------
//  Labels
// ---------------------------------------------------------------------------
const char* hmsSeverityLabel(uint8_t sev) {
  switch (sev) {
    case 1:  return "FATAL";
    case 2:  return "SERIOUS";
    case 3:  return "COMMON";
    default: return "INFO";
  }
}

const char* hmsModuleLabel(uint32_t attr) {
  // Module byte, verified against Bambu's live feed: only these five appear
  // across all 4022 codes. ha-bambulab's bundled map lists more (0x08
  // "toolhead"), but that file is a merged historical superset and 0x08 is
  // absent from the current feed - do not add labels we cannot source.
  switch ((attr >> 24) & 0xFF) {
    case 0x07: return "AMS";    // AMS
    case 0x18: return "AMS";    // AMS, newer generation - same thing to a user
    case 0x05: return "MAIN";   // mainboard
    case 0x03: return "MC";     // motion controller
    case 0x0C: return "XCAM";   // xcam / lidar
    default:   return "MODULE";
  }
}

// ---------------------------------------------------------------------------
//  Code formatting
// ---------------------------------------------------------------------------
void hmsFormatCode(uint32_t attr, uint32_t code, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "%04X-%04X-%04X-%04X",
           (unsigned)(attr >> 16), (unsigned)(attr & 0xFFFF),
           (unsigned)(code >> 16), (unsigned)(code & 0xFFFF));
}

void hmsFormatCodeFull(uint32_t attr, uint32_t code, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "HMS_%04X-%04X-%04X-%04X",
           (unsigned)(attr >> 16), (unsigned)(attr & 0xFFFF),
           (unsigned)(code >> 16), (unsigned)(code & 0xFFFF));
}

void printErrorFormatCode(uint32_t err, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "%04X-%04X",
           (unsigned)(err >> 16), (unsigned)(err & 0xFFFF));
}

// ---------------------------------------------------------------------------
//  Table lookup
// ---------------------------------------------------------------------------
// Both tables are sorted ascending by key, so a plain binary search over the
// key array gives the parallel string id. Blank-intro codes were dropped by the
// generator, so a hit always has real text.

#if HAS_ERROR_TEXT_TABLE
static const char* printErrorTextById(uint16_t id) {
  if (id >= PRINT_ERROR_TEXT_COUNT) return NULL;
  return PRINT_ERROR_TEXT_BLOB + PRINT_ERROR_TEXT_OFF[id];
}
#endif

#if HAS_FULL_HMS_TABLE
static const char* hmsTextById(uint16_t id) {
  if (id >= HMS_TEXT_COUNT) return NULL;
  return HMS_TEXT_BLOB + HMS_TEXT_OFF[id];
}
#endif

const char* printErrorLookupText(uint32_t err) {
#if HAS_ERROR_TEXT_TABLE
  if (err == 0) return NULL;
  uint16_t lo = 0, hi = PRINT_ERROR_TABLE_COUNT;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    uint32_t key = PRINT_ERROR_KEYS[mid];
    if (key == err) return printErrorTextById(PRINT_ERROR_TEXT_ID[mid]);
    if (key < err) lo = mid + 1;
    else           hi = mid;
  }
#else
  (void)err;
#endif
  return NULL;
}

const char* hmsLookupText(uint32_t attr, uint32_t code) {
#if HAS_FULL_HMS_TABLE
  uint64_t want = hmsKeyOf(attr, code);
  uint16_t lo = 0, hi = HMS_TABLE_COUNT;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    uint64_t key = HMS_KEYS[mid];
    if (key == want) return hmsTextById(HMS_TEXT_ID[mid]);
    if (key < want) lo = mid + 1;
    else            hi = mid;
  }
#else
  (void)attr;
  (void)code;
#endif
  return NULL;
}

// ---------------------------------------------------------------------------
//  Known-code set
// ---------------------------------------------------------------------------
// Two implementations of one question, picked by which table this board carries.
// A full-table board answers it from the key array it already has; every other
// board uses hms_known.h, the same keys grouped by attr so they cost 12 KB
// instead of 400. Both key sets come out of the same generator run.
bool hmsIsDescribed(uint32_t attr, uint32_t code) {
#if HAS_FULL_HMS_TABLE
  return hmsLookupText(attr, code) != NULL;
#elif HAS_HMS_KNOWN_TABLE
  const uint32_t sev = code >> 16;
  const uint32_t sub = code & 0xFFFF;
  // Outside what the packing can express, so it cannot be in the set. The
  // generator refuses to emit a table that would need wider fields, which is
  // what keeps this from silently swallowing a whole severity class.
  if (sev > 3 || sub > HMS_KNOWN_SUB_MASK) return false;

  uint16_t lo = 0, hi = HMS_KNOWN_ATTR_COUNT;
  while (lo < hi) {
    const uint16_t mid = lo + (hi - lo) / 2;
    const uint32_t a = HMS_KNOWN_ATTR[mid];
    if (a == attr) {
      const uint16_t want = (uint16_t)((sev << HMS_KNOWN_SEV_SHIFT) | sub);
      uint16_t clo = HMS_KNOWN_IDX[mid], chi = HMS_KNOWN_IDX[mid + 1];
      while (clo < chi) {
        const uint16_t cmid = clo + (chi - clo) / 2;
        const uint16_t c = HMS_KNOWN_CODE[cmid];
        if (c == want) return true;
        if (c < want) clo = cmid + 1;
        else          chi = cmid;
      }
      return false;
    }
    if (a < attr) lo = mid + 1;
    else          hi = mid;
  }
  return false;
#else
  // No key set compiled in: nothing can be judged, so nothing is suppressed.
  // Reachable only if a future board drops both tables - keep the errors
  // visible rather than silently blind.
  (void)attr;
  (void)code;
  return true;
#endif
}

// ---------------------------------------------------------------------------
//  Editorial mute list
// ---------------------------------------------------------------------------
// Kept as code rather than as a generated table: every entry needs a reason a
// human wrote down, and there are two of them.
//
//   0300_9600_0003_0001  door open. Measured on an H2 over three open/close
//                        cycles and present in a P2S dump alongside stat bit
//                        0x00800000. Bambu ships no wording for it.
//   0500_0400_0003_0008  "The door seems to be open." The same condition on the
//                        printers that report it this way - and this one Bambu
//                        DOES describe, so the undescribed-code rule never
//                        touches it. Without this line the door advisory would
//                        have survived the #164 fix on those printers.
//
// Not muted, on purpose: the AMS-HT front-cover codes (18xx_2400_0002_0009) are
// a different lid with no indicator of its own and a real effect on drying, and
// print_error 0300_800F actually pauses the print.
//
// The caller pairs this with doorSensorPresent - see the header. A printer that
// never publishes the `stat` bit or home_flag bit 23 draws no door indicator at
// all, so muting its only door signal would hide the door completely.
bool hmsIsMuted(uint32_t attr, uint32_t code) {
  const uint64_t key = hmsKeyOf(attr, code);
  return key == 0x0300960000030001ULL ||
         key == 0x0500040000030008ULL;
}

// ---------------------------------------------------------------------------
//  Error badge
// ---------------------------------------------------------------------------
uint16_t errorSeverityColor(uint8_t sev) {
  switch (sev) {
    case 1:  return CLR_RED;      // fatal
    case 2:  return CLR_ORANGE;   // serious
    case 3:  return CLR_YELLOW;   // common
    // info / unknown - listed, never urgent. Stays the fixed literal: this is a
    // severity ladder, and a theme that can dim its bottom rung into the
    // background can hide a reported fault.
    default: return CLR_TEXT_DIM_DEFAULT;
  }
}

ErrorBadge errorBadgeFor(const BambuState& s) {
  ErrorBadge b = { false, 0, 0, 0 };
  if (!dispSettings.hmsEnabled) return b;

  // print_error first: the job has stopped, which outranks any advisory HMS
  // still standing. A cancel is the user's own doing and never lights the
  // badge - printerWasCanceled() turns it into a state word instead.
  if (s.printError != 0 && !printErrorIsCancel(s.printError)) {
    b.active = true;
    b.severity = 1;   // print_error carries no severity field; it stopped a print
    b.code = s.printError;
    return b;
  }

  // hms[] is sorted worst-first, so the first entry that survives both filters
  // is the worst one worth showing - no second pass needed.
  for (uint8_t i = 0; i < s.hmsCount; i++) {
    const uint8_t sev = hmsSeverityOf(s.hms[i].code);
    if (sev < 1 || sev > 3) continue;                       // info/unknown: listed only
    if (sev == 3 && !dispSettings.hmsSeverityAll) continue;  // "important only"
    if (hmsIsBaseline(s, s.hms[i].attr, s.hms[i].code)) continue;
    b.active = true;
    b.severity = sev;
    b.attr = s.hms[i].attr;
    b.code = s.hms[i].code;
    return b;
  }
  return b;
}

uint32_t errorBadgeId(const BambuState& s) {
  const ErrorBadge b = errorBadgeFor(s);
  // The cancel word is part of the badge's visible identity, so it has to move
  // the id too - print_error lands one report after gcode_state goes FAILED,
  // and without this the badge would keep saying FAILED.
  const uint32_t cancelBit = printerWasCanceled(s) ? 0x40000000UL : 0;
  if (!b.active) return cancelBit;
  // Severity in the top nibble, the code folded into the rest. A collision
  // costs at most one missed repaint between two errors of equal severity.
  const uint32_t key = b.attr ^ b.code;
  return 0x80000000UL | cancelBit |
         ((uint32_t)(b.severity & 0x07) << 28) |
         ((key ^ (key >> 28)) & 0x0FFFFFFFUL);
}

const char* hmsTableVersion(void) {
#if HAS_FULL_HMS_TABLE
  return HMS_TABLE_VER;
#elif HAS_ERROR_TEXT_TABLE
  return PRINT_ERROR_TABLE_VER;
#else
  return NULL;
#endif
}

// Kept separate from hmsTableVersion(): that one names the text source, which is
// how a board's HMS build is identified, and folding the key set into it would
// make a C3 look like it had text it does not.
const char* hmsKnownVersion(void) {
#if HAS_HMS_KNOWN_TABLE
  return HMS_KNOWN_VER;
#elif HAS_FULL_HMS_TABLE
  return HMS_TABLE_VER;   // the text table's keys ARE the known set
#else
  return NULL;
#endif
}

#endif  // HAS_HMS_UI
