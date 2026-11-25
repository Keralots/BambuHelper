#include "hms_lookup.h"
#include "settings.h"   // dispSettings: master opt-out + severity filter

#if HAS_HMS_UI

// The generated tables are huge PROGMEM blobs with internal linkage, so they
// are included exactly once - here - the same way fonts.cpp owns the VLW data.
// Regenerate both with: python tools/gen_error_tables.py
#if HAS_FULL_HMS_TABLE || HAS_HMS_KNOWN_TABLE
#include "error_tables/error_models.h"
#endif
#if HAS_ERROR_TEXT_TABLE && !HAS_FULL_HMS_TABLE
#include "error_tables/print_error_lite.h"
#endif
#if HAS_FULL_HMS_TABLE
#include "error_tables/hms_table.h"
#include "error_tables/print_error_table.h"
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
  // Module bytes of every per-model feed (ver 202611242216). Labels come from
  // the sentences those codes carry - do not add one we cannot source.
  switch ((attr >> 24) & 0xFF) {
    case 0x07:                    // AMS
    case 0x12:                    // AMS lite
    case 0x18:                    // AMS-HT
    case 0x2A: return "AMS";      // AMS 3H / 3P
    case 0x05: return "MAIN";     // mainboard
    case 0x03: return "MC";       // motion controller
    case 0x0C: return "XCAM";     // xcam / lidar
    case 0x08: return "TOOL";     // H3C/P3C toolhead (LockTool, FlexFeed motors)
    case 0x1A: return "RACK";     // hotend / tool rack
    case 0x29: return "FILTER";   // air filter
    case 0x2E: return "DRYER";    // P3C drying module
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

#if HAS_FULL_HMS_TABLE || HAS_HMS_KNOWN_TABLE
// Mask bits for a printer: its model's bit, or every bit when the prefix is
// unknown (a printer newer than this build) - showing beats hiding there.
static uint32_t modelBits(const char* serial) {
  if (serial) {
    for (uint8_t i = 0; i < ERROR_MODEL_COUNT; i++)
      if (strncmp(serial, ERROR_MODEL_SN[i], 3) == 0) return 1UL << ERROR_MODEL_IDX[i];
  }
  return 0xFFFFFFFFUL;
}
#endif

// First row whose key is not below `want`. Rows of one key are adjacent.
template <typename K>
static uint16_t lowerBound(const K* keys, uint16_t count, K want) {
  uint16_t lo = 0, hi = count;
  while (lo < hi) {
    const uint16_t mid = lo + (hi - lo) / 2;
    if (keys[mid] < want) lo = mid + 1;
    else                  hi = mid;
  }
  return lo;
}

#if HAS_FULL_HMS_TABLE
// Row for this model, or -1. `first` gets the code's default row, or -1.
template <typename K>
static int32_t findRow(const K* keys, const uint8_t* maskId, const uint32_t* masks,
                       uint16_t count, K want, uint32_t bits, int32_t* first) {
  uint16_t i = lowerBound<K>(keys, count, want);
  *first = (i < count && keys[i] == want) ? (int32_t)i : -1;
  for (; i < count && keys[i] == want; i++)
    if (masks[maskId[i]] & bits) return i;
  return -1;
}
#endif

const char* printErrorLookupText(const char* serial, uint32_t err) {
  if (err == 0) return NULL;
#if HAS_FULL_HMS_TABLE
  int32_t first;
  int32_t row = findRow<uint32_t>(PRINT_ERROR_KEYS, PRINT_ERROR_MASK_ID, PRINT_ERROR_MASKS,
                                  PRINT_ERROR_TABLE_COUNT, err, modelBits(serial), &first);
  if (row < 0) row = first;
  return row < 0 ? NULL : printErrorTextById(PRINT_ERROR_TEXT_ID[row]);
#elif HAS_ERROR_TEXT_TABLE
  (void)serial;   // one wording per code on a 4 MB board
  const uint16_t i = lowerBound<uint32_t>(PRINT_ERROR_KEYS, PRINT_ERROR_TABLE_COUNT, err);
  return (i < PRINT_ERROR_TABLE_COUNT && PRINT_ERROR_KEYS[i] == err)
             ? printErrorTextById(PRINT_ERROR_TEXT_ID[i]) : NULL;
#else
  (void)serial;
  return NULL;
#endif
}

const char* hmsLookupText(const char* serial, uint32_t attr, uint32_t code) {
#if HAS_FULL_HMS_TABLE
  int32_t first;
  int32_t row = findRow<uint64_t>(HMS_KEYS, HMS_MASK_ID, HMS_MASKS, HMS_TABLE_COUNT,
                                  hmsKeyOf(attr, code), modelBits(serial), &first);
  if (row < 0) row = first;
  return row < 0 ? NULL : hmsTextById(HMS_TEXT_ID[row]);
#else
  (void)serial;
  (void)attr;
  (void)code;
  return NULL;
#endif
}

// ---------------------------------------------------------------------------
//  Known-code set
// ---------------------------------------------------------------------------
// Two implementations of one question, picked by which table this board carries.
// A full-table board answers it from the key array it already has; every other
// board uses hms_known.h, ~6 KB. Both come out of the same generator run.
bool hmsIsDescribed(const char* serial, uint32_t attr, uint32_t code) {
  if (hmsSeverityOf(code) == 1) return true;
#if HAS_FULL_HMS_TABLE
  int32_t first;
  return findRow<uint64_t>(HMS_KEYS, HMS_MASK_ID, HMS_MASKS, HMS_TABLE_COUNT,
                           hmsKeyOf(attr, code), modelBits(serial), &first) >= 0;
#elif HAS_HMS_KNOWN_TABLE
  const uint32_t sev = code >> 16;
  const uint32_t sub = code & 0xFFFF;
  // Outside what the packing can express, so it cannot be in the set. The
  // generator refuses to emit a table that would need wider fields, which is
  // what keeps this from silently swallowing a whole severity class.
  if (sev > 3 || sub > HMS_KNOWN_SUB_MASK) return false;

  const uint16_t a = lowerBound<uint32_t>(HMS_KNOWN_ATTR, HMS_KNOWN_ATTR_COUNT, attr);
  if (a >= HMS_KNOWN_ATTR_COUNT || HMS_KNOWN_ATTR[a] != attr) return false;
  const uint16_t start = HMS_KNOWN_IDX[HMS_KNOWN_RUN[a]];
  const uint16_t len   = HMS_KNOWN_IDX[HMS_KNOWN_RUN[a] + 1] - start;
  const uint16_t want  = (uint16_t)((sev << HMS_KNOWN_SEV_SHIFT) | sub);
  const uint16_t i = lowerBound<uint16_t>(HMS_KNOWN_CODE + start, len, want);
  if (i >= len || HMS_KNOWN_CODE[start + i] != want) return false;
  return (HMS_KNOWN_MASKS[HMS_KNOWN_MASK_ID[start + i]] & modelBits(serial)) != 0;
#else
  // No key set compiled in: nothing can be judged, so nothing is suppressed.
  // Reachable only if a future board drops both tables - keep the errors
  // visible rather than silently blind.
  (void)serial;
  (void)attr;
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
//  Self-resolved advisories
// ---------------------------------------------------------------------------
// AMS runout pair, sev 3: 0003_0001 "wait while purged" + 0003_0002 "auto
// switched to the slot with the same filament". Not faults, and auto-refill
// leaves the slot empty so 0003_0002 stands for the rest of the job (#181).
// Match the slot byte, not module+code: the same 0003_0001 on a unit-level
// attr is "AMS disabled" (0x51) / "dry cooling" (0x56) / "chamber hot" (0x97).
// Slots 0x20-0x23 = AMS A-D slots 1-4 and AMS lite (0x12); AMS-HT A-H are always
// 0x20; AMS 3H/3P (0x2A) has up to nine, 0x20-0x28.
bool hmsIsSelfResolved(uint32_t attr, uint32_t code) {
  if (code != 0x00030001UL && code != 0x00030002UL) return false;
  const uint32_t module = attr >> 24;
  const uint32_t slot = (attr >> 8) & 0xFFUL;
  switch (module) {
    case 0x07UL: case 0x12UL: case 0x18UL: return slot >= 0x20UL && slot <= 0x23UL;
    case 0x2AUL:                           return slot >= 0x20UL && slot <= 0x28UL;
    default:                               return false;
  }
}

// ---------------------------------------------------------------------------
//  Refused command
// ---------------------------------------------------------------------------
// 0500_0500_0001_0007 "MQTT Command verification failed". Only control commands
// are verified - status pushes are exempt - so this is whatever else talks MQTT
// to the printer, retrying. Severity 1 is why it beat every other filter (#185).
bool hmsIsRejectedCommand(uint32_t attr, uint32_t code) {
  return hmsKeyOf(attr, code) == 0x0500050000010007ULL;
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

  // hms[] is sorted worst-first, so the first entry that survives the filters
  // is the worst one worth showing - no second pass needed.
  for (uint8_t i = 0; i < s.hmsCount; i++) {
    const uint8_t sev = hmsSeverityOf(s.hms[i].code);
    if (sev < 1 || sev > 3) continue;                       // info/unknown: listed only
    if (sev == 3 && !dispSettings.hmsSeverityAll) continue;  // "important only"
    if (hmsIsBaseline(s, s.hms[i].attr, s.hms[i].code)) continue;
    // Listed, never alerts - the printer already handled it (issue #181).
    if (hmsIsSelfResolved(s.hms[i].attr, s.hms[i].code)) continue;
    // Listed, never alerts - another program's refused command, nothing to act
    // on here (issue #185). Ours is the exception and still alerts.
    if (hmsIsRejectedCommand(s.hms[i].attr, s.hms[i].code) &&
        !s.hmsOwnCmdRejected) continue;
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
