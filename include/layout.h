#ifndef LAYOUT_H
#define LAYOUT_H

// Layout profile dispatcher.
// Each display target defines LY_* constants for screen dimensions,
// gauge positions, text positions, etc.
// To add a new display: create layout_xxx.h and add an #elif here.
//
// Raw -D tests only. config.h includes this header (config.h:18) BEFORE it
// derives DISPLAY_IS_ROUND, so that capability is still undefined here and
// would silently select layout_default.h.

#if defined(DISPLAY_480x480)
  #include "layout_480x480.h"    // SenseCAP Indicator: ST7701S 480x480
#elif defined(DISPLAY_ROUND_480)
  #include "layout_round480.h"  // ST7701 2.8" round 480x480
#elif defined(DISPLAY_ROUND_240)
  #include "layout_round240.h"  // GC9A01 1.28" round 240x240
#elif defined(DISPLAY_320x480)
  #include "layout_320x480.h"   // 320x480 portrait (Guition JC3248W535)
#elif defined(DISPLAY_240x320)
  #include "layout_240x320.h"   // 240x320 portrait (CYD, Waveshare)
#else
  #include "layout_default.h"   // ESP32-S3 Mini: ST7789 240x240
#endif

// Profile-overridable UI scale and text tier, for the render code that is
// shared across profiles (the round dashboard, and the splash / AP / OTA /
// connecting screens that have no per-profile variant at all). LY_SC() wraps
// in-line pixel geometry - band clears, paddings, dot radii - that has no
// name worth inventing. Raster/AA safety margins are NOT wrapped: they stay
// 1-2 physical pixels at any size.
//
// Only layout_round480.h overrides these today; every other profile is 1x and
// its generated code is unchanged.
#ifndef LY_SC
#define LY_SC(v)  (v)
#endif
#ifndef LY_F_SMALL
#define LY_F_SMALL  FONT_SMALL
#define LY_F_BODY   FONT_BODY
#define LY_F_LARGE  FONT_LARGE
#endif

// Splash screen (initDisplay) - the one shared screen with no layout
// constants of its own. Offsets are from the canvas center.
#ifndef LY_SPLASH_TITLE_DY
#define LY_SPLASH_TITLE_DY  LY_SC(-20)
#define LY_SPLASH_SUB_DY    LY_SC(10)
#define LY_SPLASH_VER_DY    LY_SC(30)
#endif

// Edge-glow Storm: rim length per lightning-bolt segment, as a shift.
// A visual scale, so it grows with the panel - a plain "3-digit literal"
// sweep would never have found it.
#ifndef LY_GLOW_STORM_SEG_SHIFT
#define LY_GLOW_STORM_SEG_SHIFT  3   // 8 px
#endif

// Drawn footprint of the 16x16 1-bit icons (src/icons.h block-scales them).
#define LY_ICON16  LY_SC(16)

#endif // LAYOUT_H
