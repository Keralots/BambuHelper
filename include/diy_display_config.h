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

// --- Reserved display pins ---------------------------------------------------
// The button / buzzer / LED sanitizers call this to refuse a pin (fresh default
// OR persisted-from-another-board OR hand-typed) that would drive a display
// line. Every candidate is guarded >= 0 so an unused -1 pin never matches.
// BACKLIGHT_PIN is already resolved by config.h before this header is included.
static inline bool isDiyReservedPin(int pin) {
  if (pin < 0) return false;
  if (pin == DIY_PIN_SCLK) return true;
  if (pin == DIY_PIN_MOSI) return true;
  if (pin == DIY_PIN_DC)   return true;
  if (pin == DIY_PIN_CS)   return true;
  if (DIY_PIN_MISO  >= 0 && pin == DIY_PIN_MISO)  return true;
  if (DIY_PIN_RST   >= 0 && pin == DIY_PIN_RST)   return true;
  if (BACKLIGHT_PIN >= 0 && pin == BACKLIGHT_PIN) return true;
  return false;
}

#endif // BOARD_IS_DIY
