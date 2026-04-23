// LovyanGFX Panel_Device subclass that owns an internal Arduino_GFX instance
// (Arduino_ESP32QSPI + Arduino_AXS15231B) and forwards LovyanGFX's low-level
// drawing primitives to it. Lets the whole of BambuHelper keep calling
// `tft.fillRect()`, `tft.drawString()`, etc. on a LovyanGFX reference without
// changing any call sites, while the actual QSPI traffic is handled by the
// proven-working moononournation/Arduino_GFX driver.
//
// Why this exists: mainline LovyanGFX has no AXS15231B panel class. A
// hand-rolled one never produced correct pixels on this hardware, and
// Arduino_GFX's Arduino_AXS15231B drives the chip correctly out of the box.
// Wrapping it inside a LovyanGFX Panel subclass keeps the rest of the app
// on a single graphics API.

#pragma once

#include <LovyanGFX.hpp>
#include <Arduino_GFX_Library.h>

// ---------------------------------------------------------------------------
// Arduino_AXS15231B_QSPI — subclass of Arduino_AXS15231B that fixes two
// QSPI-mode quirks the stock driver gets wrong (both documented in the
// jc3248w535 skill's failure-mode table):
//
//   1. RASET (0x2B) must NOT be sent in QSPI mode. The panel derives rows
//      from the RAMWR/RAMWRC pixel stream itself. Sending RASET after CASET
//      causes "Only one corner displays, rest is garbage" — every draw lands
//      at the chip's RAM origin regardless of the requested (x,y). Confirmed
//      against the manufacturer's esp_lcd_axs15231b.c which only sends
//      RASET when `flags.use_qspi_interface == 0`.
//
//   2. COLMOD (0x3A) is not set by Arduino_GFX's init table. On some batches
//      the POR default is RGB666 (3-byte pixels), which misaligns against
//      our 16-bit RGB565 DMA output and produces a stripe/rainbow pattern.
//      Send 0x05 (RGB565, 16bpp) once after begin() to lock the format.
// ---------------------------------------------------------------------------
class Arduino_AXS15231B_QSPI : public Arduino_AXS15231B {
public:
  using Arduino_AXS15231B::Arduino_AXS15231B;

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    if (!Arduino_AXS15231B::begin(speed)) return false;
    // Force RGB565 / 16bpp COLMOD. Done after the base init sequence so it
    // overrides any POR default and any drift during init.
    _bus->beginWrite();
    _bus->writeC8D8(0x3A /*COLMOD*/, 0x05 /*RGB565 16bpp, AXS15231B encoding*/);
    _bus->endWrite();
    return true;
  }

  void writeAddrWindow(int16_t x, int16_t y, uint16_t w,
                       uint16_t h) override {
    if ((x != _currentX) || (w != _currentW)) {
      _currentX = x;
      _currentW = w;
      x += _xStart;
      _bus->writeC8D16D16(AXS15231B_CASET, x, x + w - 1);
    }
    // RASET intentionally skipped — QSPI mode derives y from pixel stream.
    // Cache y/h for consistency with parent expectations, but never send
    // 0x2B. The panel uses RAMWR (0x2C) in writeCommand below to reset the
    // write pointer to the top of the CASET window.
    _currentY = y;
    _currentH = h;
    _bus->writeCommand(AXS15231B_RAMWR);
  }
};

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
    if (_agfx)     { delete _agfx;     _agfx     = nullptr; }
    if (_agfx_bus) { delete _agfx_bus; _agfx_bus = nullptr; }
  }

  // -------------------------------------------------------------------------
  // Init / lifecycle
  // -------------------------------------------------------------------------

  bool init(bool /*use_reset*/) override {
    if (_init_done) return true;
    _agfx_bus = new Arduino_ESP32QSPI(
        45 /*CS*/, 47 /*SCK*/, 21 /*D0*/, 48 /*D1*/, 40 /*D2*/, 39 /*D3*/);
    _agfx = new Arduino_AXS15231B_QSPI(
        _agfx_bus,
        -1 /*RST, software-reset only*/,
        0  /*rotation*/,
        false /*IPS — MUST be false to avoid double-inversion*/,
        320, 480);
    if (!_agfx->begin(32000000UL)) {
      delete _agfx;     _agfx     = nullptr;
      delete _agfx_bus; _agfx_bus = nullptr;
      return false;
    }
    // IMPORTANT: do NOT call _agfx->fillScreen() here. Arduino_GFX's begin()
    // ends with setAddrWindow(0,0,w,h) which caches _currentX/Y/W/H. A
    // fillScreen at this point would skip the CASET/RASET re-send because
    // state already matches — and the AXS15231B appears to need those
    // explicitly re-sent after the long init sequence, or only a small sliver
    // at the chip's RAM origin paints. LovyanGFX's post-init setRotation(0)
    // call invalidates the Arduino_TFT _current* cache via Arduino_TFT::
    // setRotation (sets to 0xFFFF), which forces the next writeAddrWindow to
    // re-send CASET and RASET. After that, fills work correctly. See the
    // Arduino_TFT.cpp:137-177 for the cache logic.
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

  // Panel_Device's defaults for these four all dereference `_bus` (inherited
  // from IPanel, nullptr in this wrapper because Arduino_GFX owns its own
  // bus). Override to no-ops — Arduino_GFX flushes each draw immediately so
  // there's no deferred DMA state to wait on.
  void initDMA(void) override {}
  void waitDMA(void) override {}
  bool dmaBusy(void) override { return false; }
  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override {}

  // -------------------------------------------------------------------------
  // Drawing primitives — LovyanGFX calls these after clipping. Everything
  // higher-level (fillScreen, drawString, pushImage) funnels through here.
  // -------------------------------------------------------------------------

  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if (!_agfx) return;
    // writeAddrWindow is transaction-internal (uses the bus mid-write).
    // LovyanGFX always wraps setWindow in beginTransaction/endTransaction,
    // so the bus is already open when we get here.
    _agfx->writeAddrWindow(xs, ys, (xe - xs + 1), (ye - ys + 1));
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y,
                           uint32_t rawcolor) override {
    if (!_agfx) return;
    // Arduino_GFX's write*Preclipped variants assume we're inside an open
    // startWrite/endWrite transaction — which we always are when LovyanGFX
    // calls these, because LGFXBase wraps draws in beginTransaction which
    // we map to _agfx->startWrite().
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
    // Use a large staging buffer so a full-screen pushSprite becomes a
    // single Arduino_GFX writePixels call (one CS cycle, one RAMWRC header
    // followed by VARIABLE-CMD continuation chunks with CS held low).
    // 4096 pixels × 2 bytes = 8 KB static buf; no stack impact.
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
    // Always inside an open transaction when LovyanGFX calls us.
    _agfx->writeAddrWindow(x, y, w, h);
    writePixels(pc, (uint32_t)w * h, use_dma);
  }

  // -------------------------------------------------------------------------
  // Direct push escape-hatch: call _agfx->writePixels() with the whole
  // contiguous pixel buffer in ONE call. Arduino_ESP32QSPI::writePixels
  // then handles it as one CS cycle with a single RAMWRC header followed
  // by internal VARIABLE-flag continuation chunks. This is the only way
  // to push a large framebuffer without the chip getting confused by
  // multiple RAMWRC commands from chunked calls. Intended for full-frame
  // sprite flushes (e.g. pushing a PSRAM LGFX_Sprite as one atomic frame).
  // -------------------------------------------------------------------------
  void pushRawPixels(uint16_t* data, uint32_t length) {
    if (!_agfx || length == 0) return;
    // Arduino_GFX's MSB_32_16_16_SET byte-swaps each pixel from native LE
    // to big-endian MSB-first before DMA, which is the MIPI DCS convention
    // for 16bpp pixel data. But this chip in QSPI mode evidently reads
    // pixels LSB-first (observed: RED→BLUE, GREEN→RED, BLUE→GREEN,
    // YELLOW→MAGENTA — exactly the pattern of byte-swapped RGB565). Pre-
    // swap here to cancel Arduino_GFX's swap so the net wire byte order
    // matches what the chip expects. Restore the sprite buffer afterwards
    // so repeat pushes work.
    for (uint32_t i = 0; i < length; ++i) {
      data[i] = __builtin_bswap16(data[i]);
    }
    _agfx->startWrite();
    _agfx->writeAddrWindow(0, 0, _cfg.panel_width, _cfg.panel_height);
    _agfx->writePixels(data, length);
    _agfx->endWrite();
    for (uint32_t i = 0; i < length; ++i) {
      data[i] = __builtin_bswap16(data[i]);
    }
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

  // Alpha-blend / copy paths — not exercised by BambuHelper, and the
  // Panel_Device defaults touch `_bus`. Stub them to no-ops.
  void writeImageARGB(uint_fast16_t, uint_fast16_t,
                       uint_fast16_t, uint_fast16_t,
                       pixelcopy_t*) override {}
  void copyRect(uint_fast16_t, uint_fast16_t,
                 uint_fast16_t, uint_fast16_t,
                 uint_fast16_t, uint_fast16_t) override {}

private:
  bool _init_done = false;
  bool _in_transaction = false;
  // Renamed from `_bus` to avoid shadowing Panel_Device's protected IBus*_bus.
  Arduino_DataBus* _agfx_bus = nullptr;
  Arduino_TFT*     _agfx     = nullptr;  // exposes writeAddrWindow/writeRepeat/writePixels
};

} // namespace v1
} // namespace lgfx
