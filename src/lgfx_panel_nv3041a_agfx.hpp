// LovyanGFX Panel_Device subclass wrapping moononournation/Arduino_GFX's
// Arduino_NV3041A QSPI driver for the Guition JC4827W543 (4.3" 480x272).
//
// Same idea as lgfx_panel_axs15231b_agfx.hpp: mainline LovyanGFX has no QSPI
// bus / NV3041A panel, Arduino_GFX drives the chip correctly out of the box,
// so we let Arduino_GFX own the bus and forward LovyanGFX's clipped
// primitives to it. BambuHelper then renders into a full-frame PSRAM sprite
// and flushes it with pushRawPixels() once per loop tick.
//
// Differences from the AXS15231B wrapper:
//   - The NV3041A honours CASET *and* RASET in QSPI mode, so the stock
//     Arduino_NV3041A::writeAddrWindow is used unmodified (no RASET-skip hack).
//   - Panel is landscape-native (480 wide x 272 tall). Only 0/180 degree
//     rotations are usable; display_ui.cpp folds 90/270 back to 0/180.
//   - No TE (tear-effect) line is broken out on this board (GPIO38 is the
//     touch reset), so frames are pushed unsynchronised. At ~260 KB per frame
//     over 40 MHz QIO a push takes ~13 ms; tearing is not visible in practice
//     on the mostly static dashboard.
//   - `ips = true` in the Arduino_GFX constructor: the NV3041A panel on this
//     board needs colour inversion, otherwise the dark theme renders as a
//     white screen.
#pragma once

#include <LovyanGFX.hpp>
#include <Arduino_GFX_Library.h>
#include "lgfx_panel_agfx_frame.hpp"

