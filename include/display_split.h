#ifndef DISPLAY_SPLIT_H
#define DISPLAY_SPLIT_H

// Split (dual-printer) screen: two printers shown at once in a top band and a
// bottom band, no rotation. Engaged by the orchestration in main.cpp when
// rotState.splitEnabled is set and two printers are active (printing or drying).
// Renders printers[rotState.displayIndex] (top) and printers[rotState.splitIndexB]
// (bottom), reusing each printer's existing gaugeSlots[] configuration.
//
// Compiled to a no-op on layout profiles that do not define LAYOUT_HAS_SPLIT
// (e.g. 480x480 SenseCAP); displaySupportsSplit() returns false there so this is
// never reached anyway.
void drawSplit();

#endif // DISPLAY_SPLIT_H
