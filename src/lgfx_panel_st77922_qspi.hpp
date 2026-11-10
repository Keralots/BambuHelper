// LovyanGFX Panel_Device for the ST77922 320x480 panel on the LCDwiki ES3C35P
// (3.5" ESP32-S3 module). The panel is quad-SPI with no DC pin and wants register
// writes on ONE data line but pixel data on FOUR ("1-1-4"), which neither
// LovyanGFX's SPI bus nor Arduino_GFX's Arduino_ESP32QSPI can produce (both send
// the command phase on all four lines). So this class owns the SPI device and
// talks to it through ESP-IDF's spi_master directly.
//
// Contract (same as Panel_AXS15231B_AGFX, so display_ui.cpp treats them alike):
// the panel stays at rotation 0 (native 320x480 portrait) forever; the app draws
// into a full-frame PSRAM sprite, with rotation applied to the sprite, and
// pushes it once per loop tick with pushRawPixels(). Direct drawing is limited
// to what the boot path needs (full-screen fills).
//
// Transfer format follows the board vendor's own example (github.com/ydedox/st77922,
// Example_01):
//   register write : opcode 0x02, 24-bit address = reg << 8, params on one line
//   pixel write    : opcode 0x32, address 0x003C00 (write-memory-continue),
//                    data on four lines, after a 0x2A/0x2B window
//   the window's X start and width must be multiples of 4
#pragma once

#include <LovyanGFX.hpp>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "lgfx_panel_st77922_qspi_init.h"

