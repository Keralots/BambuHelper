// Common base for the "Arduino_GFX inside LovyanGFX" full-frame panel
// wrappers (Panel_AXS15231B_AGFX for JC3248W535, Panel_NV3041A_AGFX for
// JC4827W543). BambuHelper renders these boards into a PSRAM sprite and
// commits the whole frame once per loop tick through pushRawPixels(); the
// display glue (display_ui.cpp) only needs this interface, not the concrete
// controller class.
#pragma once

#include <LovyanGFX.hpp>

namespace lgfx {
inline namespace v1 {

class Panel_AGFX_Frame : public Panel_Device {
public:
  // Push one contiguous RGB565 (LovyanGFX rgb565_2Byte memory order) frame of
  // panel_width * panel_height pixels in a single bus transaction.
  virtual void pushRawPixels(uint16_t* data, uint32_t length) = 0;
};

} // namespace v1
} // namespace lgfx
