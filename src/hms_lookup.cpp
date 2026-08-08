#include "hms_lookup.h"

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
  snprintf(out, outSize, "%04X_%04X_%04X_%04X",
           (unsigned)(attr >> 16), (unsigned)(attr & 0xFFFF),
           (unsigned)(code >> 16), (unsigned)(code & 0xFFFF));
}

void printErrorFormatCode(uint32_t err, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "%04X_%04X",
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

const char* hmsTableVersion(void) {
#if HAS_FULL_HMS_TABLE
  return HMS_TABLE_VER;
#elif HAS_ERROR_TEXT_TABLE
  return PRINT_ERROR_TABLE_VER;
#else
  return NULL;
#endif
}

#endif  // HAS_HMS_UI
