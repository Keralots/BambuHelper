#include "fonts.h"
#include <new>
#include "config.h"   // HAS_CARD_SKIN

// VLW font tables are huge PROGMEM blobs. Including them in a header would
// give every translation unit its own copy (each `const uint8_t name[]` at
// namespace scope has internal linkage). Defining them once in a .cpp keeps
// a single copy in flash regardless of how many places call setFont().
#if defined(BAMBUHELPER_ZH_CN)
#include "fonts/zh_inter_10.h"
#include "fonts/zh_inter_14.h"
#include "fonts/zh_inter_19.h"
#define inter_10 zh_inter_10
#define inter_14 zh_inter_14
#define inter_19 zh_inter_19
#else
#include "fonts/inter_10.h"
#include "fonts/inter_14.h"
#include "fonts/inter_19.h"
#endif
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
#if defined(BAMBUHELPER_ZH_CN)
#include "fonts/zh_inter_card_lbl.h"
#define inter_card_lbl zh_inter_card_lbl
#else
#include "fonts/inter_card_lbl.h"   // ~7 KB
#endif
#if defined(DISPLAY_320x480)
#include "fonts/inter_card_num_l.h" // ~23 KB
#include "fonts/inter_card_lbl_l.h" // ~10 KB
#define HAVE_CARD_L 1
#endif
#if defined(DISPLAY_240x320)
#include "fonts/inter_card_mid.h"   // ~5 KB
#define HAVE_CARD_MID 1
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
        case FONT_CARD_MID:
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
#if defined(HAVE_CARD_MID)
        case FONT_CARD_MID: return inter_card_mid;
#else
        case FONT_CARD_MID: return inter_19;
#endif
        default:          return nullptr;
    }
}

bool loadFontInto(lgfx::LovyanGFX& gfx, FontID id) {
    const uint8_t* data = fontData(id);
    return data && gfx.loadFont(data);
}

bool fontHasGlyph(FontID id, uint32_t codepoint) {
    const uint8_t* data = fontData(id);
    if (!data) return false;
    auto read32 = [](const uint8_t* p) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
    };
    uint32_t lo = 0, hi = read32(data);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t cp = read32(data + 24 + mid * 28);
        if (cp == codepoint) return true;
        if (cp < codepoint) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

static lgfx::PointerWrapper* cacheSrc[16];
static lgfx::VLWfont*        cacheFont[16];

const lgfx::IFont* cachedFont(FontID id) {
    if (id >= 16) return nullptr;
    if (cacheFont[id]) return cacheFont[id];
    const uint8_t* data = fontData(id);
    if (!data) return nullptr;
    auto* w = new (std::nothrow) lgfx::PointerWrapper();
    auto* f = new (std::nothrow) lgfx::VLWfont();
    if (w && f) {
        w->set(data);
        if (f->loadFont(w)) { cacheSrc[id] = w; cacheFont[id] = f; return f; }
    }
    delete f;
    delete w;
    return nullptr;
}

void releaseCachedFonts() {
    for (uint8_t i = 0; i < 16; i++) {
        delete cacheFont[i];   // ~VLWfont frees the glyph tables
        delete cacheSrc[i];
        cacheFont[i] = nullptr;
        cacheSrc[i] = nullptr;
    }
}
