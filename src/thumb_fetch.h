#ifndef THUMB_FETCH_H
#define THUMB_FETCH_H

#include <stdint.h>
#include "config.h"

// Plate thumbnail of the displayed printer's current cloud job (HAS_CARD_THUMB).
// A one-shot worker task fetches the task JSON and the plate PNG; the loop task
// decodes it, crops it to the object and scales it to the size the Card asks for.

#if HAS_CARD_THUMB
// Call every loop pass. Starts / collects fetches; cheap when idle.
void thumbService();

// Rendered thumbnail for the displayed printer at size x size, on bg. Returns
// false while none is available. Re-renders from the cached PNG when size or
// bg change; *gen changes whenever the pixels do.
bool thumbGet(int16_t size, uint16_t bg, const uint16_t** px, uint32_t* gen);
#else
inline void thumbService() {}
inline bool thumbGet(int16_t, uint16_t, const uint16_t**, uint32_t*) { return false; }
#endif

#endif // THUMB_FETCH_H
