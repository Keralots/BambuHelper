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
// Progress-bar shimmer for the card on screen; true when it drew this call.
bool tickCardShimmer();
#else
inline bool cardSkinActive() { return false; }
inline bool drawCardPrinting(PrinterSlot&, bool) { return false; }
inline bool drawCardFinished(PrinterSlot&, bool) { return false; }
inline bool drawCardIdle(PrinterSlot&, bool) { return false; }
inline bool tickCardShimmer() { return false; }
#endif

#endif // DISPLAY_CARD_H
