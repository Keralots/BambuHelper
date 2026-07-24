#pragma once
// =============================================================================
//  Generic DIY display configuration (issue #153 / #151)
// =============================================================================
//
//  Resolves the DIY_* build flags into concrete pin / geometry / inversion
//  macros and validates the combination at compile time, so a hand-wired SPI
//  display is just a new boards/*.ini with no C++ edit. The matching LovyanGFX
//  panel class lives in src/lgfx_boards.h (BOARD_IS_DIY branch).
//
//  IMPORTANT: this header is pulled in by config.h, which is included almost
//  everywhere BEFORE LovyanGFX exists. It must therefore stay PURE PREPROCESSOR
//  plus one plain-C helper - no `lgfx::` types. The `using DiyPanel = ...` type
//  alias is in lgfx_boards.h, after <LovyanGFX.hpp>.
//
//  See boards/DIY-DISPLAY-HOWTO.md for the user-facing flag list.

#if defined(BOARD_IS_DIY)

// sdkconfig.h is what defines CONFIG_IDF_TARGET_*, and the reserved-pin helper
// at the bottom keys its per-chip rules off those. config.h pulls this header in
// before any Arduino/ESP header, so without this include the macros are simply
// absent and every one of those rules compiles away to nothing - a silent no-op
// that still builds clean. Verified: adding an #error on "no target visible"
// here fails the build unless this include is present. It is a pure #define
// header, so it does not break the "preprocessor only" rule stated above.
#include <sdkconfig.h>

// --- Exactly one panel driver -----------------------------------------------
// Normalise to 0/1 first: `defined()` produced BY macro expansion is undefined
// behaviour (GCC -Wexpansion-to-defined), so never `#define X (defined(A)+...)`.
#if defined(DIY_PANEL_GC9A01)
  #define DIY_HAS_GC9A01 1
#else
  #define DIY_HAS_GC9A01 0
#endif
#if defined(DIY_PANEL_ST7789)
  #define DIY_HAS_ST7789 1
#else
  #define DIY_HAS_ST7789 0
#endif
#if defined(DIY_PANEL_ILI9341)
  #define DIY_HAS_ILI9341 1
#else
  #define DIY_HAS_ILI9341 0
#endif
#if defined(DIY_PANEL_ST7796)
  #define DIY_HAS_ST7796 1
#else
  #define DIY_HAS_ST7796 0
#endif
#if (DIY_HAS_GC9A01 + DIY_HAS_ST7789 + DIY_HAS_ILI9341 + DIY_HAS_ST7796) != 1
  #error "BOARD_IS_DIY needs exactly one DIY_PANEL_* (GC9A01/ST7789/ILI9341/ST7796)"
#endif

// --- Per-driver defaults (geometry matches each panel's native GRAM) ---------
// invert is a best-guess STARTING point; flip with DIY_INVERT or the web
// "invert colours" checkbox if a module differs (it is module-specific: ES3N28P
// runs ILI9341 invert=true, the maintained ST7789 envs force invert via
// USE_ST7789_INVERT, WS350's ST7796 invert is HW-untested).
#if   defined(DIY_PANEL_GC9A01)
  #define DIY_DEF_INVERT 1
  #define DIY_DEF_PW 240
  #define DIY_DEF_PH 240
  #define DIY_DEF_MW 240
  #define DIY_DEF_MH 240
#elif defined(DIY_PANEL_ST7789)
  #define DIY_DEF_INVERT 0
  #define DIY_DEF_PW 240
  #define DIY_DEF_PH 240
  #define DIY_DEF_MW 240
  #define DIY_DEF_MH 320   // ST7789 GRAM is 240x320 even behind 240x240 glass
#elif defined(DIY_PANEL_ILI9341)
  #define DIY_DEF_INVERT 0
  #define DIY_DEF_PW 240
  #define DIY_DEF_PH 320
  #define DIY_DEF_MW 240
  #define DIY_DEF_MH 320
#elif defined(DIY_PANEL_ST7796)
  #define DIY_DEF_INVERT 1
  #define DIY_DEF_PW 320
  #define DIY_DEF_PH 480
  #define DIY_DEF_MW 320
  #define DIY_DEF_MH 480
#endif

// --- Pins: required (SCLK/MOSI/DC/CS) + optional (MISO/RST) -------------------
#if !defined(DIY_PIN_SCLK) || !defined(DIY_PIN_MOSI) || \
    !defined(DIY_PIN_DC)   || !defined(DIY_PIN_CS)
  #error "BOARD_IS_DIY needs DIY_PIN_SCLK / DIY_PIN_MOSI / DIY_PIN_DC / DIY_PIN_CS"
#endif
#ifndef DIY_PIN_MISO
  #define DIY_PIN_MISO -1
