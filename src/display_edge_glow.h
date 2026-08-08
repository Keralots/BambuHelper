#ifndef DISPLAY_EDGE_GLOW_H
#define DISPLAY_EDGE_GLOW_H

#include <Arduino.h>
#include <LovyanGFX.hpp>

// Edge glow: animated border light announcing print complete / print failed.
// The controller latches per-slot events from the gcode-edge handler and only
// animates while the display loop reports that slot on an eligible screen
// (finished, or the printing screen kept up after finish / showing FAILED).
// Round displays get no border band; the whole module compiles to no-ops
// there (ring variant is a planned follow-up).

enum GlowEvent : uint8_t {
  GLOW_EV_FINISH = 0,   // color from settings (single color / rainbow)
  GLOW_EV_FAILED = 1,   // always red, settings only pick style/duration
  GLOW_EV_ERROR  = 2,   // printer error; caller passes the severity colour
};

// Latch an event for a printer slot. No-op when the effect is disabled.
// `color` is only read for GLOW_EV_ERROR, whose hue is the error's severity
// rather than anything the user picked. A pending FINISH or FAILED is never
// downgraded to an error - those announce the outcome of a whole print and
// are the rarer, bigger news.
void glowNotifyEvent(uint8_t slot, GlowEvent ev, uint16_t color = 0);

// True while the armed or animating episode is a printer error. The eligible
// screen set is wider for those: an error can be raised on a printer that is
// merely idle, and opening the error screen must not cancel the very glow that
// announced it.
bool glowIsErrorEpisode();

// Drop a slot's latch (its printer left FINISH/FAILED); also stops the
// animation if that slot is the one on screen.
void glowClearSlot(uint8_t slot);

// Stop the animation and clear the on-screen slot's latch (user input,
// screen change, glow disabled live).
void glowDismiss();

// True while the band is being drawn - used to speed up the display tick and
// to suppress the top LED progress bar / shimmer that share the top edge.
bool glowIsActive();

// True while an episode is armed at all, including the dark pause between
// reminder pulses (PHASE_REMIND, nothing on screen). Dismissal triggers
// (button press, screen change) must check this one - glowIsActive() would
// let a reminder survive a dismissal that lands in the pause.
bool glowIsArmed();

// Advance + draw. Called every display-loop pass while `slot` is displayed on
// an eligible screen; paces itself internally (GLOW_ANIM_MS). `force` repaints
// the band immediately (after the base screen repainted over it). Returns true
// when pixels were pushed.
bool glowTick(lgfx::LovyanGFX& gfx, uint8_t slot, bool force);

// One-shot: true once after the animation stopped, so the display loop clears
// the band and forces a clean base-screen repaint.
bool glowConsumeCleanup();

// Web UI "Test" button: run one short preview episode (~5 s) of the configured
// effect on whatever screen is currently shown, then clean up as usual.
void glowStartTest(uint8_t slot);
bool glowTestRunning();

#endif // DISPLAY_EDGE_GLOW_H