namespace lgfx {
inline namespace v1 {

class Panel_ST77922_QSPI : public Panel_Device {
public:
  static constexpr int PIN_CS = 10, PIN_SCLK = 12, PIN_D0 = 11, PIN_D1 = 13,
                       PIN_D2 = 14, PIN_D3 = 9;
  static constexpr uint32_t SPI_HZ = 80000000;
  // Pixels per SPI transaction (the vendor's own value); 2 bytes each.
  static constexpr int32_t CHUNK_PIXELS = 0x4000;

  Panel_ST77922_QSPI() {
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

  ~Panel_ST77922_QSPI() {
    if (_buf) heap_caps_free(_buf);
    if (_cmd) heap_caps_free(_cmd);
  }

  bool init(bool /*use_reset*/) override {
    if (_init_done) return true;
    // DMA-capable internal RAM for pixel chunks and register parameters.
    _buf = (uint16_t*)heap_caps_malloc(CHUNK_PIXELS * 2, MALLOC_CAP_DMA);
    _cmd = (uint8_t*)heap_caps_malloc(64, MALLOC_CAP_DMA);
    if (!_buf || !_cmd) {
      Serial.println("ST77922: DMA buffer alloc failed");
      return false;
    }

    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num = PIN_D0;
    buscfg.data1_io_num = PIN_D1;
    buscfg.sclk_io_num  = PIN_SCLK;
    buscfg.data2_io_num = PIN_D2;
    buscfg.data3_io_num = PIN_D3;
    buscfg.max_transfer_sz = CHUNK_PIXELS * 2 + 8;
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS |
                   SPICOMMON_BUSFLAG_QUAD;
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      Serial.printf("ST77922: spi_bus_initialize failed (%d)\n", (int)err);
      return false;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_HZ;
    devcfg.spics_io_num = PIN_CS;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size = 17;
    err = spi_bus_add_device(SPI2_HOST, &devcfg, &_dev);
    if (err != ESP_OK) {
      Serial.printf("ST77922: spi_bus_add_device failed (%d)\n", (int)err);
      return false;
    }

    const size_t n = sizeof(es3c35p_lcd_init_cmds) / sizeof(es3c35p_lcd_init_cmds[0]);
    for (size_t i = 0; i < n; i++) {
      writeReg((uint8_t)es3c35p_lcd_init_cmds[i].cmd, es3c35p_lcd_init_cmds[i].data,
               es3c35p_lcd_init_cmds[i].data_bytes);
      if (es3c35p_lcd_init_cmds[i].delay_ms) delay(es3c35p_lcd_init_cmds[i].delay_ms);
    }
    _init_done = true;
    _width  = _cfg.panel_width;
    _height = _cfg.panel_height;
    return true;
  }

  // The SPI device is owned here, so LovyanGFX's bus plumbing is unused.
  void initBus(void) override    {}
  void releaseBus(void) override {}
  void beginTransaction(void) override {}
  void endTransaction(void) override   {}

  color_depth_t setColorDepth(color_depth_t) override {
    _write_depth = color_depth_t::rgb565_2Byte;
    _read_depth  = color_depth_t::rgb565_2Byte;
    return _write_depth;
  }

  // The panel does not rotate (its MADCTL does not remap addressing) and the
  // frame sprite owns rotation, so the panel is pinned to native portrait.
  void setRotation(uint_fast8_t) override {
    _rotation = 0;
    _internal_rotation = 0;
    _width  = _cfg.panel_width;
    _height = _cfg.panel_height;
  }

  void setInvert(bool /*invert*/) override {}
  void setSleep(bool /*flg*/)     override {}
  void setPowerSave(bool /*flg*/) override {}
  void waitDisplay(void)          override {}
  bool displayBusy(void)          override { return false; }

  // Panel_Device's defaults for these dereference `_bus`, which is null here.
  void initDMA(void) override {}
  void waitDMA(void) override {}
  bool dmaBusy(void) override { return false; }
  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override {}

  // Full-screen / rectangle fill. Used by the boot path (tft.fillScreen) before
  // the frame sprite exists. The panel needs X start and width in multiples of
  // 4, so the rectangle is widened to the next multiple.
  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y,
                               uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    if (!_dev) return;
    int32_t x0 = x & ~3;
    int32_t x1 = (x + w + 3) & ~3;
    if (x1 > _cfg.panel_width) x1 = _cfg.panel_width;
    setWindow(x0, y, x1 - 1, y + h - 1);
    size_t total = (size_t)(x1 - x0) * h;
    // rawcolor is rgb565_2Byte, i.e. already in wire (MSB-first) byte order.
    const uint16_t c = (uint16_t)rawcolor;
    const size_t first = total > CHUNK_PIXELS ? CHUNK_PIXELS : total;
    for (size_t i = 0; i < first; i++) _buf[i] = c;
    while (total) {
      size_t n = total > CHUNK_PIXELS ? CHUNK_PIXELS : total;
      sendPixels(n);
      total -= n;
    }
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override {
    writeFillRectPreclipped(x, y, 1, 1, rawcolor);
  }

  // Window is only used together with the (unsupported here) direct pixel paths.
  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if (!_dev) return;
    uint8_t cx[4] = {(uint8_t)(xs >> 8), (uint8_t)xs, (uint8_t)(xe >> 8), (uint8_t)xe};
    uint8_t cy[4] = {(uint8_t)(ys >> 8), (uint8_t)ys, (uint8_t)(ye >> 8), (uint8_t)ye};
    writeReg(0x2A, cx, 4);
    writeReg(0x2B, cy, 4);
  }

  // Arbitrary-window pixel streaming is not supported: the controller's X
  // alignment rule makes it unsafe, and nothing draws this way once the frame
  // sprite is active. Stubbed so a stray call is harmless.
  void writeBlock(uint32_t, uint32_t) override {}
  void writePixels(pixelcopy_t*, uint32_t, bool) override {}
  void writeImage(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                  pixelcopy_t*, bool) override {}
  void writeImageARGB(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                      pixelcopy_t*) override {}
  void copyRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                uint_fast16_t, uint_fast16_t) override {}

  // Push a whole 320x480 frame. `data` is an LGFX 16bpp sprite buffer, whose
  // bytes are already MSB-first (rgb565_2Byte), which is exactly the wire order,
  // so it is copied through unchanged.
  void pushRawPixels(uint16_t* data, uint32_t length) {
    if (!_dev || length == 0) return;
    setWindow(0, 0, _cfg.panel_width - 1, _cfg.panel_height - 1);
    const uint16_t* src = data;
    while (length) {
      size_t n = length > (uint32_t)CHUNK_PIXELS ? CHUNK_PIXELS : length;
      memcpy(_buf, src, n * 2);   // DMA needs internal RAM; the sprite is in PSRAM
      sendPixels(n);
      src += n;
      length -= n;
    }
  }

  // The panel is write-only over this bus.
  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override                   { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                void*, pixelcopy_t*) override {}
  int32_t getScanLine(void) override { return 0; }
  void writeCommand(uint32_t, uint_fast8_t) override {}
  void writeData(uint32_t, uint_fast8_t)    override {}

private:
  // One register write: opcode 0x02, address (reg << 8), params on one line.
  void writeReg(uint8_t reg, const void* data, size_t len) {
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd = 0x02;
    t.base.addr = (uint32_t)reg << 8;
    t.command_bits = 8;
    t.address_bits = 24;
    if (len && len <= 64) {
      memcpy(_cmd, data, len);
      t.base.tx_buffer = _cmd;
      t.base.length = 8 * len;
    }
    spi_device_polling_transmit(_dev, (spi_transaction_t*)&t);
  }

  // n pixels from _buf: opcode 0x32, address 0x003C00, data on four lines.
  void sendPixels(size_t n) {
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd = 0x32;
    t.base.addr = 0x3C << 8;
    t.command_bits = 8;
    t.address_bits = 24;
    t.base.tx_buffer = _buf;
    t.base.length = n * 16;
    spi_device_polling_transmit(_dev, (spi_transaction_t*)&t);
  }

  bool _init_done = false;
  spi_device_handle_t _dev = nullptr;
  uint16_t* _buf = nullptr;
  uint8_t*  _cmd = nullptr;
};

} // namespace v1
} // namespace lgfx