#endif
#ifndef DIY_PIN_RST
  #define DIY_PIN_RST  -1
#endif
#ifndef DIY_SPI_HOST       // SPI2_HOST works on S3/C3; classic ESP32 wants VSPI_HOST
  #define DIY_SPI_HOST SPI2_HOST
#endif
#ifndef DIY_FREQ_WRITE
  #define DIY_FREQ_WRITE 40000000
#endif

// --- Geometry / inversion (override any of these in the env) ------------------
#ifndef DIY_PANEL_W
  #define DIY_PANEL_W DIY_DEF_PW
#endif
#ifndef DIY_PANEL_H
  #define DIY_PANEL_H DIY_DEF_PH
#endif
#ifndef DIY_MEM_W
  #define DIY_MEM_W DIY_DEF_MW
#endif
#ifndef DIY_MEM_H
  #define DIY_MEM_H DIY_DEF_MH
#endif
// Where the visible glass starts inside the controller's GRAM, at rotation 0.
// Needed when the panel is smaller than the GRAM and the glass is not wired to
// its first row/column - a 240x280 ST7789 on a 240x320 die wants
// DIY_OFFSET_Y=20, and a 240x240 module can want 80 depending on which end it
// is bonded to (symptom: the image is shifted with a band of noise along one
// edge). Only rotation 0 is configured here: LovyanGFX derives the flipped
// rotations itself as memory - (panel + offset) (Panel_LCD::setRotation), so a
// correct value at rotation 0 stays correct through the web UI's rotation
// setting. Leave at 0 unless you actually see the shift.
#ifndef DIY_OFFSET_X
  #define DIY_OFFSET_X 0
#endif
#ifndef DIY_OFFSET_Y
  #define DIY_OFFSET_Y 0
#endif
#ifndef DIY_INVERT
  #define DIY_INVERT DIY_DEF_INVERT
#endif
#ifndef DIY_RGB_ORDER
  #define DIY_RGB_ORDER 0
#endif
#ifndef DIY_OFFSET_ROTATION
  #define DIY_OFFSET_ROTATION 0
#endif

// --- Driver <-> layout compatibility, EXCLUSIVE ------------------------------
// layout.h picks the FIRST matching DISPLAY_* (480x480 > round > 320x480 >
// 240x320), so a "needs X" check alone would let GC9A01 + DISPLAY_ROUND_240 +
// DISPLAY_480x480 build a 480x480 UI on a 240x240 panel. And DISPLAY_CYD must be
// rejected outright: it makes initDisplay() compile CYD _tft_storage logic that
// the DIY board selector never defines. So: forbid DISPLAY_CYD, count the
// dimensional layouts, force exactly the matching one.
#if defined(DISPLAY_CYD)
  #error "BOARD_IS_DIY is incompatible with DISPLAY_CYD"
#endif
#if defined(DISPLAY_ROUND_240)
  #define DIY_L_ROUND 1
#else
  #define DIY_L_ROUND 0
#endif
#if defined(DISPLAY_240x320)
  #define DIY_L_240320 1
#else
  #define DIY_L_240320 0
#endif
#if defined(DISPLAY_320x480)
  #define DIY_L_320480 1
#else
  #define DIY_L_320480 0
#endif
#if defined(DISPLAY_480x480)
  #define DIY_L_480480 1
#else
  #define DIY_L_480480 0
#endif
#define DIY_L_COUNT (DIY_L_ROUND + DIY_L_240320 + DIY_L_320480 + DIY_L_480480)
#if DIY_L_COUNT > 1
  #error "BOARD_IS_DIY: set at most one dimensional DISPLAY_* layout"
#endif
#if defined(DIY_PANEL_GC9A01) && !DIY_L_ROUND
  #error "DIY_PANEL_GC9A01 needs DISPLAY_ROUND_240 (and no other DISPLAY_*)"
#endif
#if defined(DIY_PANEL_ILI9341) && !DIY_L_240320
  #error "DIY_PANEL_ILI9341 needs DISPLAY_240x320"
#endif
#if defined(DIY_PANEL_ST7796) && !DIY_L_320480
  #error "DIY_PANEL_ST7796 needs DISPLAY_320x480"
#endif
#if defined(DIY_PANEL_ST7789)
  #if   (DIY_PANEL_H == 320)
    #if !DIY_L_240320
      #error "DIY_PANEL_ST7789 with DIY_PANEL_H=320 needs DISPLAY_240x320"
    #endif
  #elif (DIY_PANEL_H == 240)
    #if DIY_L_COUNT != 0
      #error "DIY_PANEL_ST7789 240x240 must set NO dimensional DISPLAY_*"
    #endif
  #else
    #error "DIY_PANEL_ST7789 supports DIY_PANEL_H of 240 or 320 only"
  #endif
