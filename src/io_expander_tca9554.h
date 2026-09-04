#ifndef IO_EXPANDER_TCA9554_H
#define IO_EXPANDER_TCA9554_H

#include "config.h"

#if PANEL_HAS_IO_EXPANDER

#include <stdint.h>

// Single owner of a TCA9554 output port.
//
// The chip has no per-bit write: the output register is a whole byte, so two
// independent writers silently clobber each other. On ws_lcd_28c that byte
// carries LCD reset, LCD CS, touch reset, SD CS and the buzzer at once - a
// buzzer backend writing 0x80/0x00 for its own bit would blank the panel
// mid-run. Everything therefore goes through this shadow.
//
// All callers run on the main task (display init, touch init, buzzer), so no
// locking.

// Claims the I2C bus, drives every pin as an output and latches initialOut.
// Safe to call more than once; only the first call touches the bus.
bool ioExpanderBegin(uint8_t addr, int8_t sda, int8_t scl, uint32_t freqHz,
                     uint8_t initialOut);

// Read-modify-write one bit of the shadow.
void ioExpanderSet(uint8_t bit, bool high);

// Current shadow byte (what the chip was last told), 0 before begin.
uint8_t ioExpanderOutputs();

// True once ioExpanderBegin() has acknowledged on the bus.
bool ioExpanderPresent();

#endif // PANEL_HAS_IO_EXPANDER
#endif // IO_EXPANDER_TCA9554_H
