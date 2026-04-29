#ifndef FONTS_H
#define FONTS_H

#include <LovyanGFX.hpp>

// Smooth VLW font identifiers - values match the old bitmap font numbers
// so existing logic (e.g. compact ? FONT_LARGE : FONT_LARGE) stays readable.
enum FontID : uint8_t {
    FONT_NONE  = 0,
    FONT_SMALL = 1,   // Inter 10pt  (was bitmap Font 1, 8px GLCD)
    FONT_BODY  = 2,   // Inter 14pt  (was bitmap Font 2, 16px)
    FONT_LARGE = 4,   // Inter 19pt  (was bitmap Font 4, 26px)
    FONT_7SEG  = 7,   // Built-in 7-segment (kept for clock displays)
};

// Sets the active font. Caches the last selection - calling setFont() repeatedly
// with the same id is a no-op. If a VLW font fails to load (e.g. heap exhausted)
// the helper falls back to a built-in bitmap font of similar height instead of
// silently leaving the previous font (or Font0) selected.
void setFont(lgfx::LovyanGFX& tft, FontID id);

#endif // FONTS_H
