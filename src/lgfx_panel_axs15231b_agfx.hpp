// LovyanGFX Panel_Device subclass that owns an internal Arduino_GFX instance
// (Arduino_ESP32QSPI + Arduino_AXS15231B) and forwards LovyanGFX's low-level
// drawing primitives to it. Lets the whole of BambuHelper keep calling
// `tft.fillRect()`, `tft.drawString()`, etc. on a LovyanGFX reference without
// changing any call sites, while the actual QSPI traffic is handled by the
// proven-working Arduino_GFX driver.
//
// Why this exists: mainline LovyanGFX has no AXS15231B panel class and our
// hand-rolled Panel_AXS15231B (src/lgfx_panel_axs15231b.hpp) never worked on
// this hardware. Arduino_GFX does work (see src/skeleton_test.cpp). This
// wrapper is "Option 3" from the jc3248w535 skill — wrap a proven external
// driver inside a LovyanGFX Panel subclass.

#pragma once

#include <LovyanGFX.hpp>
#include <Arduino_GFX_Library.h>

namespace lgfx {
inline namespace v1 {

class Panel_AXS15231B_AGFX : public Panel_Device {
public:
  Panel_AXS15231B_AGFX() {
    _cfg.memory_width  = _cfg.panel_width  = 320;
    _cfg.memory_height = _cfg.panel_height = 480;
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

  ~Panel_AXS15231B_AGFX() {
    if (_agfx) { delete _agfx; _agfx = nullptr; }
    if (_bus)  { delete _bus;  _bus  = nullptr; }
  }

  // -------------------------------------------------------------------------
  // Init / lifecycle
  // -------------------------------------------------------------------------

  bool init(bool /*use_reset*/) override {
    if (_init_done) return true;
    _bus = new Arduino_ESP32QSPI(
        45 /*CS*/, 47 /*SCK*/, 21 /*D0*/, 48 /*D1*/, 40 /*D2*/, 39 /*D3*/);
    _agfx = new Arduino_AXS15231B(
        _bus,
        -1 /*RST, software-reset only*/,
        0  /*rotation*/,
        false /*IPS — MUST be false to avoid double-inversion*/,
        320, 480);
    if (!_agfx->begin(32000000UL)) {
      delete _agfx; _agfx = nullptr;
      delete _bus;  _bus  = nullptr;
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

  // -------------------------------------------------------------------------
  // Drawing primitives — LovyanGFX calls these after clipping. Everything
  // higher-level (fillScreen, drawString, pushImage) funnels through here.
  // -------------------------------------------------------------------------

  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if (!_agfx) return;
    bool need_tx = !_in_transaction;
    if (need_tx) _agfx->startWrite();
    _agfx->writeAddrWindow(xs, ys, (xe - xs + 1), (ye - ys + 1));
    if (need_tx) _agfx->endWrite();
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y,
                           uint32_t rawcolor) override {
    if (!_agfx) return;
    _agfx->writePixel(x, y, (uint16_t)rawcolor);
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y,
                               uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    if (!_agfx) return;
    _agfx->writeFillRect(x, y, w, h, (uint16_t)rawcolor);
  }

  void writeBlock(uint32_t rawcolor, uint32_t length) override {
    if (!_agfx || length == 0) return;
    _agfx->writeRepeat((uint16_t)rawcolor, length);
  }

  void writePixels(pixelcopy_t* pc, uint32_t length, bool /*use_dma*/) override {
    if (!_agfx || length == 0) return;
    static constexpr uint32_t BUF_PIXELS = 128;
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
    bool need_tx = !_in_transaction;
    if (need_tx) _agfx->startWrite();
    _agfx->writeAddrWindow(x, y, w, h);
    writePixels(pc, (uint32_t)w * h, use_dma);
    if (need_tx) _agfx->endWrite();
  }

  // -------------------------------------------------------------------------
  // Read path — the panel is not readable in QSPI mode. Return zeros.
  // -------------------------------------------------------------------------

  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override                   { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                void*, pixelcopy_t*) override {}

  int32_t getScanLine(void) override { return 0; }

  // Command/data ops are unused — we don't own a separate bus.
  void writeCommand(uint32_t, uint_fast8_t) override {}
  void writeData(uint32_t, uint_fast8_t)    override {}

private:
  bool _init_done = false;
  bool _in_transaction = false;
  Arduino_DataBus* _bus  = nullptr;
  Arduino_TFT*     _agfx = nullptr;  // Arduino_TFT exposes writeAddrWindow/writeRepeat/writePixels
};

} // namespace v1
} // namespace lgfx
