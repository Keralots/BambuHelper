#ifndef FONTS_H
#define FONTS_H

#include <LovyanGFX.hpp>
#include "fonts/inter_10.h"
#include "fonts/inter_14.h"
#include "fonts/inter_19.h"

// Smooth VLW font identifiers — values match the old bitmap font numbers
// so existing logic (e.g. compact ? FONT_LARGE : FONT_LARGE) stays readable.
enum FontID : uint8_t {
    FONT_SMALL = 1,   // Inter 10pt  (was bitmap Font 1, 8px GLCD)
    FONT_BODY  = 2,   // Inter 14pt  (was bitmap Font 2, 16px)
    FONT_LARGE = 4,   // Inter 19pt  (was bitmap Font 4, 26px)
    FONT_7SEG  = 7,   // Built-in 7-segment (kept for clock displays)
};

inline void setFont(lgfx::LovyanGFX& tft, FontID id) {
    switch (id) {
        case FONT_SMALL: tft.loadFont(inter_10); break;
        case FONT_BODY:  tft.loadFont(inter_14); break;
        case FONT_LARGE: tft.loadFont(inter_19); break;
        case FONT_7SEG:
            tft.unloadFont();
            tft.setTextFont(7);
            break;
    }
}

#endif // FONTS_H