#endif

// --- Visible geometry must match the chosen layout ---------------------------
// The driver<->layout checks above fix the UI to SCREEN_W x SCREEN_H. A manual
// DIY_PANEL_W/H override that disagrees (e.g. ILI9341 + DISPLAY_240x320 but
// DIY_PANEL_H=240) compiles fine yet hands LovyanGFX a panel a different size
// than the UI, silently clipping the bottom/right. Catch it at build time.
// (Guarded on defined() so the header stays usable if ever included before the
// layout; config.h defines SCREEN_W/H just before pulling this in.)
#if defined(SCREEN_W) && defined(SCREEN_H)
  #if (DIY_PANEL_W != SCREEN_W) || (DIY_PANEL_H != SCREEN_H)
    #error "BOARD_IS_DIY: DIY_PANEL_W/H must equal the layout's SCREEN_W x SCREEN_H - drop the geometry override or pick the matching DISPLAY_* layout"
  #endif
#endif

// --- The offset window must fit inside the GRAM ------------------------------
// LovyanGFX stores offset_x/y as uint16_t, so a negative silently wraps to a
// huge column start, and an offset that pushes the visible area past the GRAM
// edge shifts the image off-screen instead of correcting it. Both are the sort
// of typo this target exists to make loud rather than mysterious. Note the
// window has to fit at rotation 0 AND at the flipped rotations, but
// Panel_LCD::setRotation derives those as memory - (panel + offset), which is
// non-negative exactly when this check passes.
#if (DIY_OFFSET_X) < 0 || (DIY_OFFSET_Y) < 0
  #error "BOARD_IS_DIY: DIY_OFFSET_X/Y must be >= 0 (LovyanGFX stores them unsigned)"
#endif
#if ((DIY_OFFSET_X) + (DIY_PANEL_W)) > (DIY_MEM_W)
  #error "BOARD_IS_DIY: DIY_OFFSET_X + DIY_PANEL_W exceeds DIY_MEM_W - raise DIY_MEM_W to the controller's real GRAM width or lower the offset"
#endif
#if ((DIY_OFFSET_Y) + (DIY_PANEL_H)) > (DIY_MEM_H)
  #error "BOARD_IS_DIY: DIY_OFFSET_Y + DIY_PANEL_H exceeds DIY_MEM_H - raise DIY_MEM_H to the controller's real GRAM height or lower the offset"
#endif

// --- Reserved pins -----------------------------------------------------------
// The button / buzzer / LED sanitizers call this to refuse a pin (fresh default
// OR persisted-from-another-board OR hand-typed) that must not be driven.
//
// Two groups. First this build's display lines - every candidate is guarded
// >= 0 so an unused -1 pin never matches, and BACKLIGHT_PIN is already resolved
// by config.h before this header is included.
//
// Then the pins the CHIP cannot lend out whatever the wiring: the SPI flash /
// PSRAM bus, the native USB data lines, and anything past the last real GPIO.
// The maintained boards get these from their BOARD_IS_* branch in led.cpp, but
// a DIY env is allowed to name no chip target at all (diy_spi_template and the
// classic-ESP32 starter do not), and those envs would otherwise reach the end
// of that chain with nothing but the display pins denied - on a DevKitC header
// GPIO 6-11 are silkscreened right next to the usable ones. Keyed off the IDF
// target macros so it works with or without a BOARD_IS_* flag; a DIY env that
// does set one (diy_round240 sets BOARD_IS_C3) just gets the same answer twice.
static inline bool isDiyReservedPin(int pin) {
  if (pin < 0) return false;
  if (pin == DIY_PIN_SCLK) return true;
  if (pin == DIY_PIN_MOSI) return true;
  if (pin == DIY_PIN_DC)   return true;
  if (pin == DIY_PIN_CS)   return true;
  if (DIY_PIN_MISO  >= 0 && pin == DIY_PIN_MISO)  return true;
  if (DIY_PIN_RST   >= 0 && pin == DIY_PIN_RST)   return true;
  if (BACKLIGHT_PIN >= 0 && pin == BACKLIGHT_PIN) return true;

#if defined(CONFIG_IDF_TARGET_ESP32)
  if (pin >= 6 && pin <= 11) return true;    // SPI flash
  if (pin > 39) return true;                 // no such GPIO
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  if (pin >= 26 && pin <= 37) return true;   // SPI flash + PSRAM
  if (pin == 19 || pin == 20) return true;   // native USB D-/D+
  if (pin > 48) return true;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  if (pin >= 11 && pin <= 17) return true;   // SPI flash
  if (pin == 18 || pin == 19) return true;   // native USB D-/D+
  if (pin > 21) return true;
#endif
  return false;
}

#endif // BOARD_IS_DIY
