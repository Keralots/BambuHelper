#ifndef FONTS_H
#define FONTS_H

#include <LovyanGFX.hpp>

// Smooth VLW font identifiers - values match the old bitmap font numbers
// so existing logic (e.g. compact ? FONT_LARGE : FONT_LARGE) stays readable.
enum FontID : uint8_t {
    FONT_NONE   = 0,
    FONT_SMALL  = 1,   // Inter 10pt  (was bitmap Font 1, 8px GLCD)
    FONT_BODY   = 2,   // Inter 14pt  (was bitmap Font 2, 16px)
    FONT_LARGE  = 4,   // Inter 19pt  (was bitmap Font 4, 26px)
    FONT_XLARGE = 5,   // Inter 22pt - only loaded on DISPLAY_320x480; on
                       // other boards setFont(FONT_XLARGE) silently falls
                       // back to FONT_LARGE (the inter_22 blob isn't linked).
    FONT_7SEG   = 7,   // Built-in 7-segment (kept for clock displays)
    // True 2x tier, linked only on DISPLAY_ROUND_480. Elsewhere setFont()
    // falls back to the 1x face of the same rank - never to the default:
    // branch, which would leave whatever built-in font was last active.
    FONT_SMALL_2X = 8,   // Inter 20pt / 24 px
    FONT_BODY_2X  = 9,   // Inter 27pt / 32 px
    FONT_LARGE_2X = 10,  // Inter 37pt / 44 px
};

// Sets the active font. Caches the last selection - calling setFont() repeatedly
// with the same id is a no-op. If a VLW font fails to load (e.g. heap exhausted)
// the helper falls back to a built-in bitmap font of similar height instead of
// silently leaving the previous font (or Font0) selected.
// Note: parameter is named `gfx` (not `tft`) because display_ui.h defines
// a `#define tft (*tft_ptr)` macro for the JC3248W535 sprite redirection,
// which would otherwise mangle this declaration when both headers are
// included in the same translation unit.
void setFont(lgfx::LovyanGFX& gfx, FontID id);

// Load a VLW font into an arbitrary render target (e.g. an LGFX_Sprite),
// bypassing setFont()'s last-selection cache — that cache tracks the main
// panel only and would skip the load on a second target. Returns false when
// the id has no VLW blob (FONT_7SEG / FONT_NONE) or the load fails.
bool loadFontInto(lgfx::LovyanGFX& gfx, FontID id);

#endif // FONTS_H
