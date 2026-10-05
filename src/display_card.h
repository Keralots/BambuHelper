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
// Tap stop on Ready / Print complete: opens the AMS page (true), or closes an
// open one and returns false so the tap carries on through the cycle.
bool cardAmsPeekToggle(const BambuState& s);
// Progress-bar shimmer for the card on screen; true when it drew this call.
bool tickCardShimmer();
// Scrolls a job name too long for the hero column; true when it drew this call.
bool tickCardMarquee();
// Last full Card frame (PSRAM boards only): byte-swapped RGB565, w x h.
// False when no full-frame sprite exists. Used by the /card.bmp endpoint.
bool cardFrameView(const uint16_t** buf, int16_t* w, int16_t* h);
#else
inline bool cardSkinActive() { return false; }
inline bool drawCardPrinting(PrinterSlot&, bool) { return false; }
inline bool drawCardFinished(PrinterSlot&, bool) { return false; }
inline bool drawCardIdle(PrinterSlot&, bool) { return false; }
inline bool cardAmsPeekToggle(const BambuState&) { return false; }
inline bool tickCardShimmer() { return false; }
inline bool tickCardMarquee() { return false; }
inline bool cardFrameView(const uint16_t**, int16_t*, int16_t*) { return false; }
#endif

#endif // DISPLAY_CARD_H
