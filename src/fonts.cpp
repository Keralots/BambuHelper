#include "fonts.h"
#include <new>
#include "config.h"   // HAS_CARD_SKIN

// VLW font tables are huge PROGMEM blobs. Including them in a header would
// give every translation unit its own copy (each `const uint8_t name[]` at
// namespace scope has internal linkage). Defining them once in a .cpp keeps
// a single copy in flash regardless of how many places call setFont().
#include "fonts/inter_10.h"
#include "fonts/inter_14.h"
#include "fonts/inter_19.h"
#if defined(DISPLAY_320x480)
// jc3248w535 carries an extra ~105 KB font blob so the gauge primary value
// can render larger on the bigger canvas. Excluded from other boards to
// keep the C3 / S3 Mini flash footprints unchanged.
#include "fonts/inter_22.h"
#endif
#if defined(DISPLAY_ROUND_480) || (defined(DISPLAY_320x480) && HAS_CARD_SKIN)
// inter_20 (24 px regular): 2x body on the 480 round profile, Card body text
// at 1.5x on the 320x480 boards. 16 MB boards only.
#include "fonts/inter_20.h"
#define HAVE_INTER_20 1
#endif
#if defined(DISPLAY_ROUND_480)
// Rest of the 2x tier for the 480 round profile.
#include "fonts/inter_27.h"
#include "fonts/inter_37.h"
#endif

#if HAS_CARD_SKIN
#include "fonts/inter_card_num.h"   // ~15 KB
#include "fonts/inter_card_lbl.h"   // ~7 KB
#if defined(DISPLAY_320x480)
#include "fonts/inter_card_num_l.h" // ~23 KB
#include "fonts/inter_card_lbl_l.h" // ~10 KB
#define HAVE_CARD_L 1
#endif
#endif

static FontID currentFont = FONT_NONE;

static void applyBitmapFallback(lgfx::LovyanGFX& gfx, FontID id) {
    gfx.unloadFont();
    switch (id) {
        case FONT_SMALL:  gfx.setTextFont(1); break;  // 6x8 GLCD
        case FONT_BODY:   gfx.setTextFont(2); break;  // 16px
        case FONT_LARGE:  gfx.setTextFont(4); break;  // 26px
        case FONT_XLARGE: gfx.setTextFont(4); break;  // no bitmap equivalent
        case FONT_SMALL_2X: gfx.setTextFont(2); break;
        case FONT_BODY_2X:  gfx.setTextFont(4); break;
        case FONT_LARGE_2X: gfx.setTextFont(4); break;
        case FONT_CARD_NUM: gfx.setTextFont(4); break;
        case FONT_CARD_LBL: gfx.setTextFont(1); break;
        default:          gfx.setTextFont(2); break;
    }
}

