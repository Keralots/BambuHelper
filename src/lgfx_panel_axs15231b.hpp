// AXS15231B QSPI panel driver for LovyanGFX (ESP32-S3).
//
// Mainline LovyanGFX has no Panel_AXS15231B (issue #699). The chip requires
// real 4-line QSPI for pixel data — single-wire MOSI works for init but the
// controller switches to quad-mode input after the RAMWR/RAMWRCONT header
// and stops accepting data on D0 alone. Arduino_GFX's Arduino_ESP32QSPI
// uses ESP-IDF's spi_master with SPI_TRANS_MODE_QIO on the data path; we do
// the same here. This class inherits Panel_LCD so LovyanGFX's graphics
// layer (fillRect, drawString, smoothArc, sprites, VLW fonts) is unchanged.
//
// Protocol:
//   Commands: cmd=0x02, addr={0x00,cmd,0x00} (24 bits), single-line on D0.
//             Optional byte-data is sent single-line on D0 after the header.
//   Pixels:   cmd=0x32, addr={0x00,0x3C,0x00} (24 bits), QIO on D0..D3.
//             (0x3C = RAMWRCONT, appended after the 0x2C RAMWR that
//             setWindow() issues.)

#pragma once

#if defined(ESP_PLATFORM)

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include <lgfx/v1/misc/pixelcopy.hpp>
#include <lgfx/v1/misc/colortype.hpp>

#include <Arduino.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <hal/spi_types.h>
#include <string.h>