namespace lgfx {
inline namespace v1 {

class Panel_NV3041A_AGFX : public Panel_AGFX_Frame {
public:
  static constexpr uint16_t PANEL_W = 480;
  static constexpr uint16_t PANEL_H = 272;

  Panel_NV3041A_AGFX() {
    _cfg.memory_width  = _cfg.panel_width  = PANEL_W;
    _cfg.memory_height = _cfg.panel_height = PANEL_H;
    _cfg.offset_x = 0;
    _cfg.offset_y = 0;
    _cfg.offset_rotation = 0;
    _cfg.dummy_read_pixel = 0;
    _cfg.dummy_read_bits  = 0;
    _cfg.readable         = false;
    _cfg.invert           = false;
    _cfg.rgb_order        = false;
    _cfg.dlen_16bit       = false;
    _cfg.bus_shared       = false;
    _write_depth = color_depth_t::rgb565_2Byte;
    _read_depth  = color_depth_t::rgb565_2Byte;
  }

  ~Panel_NV3041A_AGFX() {
    if (_agfx)     { delete _agfx;     _agfx     = nullptr; }
    if (_agfx_bus) { delete _agfx_bus; _agfx_bus = nullptr; }
  }

  // -------------------------------------------------------------------------
  // Init / lifecycle
  // -------------------------------------------------------------------------
  bool init(bool /*use_reset*/) override {
    if (_init_done) return true;
    // Verified JC4827W543 QSPI map (identical to the JC3248W535).
    _agfx_bus = new Arduino_ESP32QSPI(
        45 /*CS*/, 47 /*SCK*/, 21 /*D0*/, 48 /*D1*/, 40 /*D2*/, 39 /*D3*/);
    _agfx = new Arduino_NV3041A(
        _agfx_bus,
        GFX_NOT_DEFINED /*RST - no reset GPIO, software reset only*/,
        0    /*rotation*/,
        true /*IPS -> inverted colours, required on this panel*/);
    if (!_agfx->begin(40000000UL)) {
      delete _agfx;     _agfx     = nullptr;
      delete _agfx_bus; _agfx_bus = nullptr;
      return false;
    }
    _init_done = true;
    _width  = _cfg.panel_width;
    _height = _cfg.panel_height;
    return true;
  }

  // Arduino_GFX owns its bus entirely, so LovyanGFX's bus plumbing is unused.
  void initBus(void) override    {}
  void releaseBus(void) override {}

  void beginTransaction(void) override {
    if (_agfx && !_in_transaction) {
      _agfx->startWrite();
      _in_transaction = true;
    }
  }

  void endTransaction(void) override {
    if (_agfx && _in_transaction) {
      _agfx->endWrite();
      _in_transaction = false;
    }
  }

  color_depth_t setColorDepth(color_depth_t) override {
    _write_depth = color_depth_t::rgb565_2Byte;
    _read_depth  = color_depth_t::rgb565_2Byte;
    return _write_depth;
  }

  // The panel MADCTL is left at rotation 0 for the whole session; user-facing
  // rotation is applied on the PSRAM sprite (display_ui.cpp). We still track
  // the logical size so tft.width()/height() stay consistent if some code
  // path ever draws directly to the panel.
  void setRotation(uint_fast8_t r) override {
    r &= 3;
    _rotation = r;
    _internal_rotation = r;
    if (_agfx) _agfx->setRotation(r);
    _width  = (r & 1) ? _cfg.panel_height : _cfg.panel_width;
    _height = (r & 1) ? _cfg.panel_width  : _cfg.panel_height;
  }

  void setInvert(bool /*invert*/) override {}
  void setSleep(bool /*flg*/)     override {}
  void setPowerSave(bool /*flg*/) override {}
  void waitDisplay(void)          override {}
  bool displayBusy(void)          override { return false; }

  // Panel_Device defaults for these dereference `_bus`, which is nullptr here.
  void initDMA(void) override {}
  void waitDMA(void) override {}
  bool dmaBusy(void) override { return false; }
  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override {}

  // -------------------------------------------------------------------------
  // Drawing primitives (only exercised if the frame sprite could not be
  // allocated; normally everything goes through pushRawPixels).
  // -------------------------------------------------------------------------
  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if (!_agfx) return;
    _agfx->writeAddrWindow(xs, ys, (xe - xs + 1), (ye - ys + 1));
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y,
                           uint32_t rawcolor) override {
    if (!_agfx) return;
    _agfx->writePixelPreclipped(x, y, (uint16_t)rawcolor);
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y,
                               uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    if (!_agfx) return;
    _agfx->writeFillRectPreclipped(x, y, w, h, (uint16_t)rawcolor);
  }

  void writeBlock(uint32_t rawcolor, uint32_t length) override {
    if (!_agfx || length == 0) return;
    _agfx->writeRepeat((uint16_t)rawcolor, length);
  }

  void writePixels(pixelcopy_t* pc, uint32_t length, bool /*use_dma*/) override {
    if (!_agfx || length == 0) return;
    static constexpr uint32_t BUF_PIXELS = 4096;
    static uint16_t buf[BUF_PIXELS];
    while (length > 0) {
      uint32_t n = length > BUF_PIXELS ? BUF_PIXELS : length;
      pc->fp_copy(buf, 0, n, pc);
      _agfx->writePixels(buf, n);
      length -= n;
    }
  }

  void writeImage(uint_fast16_t x, uint_fast16_t y,
                  uint_fast16_t w, uint_fast16_t h,
                  pixelcopy_t* pc, bool use_dma) override {
    if (!_agfx || w == 0 || h == 0) return;
    _agfx->writeAddrWindow(x, y, w, h);
    writePixels(pc, (uint32_t)w * h, use_dma);
  }

  // Full-frame push: one CS cycle, RAMWRC header, continuation chunks with CS
  // held low (Arduino_ESP32QSPI::writeBytes). The sprite buffer is already in
  // the panel's byte order (LovyanGFX rgb565_2Byte), so no swapping is done.
  void pushRawPixels(uint16_t* data, uint32_t length) override {
    if (!_agfx || length == 0) return;
    _agfx->startWrite();
    _agfx->writeAddrWindow(0, 0, _cfg.panel_width, _cfg.panel_height);
    _agfx->writeBytes(reinterpret_cast<uint8_t*>(data), length * sizeof(uint16_t));
    _agfx->endWrite();
  }

  // -------------------------------------------------------------------------
  // Read path — not readable over QSPI. Return zeros.
  // -------------------------------------------------------------------------
  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override                   { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                void*, pixelcopy_t*) override {}
  int32_t getScanLine(void) override { return 0; }

  void writeCommand(uint32_t, uint_fast8_t) override {}
  void writeData(uint32_t, uint_fast8_t)    override {}

  void writeImageARGB(uint_fast16_t, uint_fast16_t,
                      uint_fast16_t, uint_fast16_t,
                      pixelcopy_t*) override {}
  void copyRect(uint_fast16_t, uint_fast16_t,
                uint_fast16_t, uint_fast16_t,
                uint_fast16_t, uint_fast16_t) override {}

private:
  bool _init_done = false;
  bool _in_transaction = false;
  Arduino_DataBus* _agfx_bus = nullptr;
  Arduino_TFT*     _agfx     = nullptr;
};

} // namespace v1
} // namespace lgfx
