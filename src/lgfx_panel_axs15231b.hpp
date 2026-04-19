// AXS15231B QSPI panel driver for LovyanGFX.
//
// Mainline LovyanGFX has no Panel_AXS15231B (issue #699). This class is
// modeled on lgfx::Panel_NV3041A — a 1.2.x panel with the same QSPI-header
// framing — and uses the AXS15231B init sequence from moononournation's
// Arduino_GFX (axs15231b_320480_type1_init_operations).
//
// Protocol: commands and pixel data flow over a standard single-wire SPI bus
// (lgfx::Bus_SPI). The QSPI part is pure framing: every command is prefixed
// with a 4-byte header {0x02, 0x00, cmd, 0x00} and RAMWR starts with
// {0x32, 0x00, 0x2C, 0x00}. Physical wiring only needs MOSI=D0, SCK, CS.

#pragma once

#if defined(ESP_PLATFORM)

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include <lgfx/v1/Bus.hpp>
#include <lgfx/v1/misc/pixelcopy.hpp>
#include <lgfx/v1/misc/colortype.hpp>

namespace lgfx {
inline namespace v1 {

struct Panel_AXS15231B : public Panel_LCD {
 protected:
  static constexpr uint8_t CMD_NOP     = 0x00;
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

  static constexpr uint8_t CMD_MADCTL_MY  = 0x80;
  static constexpr uint8_t CMD_MADCTL_MX  = 0x40;
  static constexpr uint8_t CMD_MADCTL_MV  = 0x20;
  static constexpr uint8_t CMD_MADCTL_ML  = 0x10;
  static constexpr uint8_t CMD_MADCTL_RGB = 0x00;

  // Init sequence for 320x480 AXS15231B (type 1). Each entry is:
  //   { cmd_byte, arg_count, args[0..arg_count-1] }
  // Sourced from Arduino_GFX axs15231b_320480_type1_init_operations[].
  // End-of-sequence marker: cmd_byte=0xFF, arg_count=0xFF.
  static const uint8_t* init_ops() {
    static const uint8_t ops[] = {
      0xA0, 17,
        0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F,
        0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xA2, 31,
        0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
        0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
        0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
        0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A,
      0xD0, 30,
        0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01,
        0x20, 0x15, 0xC2, 0x42, 0x22, 0x22, 0xAA, 0x03,
        0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00,
        0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12,
      0xA3, 22,
        0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x00, 0x55, 0x55,
      0xC1, 30,
        0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55,
        0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF,
        0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B,
        0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40,
      0xC3, 11,
        0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00,
        0x01, 0x80, 0x01,
      0xC4, 29,
        0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32,
        0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06,
        0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10,
        0x00, 0x0A, 0x0A, 0x44, 0x50,
      0xC5, 23,
        0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
        0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
        0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00,
      0xC6, 20,
        0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B,
        0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F,
        0x6A, 0x18, 0xC8, 0x22,
      0xC7, 20,
        0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00,
        0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24,
        0x0C, 0x0D, 0x0E, 0x0F,
      0xC9, 4,
        0x33, 0x44, 0x44, 0x01,
      0xCF, 27,
        0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18,
        0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4,
        0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08,
        0x12, 0xA0, 0x08,
      0xD5, 30,
        0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74,
        0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46,
        0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00,
        0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00,
      0xD6, 30,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07,
        0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x00, 0x84, 0x00, 0x20, 0x01, 0x00,
      0xD7, 19,
        0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00,
        0x20, 0xA0, 0x1F,
      0xD8, 12,
        0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F,
        0x18, 0x1D, 0x1F, 0x19,
      0xD9, 12,
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F,
      0xDD, 12,
        0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1F, 0x1F,
      0xDF, 8,
        0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90,
      0xE0, 17,
        0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D,
      0xE1, 17,
        0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28,
        0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F,
      0xE2, 17,
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
      0xE3, 17,
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
        0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F,
      0xE4, 17,
        0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
      0xE5, 17,
        0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
        0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F,
      0xA4, 16,
        0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80,
        0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30,
      0xA4, 4,
        0x85, 0x85, 0x95, 0x85,
      0xBB, 8,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xFF, 0xFF    // end-of-sequence sentinel
    };
    return ops;
  }

 public:
  Panel_AXS15231B(void) {
    _cfg.memory_width  = _cfg.panel_width  = 320;
    _cfg.memory_height = _cfg.panel_height = 480;
  }

  bool init(bool use_reset) override {
    if (!Panel_Device::init(use_reset)) return false;

    startWrite();

    // Software reset (hardware RST not wired on JC3248W535).
    cs_control(false);
    write_cmd(CMD_SWRESET);
    _bus->wait();
    cs_control(true);
    delay(200);

    // Walk the init table.
    const uint8_t* p = init_ops();
    while (p[0] != 0xFF || p[1] != 0xFF) {
      uint8_t cmd = p[0];
      uint8_t n   = p[1];
      cs_control(false);
      write_cmd(cmd);
      for (uint8_t i = 0; i < n; ++i) {
        _bus->writeCommand(p[2 + i], 8);
      }
      _bus->wait();
      cs_control(true);
      p += 2 + n;
    }

    // Sleep out + display on.
    cs_control(false);
    write_cmd(CMD_SLPOUT);
    _bus->wait();
    cs_control(true);
    delay(200);

    cs_control(false);
    write_cmd(CMD_DISPON);
    _bus->wait();
    cs_control(true);
    delay(100);

    endWrite();
    return true;
  }

  void beginTransaction(void) override {
    if (_in_transaction) return;
    _in_transaction = true;
    _bus->beginTransaction();
  }

  void endTransaction(void) override {
    if (!_in_transaction) return;
    _in_transaction = false;
    if (_has_align_data) {
      _has_align_data = false;
      _bus->writeData(0, 8);
    }
    _bus->endTransaction();
  }

  color_depth_t setColorDepth(color_depth_t depth) override {
    // AXS15231B supports RGB565 (0x05 in COLMOD) as the common choice for
    // QSPI-wrapped transfers. RGB666 exists but is less common in the wild.
    uint8_t mode;
    if (depth == rgb565_2Byte) {
      mode = 0x05;
    } else if (depth == rgb666_3Byte) {
      mode = 0x06;
    } else {
      return _write_depth;
    }
    _write_depth = depth;
    startWrite();
    cs_control(false);
    write_cmd(CMD_COLMOD);
    _bus->writeCommand(mode, 8);
    _bus->wait();
    cs_control(true);
    endWrite();
    return _write_depth;
  }

  void setInvert(bool invert) override {
    cs_control(false);
    write_cmd(invert ? CMD_INVON : CMD_INVOFF);
    _bus->wait();
    cs_control(true);
  }

  void setSleep(bool flg) override {
    cs_control(false);
    write_cmd(flg ? CMD_SLPIN : CMD_SLPOUT);
    _bus->wait();
    cs_control(true);
    if (!flg) delay(150);
  }

  void setPowerSave(bool /*flg*/) override {}
  void waitDisplay(void) override {}
  bool displayBusy(void) override { return false; }

  void writePixels(pixelcopy_t* param, uint32_t len, bool use_dma) override {
    start_qspi();
    if (param->no_convert) {
      _bus->writeBytes(reinterpret_cast<const uint8_t*>(param->src_data),
                       len * _write_bits >> 3, true, use_dma);
    } else {
      _bus->writePixels(param, len);
    }
    if (_cfg.dlen_16bit && (_write_bits & 15) && (len & 1)) {
      _has_align_data = !_has_align_data;
    }
    _bus->wait();
    end_qspi();
  }

  void writeBlock(uint32_t rawcolor, uint32_t len) override {
    start_qspi();
    _bus->writeDataRepeat(rawcolor, _write_bits, len);
    _bus->wait();
    end_qspi();
  }

  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override {
    if ((xe - xs) >= _width)  { xs = 0; xe = _width  - 1; }
    if ((ye - ys) >= _height) { ys = 0; ye = _height - 1; }

    cs_control(false);
    write_cmd(CMD_CASET);
    _bus->writeCommand(xs >> 8,   8);
    _bus->writeCommand(xs & 0xFF, 8);
    _bus->writeCommand(xe >> 8,   8);
    _bus->writeCommand(xe & 0xFF, 8);
    _bus->wait();
    cs_control(true);

    cs_control(false);
    write_cmd(CMD_RASET);
    _bus->writeCommand(ys >> 8,   8);
    _bus->writeCommand(ys & 0xFF, 8);
    _bus->writeCommand(ye >> 8,   8);
    _bus->writeCommand(ye & 0xFF, 8);
    _bus->wait();
    cs_control(true);

    cs_control(false);
    write_cmd(CMD_RAMWR);
    _bus->wait();
    cs_control(true);
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y,
                           uint32_t rawcolor) override {
    setWindow(x, y, x, y);
    if (_cfg.dlen_16bit) { _has_align_data = (_write_bits & 15); }
    start_qspi();
    _bus->writeData(rawcolor, _write_bits);
    _bus->wait();
    end_qspi();
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y,
                               uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    uint32_t len = (uint32_t)w * h;
    setWindow(x, y, x + w - 1, y + h - 1);
    start_qspi();
    _bus->writeDataRepeat(rawcolor, _write_bits, len);
    _bus->wait();
    end_qspi();
  }

  void writeImage(uint_fast16_t x, uint_fast16_t y,
                  uint_fast16_t w, uint_fast16_t h,
                  pixelcopy_t* param, bool use_dma) override {
    setWindow(x, y, x + w - 1, y + h - 1);
    start_qspi();
    auto sx = param->src_x;
    auto bytes = param->dst_bits >> 3;
    do {
      if (param->no_convert) {
        auto i = (param->src_bitwidth * param->src_y + sx) * bytes;
        _bus->writeBytes(&((const uint8_t*)param->src_data)[i],
                         w * bytes, true, use_dma);
      } else {
        _bus->writePixels(param, w);
      }
      param->src_x = sx;
      param->src_y++;
    } while (--h);
    _bus->wait();
    end_qspi();
  }

  // readRect is not supported (QSPI AMOLED-style panels are write-only).
  uint32_t readCommand(uint_fast16_t /*cmd*/, uint_fast8_t /*index*/,
                       uint_fast8_t /*len*/) override { return 0; }
  uint32_t readData(uint_fast8_t /*index*/, uint_fast8_t /*len*/) override { return 0; }
  void readRect(uint_fast16_t /*x*/, uint_fast16_t /*y*/,
                uint_fast16_t /*w*/, uint_fast16_t /*h*/,
                void* /*dst*/, pixelcopy_t* /*param*/) override {}

 protected:
  bool _in_transaction = false;

  void update_madctl(void) override {
    uint8_t r = _internal_rotation;
    uint8_t rgb = _cfg.rgb_order ? 0x08 : 0x00;  // AXS15231B uses bit3 for BGR
    uint8_t v;
    switch (r) {
      case 1: v = CMD_MADCTL_MX | CMD_MADCTL_MV | rgb; break;
      case 2: v = CMD_MADCTL_MX | CMD_MADCTL_MY | rgb; break;
      case 3: v = CMD_MADCTL_MY | CMD_MADCTL_MV | rgb; break;
      default: v = rgb; break;
    }
    startWrite();
    cs_control(false);
    write_cmd(CMD_MADCTL);
    _bus->writeCommand(v, 8);
    _bus->wait();
    cs_control(true);
    endWrite();
  }

  // Emit a single-byte command prefixed by the QSPI header.
  void write_cmd(uint8_t cmd) {
    _bus->writeCommand(0x02, 8);
    _bus->writeCommand(0x00, 8);
    _bus->writeCommand(cmd,  8);
    _bus->writeCommand(0x00, 8);
  }

  // Enter pixel-data mode. After this, every byte written is pixel data
  // until end_qspi() de-asserts CS.
  void start_qspi() {
    cs_control(false);
    _bus->writeCommand(0x32, 8);
    _bus->writeCommand(0x00, 8);
    _bus->writeCommand(0x2C, 8);
    _bus->writeCommand(0x00, 8);
    _bus->wait();
  }

  void end_qspi() {
    _bus->writeCommand(0x32, 8);
    _bus->writeCommand(0x00, 8);
    _bus->writeCommand(0x00, 8);
    _bus->writeCommand(0x00, 8);
    _bus->wait();
    cs_control(true);
  }
};

}  // namespace v1
}  // namespace lgfx

#endif  // ESP_PLATFORM
