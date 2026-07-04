#pragma once
#include <stdint.h>

// Lifecycle
void  tasmotaInit();

// Mapping helpers
//   tasmotaPlugForPrinterSlot(slot)  - STRICT: returns the plug index that
//     should record print-state changes for printer `slot`, or 0xFF if none.
//     On single-plug builds with assignedSlot==255 ("Any"), only slot 0 is
//     considered canonical (avoids double-marking under dualPrinterUnsafe).
//   tasmotaPrinterSlotForPlug(plug)  - inverse, used by auto-off.
uint8_t tasmotaPlugForPrinterSlot(uint8_t slot);
uint8_t tasmotaPrinterSlotForPlug(uint8_t plug);

// LOOSE control mapping: the plug a printer `slot` should power-control from the
// device button. Matches the visibility mapping used by the watts gauge, so
// "gauge visible" and "can power-control" stay coherent on single-plug ("Any")
// builds where the strict mapping returns 0xFF on slots 1-3. 0xFF if none.
uint8_t tasmotaControlPlugForSlot(uint8_t slot);

// Display-side accessors. These keep all plug-mapping policy inside tasmota.cpp.
//   tasmotaIsActiveForSlot - LOOSE visibility + strict freshness gate
//   tasmotaGetWattsForSlot - LOOSE mapping; 0 when not active
//   tasmotaDisplayModeForSlot - LOOSE mapping; defaults to 0 (alternate)
//   tasmotaGetPrintKwhUsedForSlot - STRICT mapping; -1 when no print recorded
//   tasmotaTariffForSlot - returns the global tariff (slot ignored)
//   tasmotaCurrencySymbol - global UTF-8 currency symbol
bool        tasmotaIsActiveForSlot(uint8_t slot);
// LOOSE configuration check (enabled + assignment match, ignores freshness).
// Used by the web UI to decide whether the Power gauge option is offered for a
// slot, since a plug may be offline at config time.
bool        tasmotaConfiguredForSlot(uint8_t slot);
float       tasmotaGetWattsForSlot(uint8_t slot);
uint8_t     tasmotaDisplayModeForSlot(uint8_t slot);
float       tasmotaGetPrintKwhUsedForSlot(uint8_t slot);
float       tasmotaTariffForSlot(uint8_t slot);
const char* tasmotaCurrencySymbol();

// Latched "kWh value changed since last call" signal, scoped to a printer slot
// (uses strict mapping). Returns true (once) when the strict-owner plug's
// printUsedKwh has changed.
bool tasmotaKwhChangedForSlot(uint8_t slot);

// State-change API - called from main.cpp's per-slot transition handler.
// Use tasmotaPlugForPrinterSlot(slot) to derive the plug index.
void tasmotaMarkPrintStart(uint8_t plug);
void tasmotaMarkPrintEnd(uint8_t plug);

// Manual relay control from the web UI. Returns true on HTTP 200.
bool tasmotaSetPower(uint8_t plug, bool on);

// Stats accessor used by web /power/stats. Returns NaN-like sentinels (-1) for
// fields the plug has not reported yet.
struct TasmotaPlugStatsView {
  bool  online;
  float watts;
  float todayKwh;
  float totalKwh;
  float printUsedKwh;
  bool  powerStateKnown;   // true when the plug reports relay state (Shelly output)
  bool  powerOn;           // valid only when powerStateKnown
};
void tasmotaGetStats(uint8_t plug, TasmotaPlugStatsView* out);
