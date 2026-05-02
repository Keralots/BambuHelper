#ifndef LAYOUT_H
#define LAYOUT_H

// Layout profile dispatcher.
// Each display target defines LY_* constants for screen dimensions,
// gauge positions, text positions, etc.
// To add a new display: create layout_xxx.h and add an #elif here.

#if defined(DISPLAY_480x480)
  #include "layout_480x480.h"    // SenseCAP Indicator: ST7701S 480x480
#elif defined(DISPLAY_240x320)
  #include "layout_240x320.h"   // 240x320 portrait (CYD, Waveshare)
#else
  #include "layout_default.h"   // ESP32-S3 Mini: ST7789 240x240
#endif

#endif // LAYOUT_H
