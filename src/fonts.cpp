#include "fonts.h"

// VLW font tables are huge PROGMEM blobs. Including them in a header would
// give every translation unit its own copy (each `const uint8_t name[]` at
// namespace scope has internal linkage). Defining them once in a .cpp keeps
// a single copy in flash regardless of how many places call setFont().
#include "fonts/inter_10.h"
#include "fonts/inter_14.h"
#include "fonts/inter_19.h"

static FontID currentFont = FONT_NONE;

static void applyBitmapFallback(lgfx::LovyanGFX& tft, FontID id) {
    tft.unloadFont();
    switch (id) {
        case FONT_SMALL: tft.setTextFont(1); break;  // 6x8 GLCD
        case FONT_BODY:  tft.setTextFont(2); break;  // 16px
        case FONT_LARGE: tft.setTextFont(4); break;  // 26px
        default:         tft.setTextFont(2); break;
    }
}

void setFont(lgfx::LovyanGFX& tft, FontID id) {
    if (id == currentFont) return;

    switch (id) {
        case FONT_SMALL:
            if (!tft.loadFont(inter_10)) applyBitmapFallback(tft, FONT_SMALL);
            break;
        case FONT_BODY:
            if (!tft.loadFont(inter_14)) applyBitmapFallback(tft, FONT_BODY);
            break;
        case FONT_LARGE:
            if (!tft.loadFont(inter_19)) applyBitmapFallback(tft, FONT_LARGE);
            break;
        case FONT_7SEG:
            tft.unloadFont();
            tft.setTextFont(7);
            break;
        case FONT_NONE:
        default:
            tft.unloadFont();
            break;
    }

    currentFont = id;
}
