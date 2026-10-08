#ifndef DISPLAY_CARD_H
#define DISPLAY_CARD_H

#include "config.h"
#include "bambu_state.h"

// "Card" print-screen style (HAS_CARD_SKIN): big percent, remaining time, a
// temperature or filament row and an optional AMS column, rendered off-screen
// and pushed in one go. Each draw returns false when it could not render (no
// sprite memory); the caller then draws its classic screen instead.

#if HAS_CARD_SKIN
// Card style selected. Split view never reaches these.
bool cardSkinActive();
bool drawCardPrinting(PrinterSlot& p, bool force);
bool drawCardFinished(PrinterSlot& p, bool force);
bool drawCardIdle(PrinterSlot& p, bool force);
// Tap stop on Ready / Print complete: flips to the other view (AMS or status)
// for a while (true), or flips back and returns false so the tap carries on.
bool cardAmsPeekToggle(const BambuState& s);
// Progress-bar shimmer for the card on screen; true when it drew this call.
bool tickCardShimmer();
// Scrolls a job name too long for the hero column; true when it drew this call.
bool tickCardMarquee();
// Blinks the Ready-screen clock colon; true when it drew this call.
bool tickCardColon();
// Last full Card frame (PSRAM boards only): byte-swapped RGB565, w x h.
// False when no full-frame sprite exists. Used by the /card.bmp endpoint.
bool cardFrameView(const uint16_t** buf, int16_t* w, int16_t* h);
// Band-render boards: re-renders the band starting at y0 of the last frame.
bool cardBandView(int16_t y0, const uint16_t** buf, int16_t* w, int16_t* h, int16_t* fullH);
// Frees the band cardBandView() left allocated.
void cardBandRelease();
// Frees everything Cards holds (frame state, sprites, cached faces). Called
// while Cards is off; cheap when nothing is held.
void cardRelease();
#else
inline void cardRelease() {}
inline bool cardBandView(int16_t, const uint16_t**, int16_t*, int16_t*, int16_t*) { return false; }
inline void cardBandRelease() {}
inline bool cardSkinActive() { return false; }
inline bool drawCardPrinting(PrinterSlot&, bool) { return false; }
inline bool drawCardFinished(PrinterSlot&, bool) { return false; }
inline bool drawCardIdle(PrinterSlot&, bool) { return false; }
inline bool cardAmsPeekToggle(const BambuState&) { return false; }
inline bool tickCardShimmer() { return false; }
inline bool tickCardMarquee() { return false; }
inline bool tickCardColon() { return false; }
inline bool cardFrameView(const uint16_t**, int16_t*, int16_t*) { return false; }
#endif

#endif // DISPLAY_CARD_H