void setFont(lgfx::LovyanGFX& gfx, FontID id) {
    if (id == currentFont) return;

    switch (id) {
        case FONT_SMALL:
            if (!gfx.loadFont(inter_10)) applyBitmapFallback(gfx, FONT_SMALL);
            break;
        case FONT_BODY:
            if (!gfx.loadFont(inter_14)) applyBitmapFallback(gfx, FONT_BODY);
            break;
        case FONT_LARGE:
            if (!gfx.loadFont(inter_19)) applyBitmapFallback(gfx, FONT_LARGE);
            break;
        case FONT_XLARGE:
#if defined(DISPLAY_320x480)
            if (!gfx.loadFont(inter_22)) applyBitmapFallback(gfx, FONT_XLARGE);
#else
            // Font blob not linked on this board - fall back to FONT_LARGE.
            if (!gfx.loadFont(inter_19)) applyBitmapFallback(gfx, FONT_LARGE);
#endif
            break;
        case FONT_SMALL_2X:
#if defined(HAVE_INTER_20)
            if (!gfx.loadFont(inter_20)) applyBitmapFallback(gfx, FONT_SMALL_2X);
#else
            if (!gfx.loadFont(inter_10)) applyBitmapFallback(gfx, FONT_SMALL);
#endif
            break;
        case FONT_BODY_2X:
#if defined(DISPLAY_ROUND_480)
            if (!gfx.loadFont(inter_27)) applyBitmapFallback(gfx, FONT_BODY_2X);
#else
            if (!gfx.loadFont(inter_14)) applyBitmapFallback(gfx, FONT_BODY);
#endif
            break;
        case FONT_LARGE_2X:
#if defined(DISPLAY_ROUND_480)
            if (!gfx.loadFont(inter_37)) applyBitmapFallback(gfx, FONT_LARGE_2X);
#else
            if (!gfx.loadFont(inter_19)) applyBitmapFallback(gfx, FONT_LARGE);
#endif
            break;
        case FONT_CARD_NUM:
#if HAS_CARD_SKIN
            if (!gfx.loadFont(inter_card_num)) applyBitmapFallback(gfx, FONT_CARD_NUM);
#else
            if (!gfx.loadFont(inter_19)) applyBitmapFallback(gfx, FONT_LARGE);
#endif
            break;
        case FONT_CARD_LBL:
#if HAS_CARD_SKIN
            if (!gfx.loadFont(inter_card_lbl)) applyBitmapFallback(gfx, FONT_CARD_LBL);
#else
            if (!gfx.loadFont(inter_10)) applyBitmapFallback(gfx, FONT_SMALL);
#endif
            break;
        case FONT_CARD_NUM_L:
        case FONT_CARD_LBL_L:
            if (!loadFontInto(gfx, id)) applyBitmapFallback(gfx, FONT_LARGE);
            break;
        case FONT_7SEG:
            gfx.unloadFont();
            gfx.setTextFont(7);
            break;
        case FONT_NONE:
        default:
            gfx.unloadFont();
            break;
    }

    currentFont = id;
}

static const uint8_t* fontData(FontID id) {
    switch (id) {
        case FONT_SMALL:  return inter_10;
        case FONT_BODY:   return inter_14;
        case FONT_LARGE:  return inter_19;
        case FONT_XLARGE:
#if defined(DISPLAY_320x480)
            return inter_22;
#else
            return inter_19;
#endif
#if defined(DISPLAY_ROUND_480)
        case FONT_SMALL_2X: return inter_20;
        case FONT_BODY_2X:  return inter_27;
        case FONT_LARGE_2X: return inter_37;
#else
#if defined(HAVE_INTER_20)
        case FONT_SMALL_2X: return inter_20;
#else
        case FONT_SMALL_2X: return inter_10;
#endif
        case FONT_BODY_2X:  return inter_14;
        case FONT_LARGE_2X: return inter_19;
#endif
#if defined(HAVE_CARD_L)
        case FONT_CARD_NUM_L: return inter_card_num_l;
        case FONT_CARD_LBL_L: return inter_card_lbl_l;
#else
        case FONT_CARD_NUM_L: return inter_19;
        case FONT_CARD_LBL_L: return inter_10;
#endif
#if HAS_CARD_SKIN
        case FONT_CARD_NUM: return inter_card_num;
        case FONT_CARD_LBL: return inter_card_lbl;
#else
        case FONT_CARD_NUM: return inter_19;
        case FONT_CARD_LBL: return inter_10;
#endif
        default:          return nullptr;
    }
}

bool loadFontInto(lgfx::LovyanGFX& gfx, FontID id) {
    const uint8_t* data = fontData(id);
    return data && gfx.loadFont(data);
}

const lgfx::IFont* cachedFont(FontID id) {
    static lgfx::PointerWrapper* src[16];
    static lgfx::VLWfont*        font[16];
    if (id >= 16) return nullptr;
    if (font[id]) return font[id];
    const uint8_t* data = fontData(id);
    if (!data) return nullptr;
    auto* w = new (std::nothrow) lgfx::PointerWrapper();
    auto* f = new (std::nothrow) lgfx::VLWfont();
    if (w && f) {
        w->set(data);
        if (f->loadFont(w)) { src[id] = w; font[id] = f; return f; }
    }
    delete f;
    delete w;
    return nullptr;
}