namespace lgfx {
inline namespace v1 {

struct Panel_AXS15231B : public Panel_LCD {
  static constexpr uint32_t AXS_CHUNK_PIXELS = 1024;  // matches Arduino_ESP32QSPI
  static constexpr uint32_t AXS_CHUNK_BYTES  = AXS_CHUNK_PIXELS * 2;

  static constexpr uint8_t CMD_SWRESET = 0x01;
  static constexpr uint8_t CMD_SLPIN   = 0x10;
  static constexpr uint8_t CMD_SLPOUT  = 0x11;
  static constexpr uint8_t CMD_INVOFF  = 0x20;
  static constexpr uint8_t CMD_INVON   = 0x21;
  static constexpr uint8_t CMD_DISPOFF = 0x28;
  static constexpr uint8_t CMD_DISPON  = 0x29;
  static constexpr uint8_t CMD_CASET   = 0x2A;
  static constexpr uint8_t CMD_RASET   = 0x2B;
  static constexpr uint8_t CMD_RAMWR   = 0x2C;
  static constexpr uint8_t CMD_MADCTL  = 0x36;
  static constexpr uint8_t CMD_COLMOD  = 0x3A;

  // Init table — 320x480 type1, ported from moononournation/Arduino_GFX.
  // Layout: cmd, arg_count, args... (sentinel 0xFF 0xFF ends the list).
  static const uint8_t* init_ops() {
    static const uint8_t ops[] = {
      0xA0, 17,
        0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05,
        0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xA2, 31,
        0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19,
        0x80, 0x80, 0x80, 0x20, 0xF9, 0x10, 0x02, 0xFF, 0xFF, 0xF0,
        0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A,
      0xD0, 30,
        0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15,
        0xC2, 0x42, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14,
        0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12,
      0xA3, 22,
        0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55,
      0xC1, 30,
        0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00,
        0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F,
        0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40,
      0xC3, 11,
        0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01,
      0xC4, 29,
        0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64,
        0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00,
        0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50,
      0xC5, 23,
        0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10,
        0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20,
        0x10, 0x10, 0x00,
      0xC6, 20,
        0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22,
        0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22,
      0xC7, 20,
        0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF,
        0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F,
      0xC9, 4,  0x33, 0x44, 0x44, 0x01,
      0xCF, 27,
        0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68,
        0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44,
        0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08,
      0xD5, 30,
        0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92,
        0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03,
        0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00,
      0xD6, 30,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00,
        0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03,
        0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00,
      0xD7, 19,
        0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D,
        0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F,
      0xD8, 12,
        0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19,
      0xD9, 12,
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
      0xDD, 12,
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
      0xDF, 8,  0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90,
      0xE0, 17,
        0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21,
        0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D,
      0xE1, 17,
        0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21,
        0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F,
      0xE2, 17,
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32,
        0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
      0xE3, 17,
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32,
        0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F,
      0xE4, 17,
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E,
        0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
      0xE5, 17,
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E,
        0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F,
      0xA4, 16,
        0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30,
        0x40, 0x40, 0x20, 0xFF, 0x60, 0x30,
      0xA4, 4,  0x85, 0x85, 0x95, 0x85,
      0xBB, 8,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xFF, 0xFF   // end sentinel
    };
    return ops;
  }

  Panel_AXS15231B(void) {
    _cfg.memory_width  = _cfg.panel_width  = 320;
    _cfg.memory_height = _cfg.panel_height = 480;
  }

  ~Panel_AXS15231B() {
    if (_spi_dev) { spi_bus_remove_device(_spi_dev); _spi_dev = nullptr; }
    if (_bus_attached) { spi_bus_free(_spi_host); _bus_attached = false; }
    if (_dma_buf) { heap_caps_free(_dma_buf); _dma_buf = nullptr; }
  }

  // Pin / clock setup — called before init(). The panel owns its own SPI
  // bus; Panel_LCD's _bus pointer is ignored.
  void setTrace(bool on) { _trace = on; }

  void setBusPins(spi_host_device_t host, int8_t sck,
                  int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                  uint32_t freq_hz) {
    _spi_host = host;
    _pin_sck  = sck;
    _pin_d0   = d0;  _pin_d1 = d1;  _pin_d2 = d2;  _pin_d3 = d3;
    _freq     = freq_hz;
  }

  bool init(bool use_reset) override {
    if (!Panel_Device::init(use_reset)) return false;
    if (!ensure_bus()) return false;

    delay(100);

    // Absolute minimal init — match the skill's skeleton verbatim:
    //   SWRESET, DISPOFF, SLPIN, SLPOUT, DISPON, COLMOD=0x05.
    // No MADCTL (use factory default orientation for this test).
    send_cmd(CMD_SWRESET);   delay(200);
    send_cmd(CMD_DISPOFF);   delay(20);
    send_cmd(CMD_SLPIN);     delay(20);
    send_cmd(CMD_SLPOUT);    delay(200);
    send_cmd(CMD_DISPON);    delay(20);

    uint8_t colmod = 0x05;
    send_cmd(CMD_COLMOD, &colmod, 1);
    delay(10);
    _write_depth = rgb565_2Byte;
    _write_bits  = 16;

    return true;
  }

  void beginTransaction(void) override {
    if (_in_tx) return;
    _in_tx = true;
    if (_spi_dev) spi_device_acquire_bus(_spi_dev, portMAX_DELAY);
  }

  void endTransaction(void) override {
    if (!_in_tx) return;
    _in_tx = false;
    flush_pixels();   // safety net — close any open pixel batch
    if (_spi_dev) spi_device_release_bus(_spi_dev);
  }

  color_depth_t setColorDepth(color_depth_t depth) override {
    uint8_t mode = 0;
    if (depth == rgb565_2Byte)      { mode = 0x05; _write_depth = depth; }
    else if (depth == rgb666_3Byte) { mode = 0x06; _write_depth = depth; }
    else return _write_depth;
    send_cmd(CMD_COLMOD, &mode, 1);
    return _write_depth;
  }

  void setInvert(bool invert) override {
    send_cmd(invert ? CMD_INVON : CMD_INVOFF);
  }

  void setSleep(bool flg) override {
    send_cmd(flg ? CMD_SLPIN : CMD_SLPOUT);
    if (!flg) delay(150);
  }

  void setPowerSave(bool /*flg*/) override {}
  void waitDisplay(void) override {}
  bool displayBusy(void) override { return false; }

  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if ((xe - xs) >= _width)  { xs = 0; xe = _width  - 1; }
    if ((ye - ys) >= _height) { ys = 0; ye = _height - 1; }
    // Recovery gap after a QIO pixel batch — the panel sometimes refuses
    // to accept a single-line CASET/RASET immediately after quad data.
    esp_rom_delay_us(2);
    uint8_t buf[4];
    buf[0] = xs >> 8; buf[1] = xs & 0xFF; buf[2] = xe >> 8; buf[3] = xe & 0xFF;
    send_cmd(CMD_CASET, buf, 4);
    esp_rom_delay_us(2);
    buf[0] = ys >> 8; buf[1] = ys & 0xFF; buf[2] = ye >> 8; buf[3] = ye & 0xFF;
    send_cmd(CMD_RASET, buf, 4);
    esp_rom_delay_us(2);
    _pixel_first = true;
    Serial.printf("[AXS] setWindow (%u,%u)-(%u,%u)\n",
      (unsigned)xs, (unsigned)ys, (unsigned)xe, (unsigned)ye);
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y,
                           uint32_t rawcolor) override {
    setWindow(x, y, x, y);
    // Single-pixel path — route through send_pixels_repeat_qio so the DMA
    // buffer is used (stack pointers aren't DMA-capable on ESP32-S3).
    send_pixels_repeat_qio((uint16_t)rawcolor, 1);
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y,
                               uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    uint32_t len = (uint32_t)w * h;
    setWindow(x, y, x + w - 1, y + h - 1);
    send_pixels_repeat_qio((uint16_t)rawcolor, len);
  }

  void writeBlock(uint32_t rawcolor, uint32_t len) override {
    send_pixels_repeat_qio((uint16_t)rawcolor, len);
  }

  void writePixels(pixelcopy_t* param, uint32_t len, bool /*use_dma*/) override {
    // Convert into the DMA staging buffer in AXS_CHUNK_PIXELS-sized chunks
    // and byte-swap for the panel (AXS15231B wants big-endian RGB565).
    // send_pixels_qio_chunk consults _pixel_first so the first chunk after
    // setWindow opens with RAMWR and everything after uses RAMWRCONT, even
    // across multiple writePixels calls (e.g. writeImage looping rows).
    uint8_t* staging = dma_buf();
    if (!staging) return;
    while (len) {
      uint32_t chunk = (len > AXS_CHUNK_PIXELS) ? AXS_CHUNK_PIXELS : len;
      param->fp_copy(staging, 0, chunk, param);
      uint16_t* dst = reinterpret_cast<uint16_t*>(staging);
      for (uint32_t i = 0; i < chunk; ++i) {
        uint16_t p = dst[i];
        dst[i] = (uint16_t)((p << 8) | (p >> 8));
      }
      send_pixels_qio_chunk(staging, chunk * 2);
      len -= chunk;
    }
    // writeImage calls writePixels per row — DON'T flush here or each row
    // starts a fresh batch. writeImage handles flush at its end.
  }

  void writeImage(uint_fast16_t x, uint_fast16_t y,
                  uint_fast16_t w, uint_fast16_t h,
                  pixelcopy_t* param, bool use_dma) override {
    if (_trace) Serial.printf("[AXS] writeImage xy=(%u,%u) wh=(%u,%u)\n",
      (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);
    setWindow(x, y, x + w - 1, y + h - 1);
    auto sx = param->src_x;
    do {
      writePixels(param, w, use_dma);
      param->src_x = sx;
      param->src_y++;
    } while (--h);
    flush_pixels();
  }

  // Readback is not supported (write-only QSPI panel).
  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                void*, pixelcopy_t*) override {}

 protected:
  spi_host_device_t     _spi_host   = SPI2_HOST;
  spi_device_handle_t   _spi_dev    = nullptr;
  bool                  _bus_attached = false;
  bool                  _in_tx      = false;
  // True until the first pixel chunk after the most recent setWindow has
  // been pushed. That chunk sends RAMWR (0x002C00) to anchor at the top
  // of the window; every subsequent chunk sends RAMWRCONT (0x003C00) so
  // the panel appends instead of re-anchoring.
  bool                  _pixel_first = true;
  // Debug — toggle at runtime to trace window/image/pixel calls. Cheap
  // when off, noisy when on. Set by Panel_AXS15231B::setTrace().
  bool                  _trace = false;
  int8_t  _pin_sck = -1, _pin_d0 = -1, _pin_d1 = -1, _pin_d2 = -1, _pin_d3 = -1;
  uint32_t _freq   = 40000000;
  uint8_t* _dma_buf = nullptr;

  uint8_t* dma_buf() {
    if (!_dma_buf) {
      _dma_buf = (uint8_t*)heap_caps_aligned_alloc(
          16, AXS_CHUNK_BYTES, MALLOC_CAP_DMA);
    }
    return _dma_buf;
  }

  // Bypass Panel_Device::cs_control — go directly to GPIO to match the
  // skeleton and Arduino_ESP32QSPI exactly. Panel_Device's default might
  // do extra bookkeeping we don't need and that may interact badly with
  // our manual pin management.
  void cs_control(bool level) override {
    if (_cfg.pin_cs >= 0) {
      gpio_set_level((gpio_num_t)_cfg.pin_cs, level ? 1 : 0);
    }
  }

  bool ensure_bus() {
    if (_spi_dev) return true;
    if (_cfg.pin_cs >= 0) {
      gpio_set_direction((gpio_num_t)_cfg.pin_cs, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_cfg.pin_cs, 1);
    }
    spi_bus_config_t bus = {};
    bus.mosi_io_num   = _pin_d0;
    bus.miso_io_num   = _pin_d1;    // ESP-IDF uses MISO pin slot for D1
    bus.sclk_io_num   = _pin_sck;
    bus.quadwp_io_num = _pin_d2;
    bus.quadhd_io_num = _pin_d3;
    bus.max_transfer_sz = AXS_CHUNK_BYTES + 16;
    bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
    esp_err_t err = spi_bus_initialize(_spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      ESP_LOGE("AXS15231B", "spi_bus_initialize failed: %d", err);
      return false;
    }
    _bus_attached = true;

    spi_device_interface_config_t dev = {};
    dev.command_bits   = 8;
    dev.address_bits   = 24;
    dev.mode           = 0;
    dev.clock_speed_hz = _freq;
    dev.spics_io_num   = -1;   // CS is handled by Panel_Device::cs_control
    dev.queue_size     = 1;
    dev.flags          = SPI_DEVICE_HALFDUPLEX;
    err = spi_bus_add_device(_spi_host, &dev, &_spi_dev);
    if (err != ESP_OK) {
      ESP_LOGE("AXS15231B", "spi_bus_add_device failed: %d", err);
      return false;
    }
    return dma_buf() != nullptr;
  }

  void update_madctl(void) override {
    uint8_t r   = _internal_rotation;
    // Match Panel_NV3041A's rgb_order convention: rgb_order=true -> RGB (0x00),
    // rgb_order=false -> BGR (0x08). AXS15231B's MADCTL bit3 = BGR.
    uint8_t rgb = _cfg.rgb_order ? 0x00 : 0x08;
    uint8_t v;
    switch (r) {
      case 1: v = 0x40 | 0x20 | rgb; break;  // MX|MV
      case 2: v = 0x40 | 0x80 | rgb; break;  // MX|MY
      case 3: v = 0x80 | 0x20 | rgb; break;  // MY|MV
      default: v = rgb; break;
    }
    send_cmd(CMD_MADCTL, &v, 1);
    if (_trace) Serial.printf("[AXS] MADCTL = 0x%02X (rot=%u rgb_order=%d)\n",
      v, (unsigned)r, (int)_cfg.rgb_order);
  }

  // Single-line command, optionally followed by single-line data bytes.
  // Matches Arduino_ESP32QSPI exactly — small payloads (<=4 bytes) go via
  // SPI_TRANS_USE_TXDATA (inline in the transaction struct, internal SPI
  // FIFO — no DMA), which is how writeC8D16D16 handles CASET/RASET. Larger
  // payloads use the DMA staging buffer.
  void send_cmd(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
    if (!_spi_dev) return;
    flush_pixels();
    cs_control(false);
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd   = 0x02;
    t.base.addr  = ((uint32_t)cmd) << 8;
    if (len == 0) {
      t.base.tx_buffer = nullptr;
      t.base.length    = 0;
    } else if (len <= 4) {
      t.base.flags |= SPI_TRANS_USE_TXDATA;
      for (size_t i = 0; i < len; ++i) t.base.tx_data[i] = data[i];
      t.base.length = len * 8;
    } else {
      uint8_t* staging = dma_buf();
      if (!staging) { cs_control(true); return; }
      size_t copy = (len > AXS_CHUNK_BYTES) ? AXS_CHUNK_BYTES : len;
      memcpy(staging, data, copy);
      t.base.tx_buffer = staging;
      t.base.length    = copy * 8;
    }
    spi_device_polling_transmit(_spi_dev, &t.base);
    cs_control(true);
  }

  // Send one chunk of pixel data in QIO mode. Matches the skill skeleton /
  // LilyGo T-Display-S3 Long pattern: CS toggles per chunk, full header
  // sent every time. First chunk uses RAMWR (0x002C00) to anchor at the
  // setWindow origin; continuations use RAMWRCONT (0x003C00) to append.
  // A single microsecond CS-high gap between chunks prevents the panel
  // from interpreting the next header as appended pixel data.
  void send_pixels_qio_chunk(const void* buf, size_t bytes) {
    if (!_spi_dev || bytes == 0) return;
    bool first = _pixel_first;
    _pixel_first = false;
    cs_control(false);
    spi_transaction_ext_t t = {};
    t.base.flags     = SPI_TRANS_MODE_QIO;
    t.base.cmd       = 0x32;
    t.base.addr      = first ? 0x002C00 : 0x003C00;
    t.base.tx_buffer = buf;
    t.base.length    = bytes * 8;
    esp_err_t err = spi_device_polling_transmit(_spi_dev, &t.base);
    cs_control(true);
    esp_rom_delay_us(1);
    if (err != ESP_OK) {
      ESP_LOGE("AXS15231B", "chunk bytes=%u err=%d", (unsigned)bytes, (int)err);
    }
  }

  // No-op now that CS toggles per chunk — kept so send_cmd / endTransaction
  // callers don't need to know about it. Also resets _pixel_first so the
  // next setWindow starts a fresh batch with a RAMWR header.
  void flush_pixels() { _pixel_first = true; }


  // Fill the DMA staging buffer with `color` (RGB565, panel byte order) and
  // clock it out in chunks. `count` is pixel count.
  void send_pixels_repeat_qio(uint16_t color, uint32_t count) {
    uint8_t* staging = dma_buf();
    if (!staging) return;
    uint16_t be = (uint16_t)((color << 8) | (color >> 8));
    uint32_t fill_pixels = (count < AXS_CHUNK_PIXELS) ? count : AXS_CHUNK_PIXELS;
    uint16_t* dst = reinterpret_cast<uint16_t*>(staging);
    for (uint32_t i = 0; i < fill_pixels; ++i) dst[i] = be;

    const uint32_t total = count;
    uint32_t sent = 0;
    while (count) {
      uint32_t chunk = (count > AXS_CHUNK_PIXELS) ? AXS_CHUNK_PIXELS : count;
      send_pixels_qio_chunk(staging, chunk * 2);
      count -= chunk;
      sent  += chunk;
    }
    flush_pixels();
    (void)sent; (void)total;
  }
};

}  // namespace v1
}  // namespace lgfx

#endif  // ESP_PLATFORM
