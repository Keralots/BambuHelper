#ifndef LGFX_BOARDS_H
#define LGFX_BOARDS_H

// =============================================================================
//  LovyanGFX board-specific configurations
// =============================================================================
//
// One board variant compiles per build, selected by the BOARD_IS_* / DISPLAY_*
// flags set in platformio.ini / boards/*.ini. Each variant defines an LGFX
// device class plus a single file-scope `_tft_instance` (CYD uses a union +
// reference so the V2/Classic panel can be chosen at runtime in initDisplay()).
// The SenseCAP block also defines its PCA9535 IO-expander pin macros, which
// initDisplay() uses for the reset sequence.
//
// NOTE: this header intentionally defines file-scope `static` objects and is
// meant to be included exactly once, by display_ui.cpp. Do not include it
// elsewhere or each translation unit gets its own panel instance.

#include <LovyanGFX.hpp>

#if defined(BOARD_IS_DIY)
// --- Generic DIY SPI display (issue #153/#151) -------------------------------
// Pins/driver/geometry come from -D flags, resolved + validated in
// diy_display_config.h (pulled in via config.h, re-included here for clarity;
// #pragma once makes it a no-op the second time). This branch is FIRST in the
// chain so a DIY env that also sets BOARD_IS_C3 (to keep the C3 WiFi/Improv
// fixes) still gets the DIY panel class - specialized-first, same convention as
// BOARD_IS_C3_ROUND preceding BOARD_IS_C3.
#include "config.h"
#if   defined(DIY_PANEL_GC9A01)
  using DiyPanel = lgfx::Panel_GC9A01;
#elif defined(DIY_PANEL_ST7789)
  using DiyPanel = lgfx::Panel_ST7789;
#elif defined(DIY_PANEL_ILI9341)
  using DiyPanel = lgfx::Panel_ILI9341;
#elif defined(DIY_PANEL_ST7796)
  using DiyPanel = lgfx::Panel_ST7796;
#endif

class LGFX_DIY : public lgfx::LGFX_Device {
  DiyPanel      _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX_DIY() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = DIY_SPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = DIY_FREQ_WRITE;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = DIY_PIN_SCLK;
      cfg.pin_mosi   = DIY_PIN_MOSI;
      cfg.pin_miso   = DIY_PIN_MISO;
      cfg.pin_dc     = DIY_PIN_DC;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = DIY_PIN_CS;
      cfg.pin_rst  = DIY_PIN_RST;
      cfg.pin_busy = -1;
      cfg.memory_width  = DIY_MEM_W;
      cfg.memory_height = DIY_MEM_H;
      cfg.panel_width   = DIY_PANEL_W;
      cfg.panel_height  = DIY_PANEL_H;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = DIY_OFFSET_ROTATION;
      cfg.invert        = DIY_INVERT;
      cfg.rgb_order     = DIY_RGB_ORDER;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_DIY _tft_instance;

#elif defined(BOARD_IS_S3_ZERO)
// --- Waveshare ESP32-S3-Zero + external ST7789 240x240 -----------------------
class LGFX_S3Zero : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_S3Zero() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 9;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = 8;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;   // ST7789 chip GRAM is 240x320
#if defined(BOARD_PANEL_320)
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;   // 2.0" 240x320 modules (e.g. GMT020-02-8P)
#else
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;   // default 1.3"/1.54" 240x240 modules
#endif
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_S3Zero _tft_instance;

#elif defined(BOARD_IS_S3_ROUND)
// --- ESP32-S3 Super Mini + GC9A01 1.28" round 240x240 ------------------------
// Bare 7-pin SPI module, same wiring as the ST7789 S3 map (SCL=12, SDA=11,
// DC=9, CS=10, RES=8). GC9A01 GRAM is a full 240x240 square; the corners are
// simply not visible, so overdraw outside the inscribed circle is harmless.
class LGFX_S3_ROUND : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_S3_ROUND() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 9;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = 8;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      cfg.invert        = true;   // GC9A01 modules ship color-inverted
      cfg.rgb_order     = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_S3_ROUND _tft_instance;

#elif defined(BOARD_IS_S3)
// --- ESP32-S3 Super Mini + ST7789 240x240 ------------------------------------
class LGFX_S3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_S3() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 9;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = 8;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;   // ST7789 chip GRAM is 240x320; visible rows 0-239
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_S3 _tft_instance;

#elif defined(DISPLAY_CYD)
// --- ESP32-2432S028 (CYD) + ILI9341 240x320 ---------------------------------
// Two hardware variants exist:
//   - V2 (default): Panel_ILI9341_2 + color inversion — matches the TFT_eSPI
//     ILI9341_2_DRIVER + TFT_INVERSION_ON used on `main`.
//   - Classic: plain Panel_ILI9341, no color inversion — for units that show
//     mirrored/rotated image on V2.
// Selected at runtime from DisplaySettings.cydPanelClassic (persisted in
// Preferences).
template <class PanelT, bool InvertColors, uint8_t RotationOffset>
class LGFX_CYD_Impl : public lgfx::LGFX_Device {
  PanelT          _panel;
  lgfx::Bus_SPI   _bus;
public:
  LGFX_CYD_Impl() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = VSPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 27000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 14;
      cfg.pin_mosi   = 13;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 2;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs    = 15;
      cfg.pin_rst   = 12;
      cfg.pin_busy  = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = RotationOffset;
      cfg.invert        = InvertColors;
      cfg.rgb_order     = false;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
using LGFX_CYD_V2      = LGFX_CYD_Impl<lgfx::Panel_ILI9341_2, true,  6>;
using LGFX_CYD_Classic = LGFX_CYD_Impl<lgfx::Panel_ILI9341,   false, 2>;
// One of these is placement-new'd into _tft_storage in initDisplay() based on
// dispSettings.cydPanelClassic. Alignment covers both; size covers the larger.
union LGFX_CYD_Storage {
  LGFX_CYD_V2      v2;
  LGFX_CYD_Classic classic;
  LGFX_CYD_Storage() : v2() {}   // default-construct V2 for static-init safety
  ~LGFX_CYD_Storage() {}
};
static LGFX_CYD_Storage   _tft_storage;
// _tft_instance is a reference to the base LGFX_Device for the currently
// constructed variant. Defaults to V2; rebound via placement-new in
// initDisplay() if the user selected Classic.
static lgfx::LGFX_Device& _tft_instance = _tft_storage.v2;

#elif defined(BOARD_IS_TZT_2432)
// --- TZT L1435-2.4 (ESP32 + ST7789V 240x320) -------------------------------
// Same SPI/CS/DC pinout as CYD, but ST7789V driver. Backlight is on GPIO27
// (set via BACKLIGHT_PIN). RST is not wired on the typical TZT variant - if a
// future user reports init failure we may need to switch pin_rst to 12.
class LGFX_TZT_2432 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_TZT_2432() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = VSPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 27000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 14;
      cfg.pin_mosi   = 13;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 2;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs    = 15;
      cfg.pin_rst   = -1;
      cfg.pin_busy  = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = 0;
      cfg.invert        = true;
      cfg.rgb_order     = false;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_TZT_2432 _tft_instance;

#elif defined(BOARD_IS_WS200)
// --- Waveshare ESP32-S3-Touch-LCD-2 (2.0" ST7789 240x320) --------------------
class LGFX_WS200 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS200() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 39;
      cfg.pin_mosi   = 38;
      cfg.pin_miso   = 40;
      cfg.pin_dc     = 42;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 45;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_WS200 _tft_instance;

#elif defined(BOARD_IS_WS280)
// --- Waveshare ESP32-S3-Touch-LCD-2.8 (2.8" ST7789 240x320) -----------------
// Community / untested. Pins from Waveshare wiki "Internal Hardware Connection".
// LCD signals are direct ESP32-S3 GPIOs (no IO expander), separate from main I2C.
class LGFX_WS280 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS280() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 40;
      cfg.pin_mosi   = 45;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 41;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 42;
      cfg.pin_rst  = 39;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_WS280 _tft_instance;

#elif defined(BOARD_IS_WS154)
// --- Waveshare ESP32-S3-Touch-LCD-1.54 (1.54" ST7789 240x240) ---------------
class LGFX_WS154 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS154() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 38;
      cfg.pin_mosi   = 39;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 45;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 21;
      cfg.pin_rst  = 40;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;   // ST7789 chip GRAM is 240x320; visible rows 0-239
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_WS154 _tft_instance;

#elif defined(BOARD_IS_WS350)
// --- Waveshare ESP32-S3-Touch-LCD-3.5 (3.5" ST7796 320x480 IPS) -------------
// ST7796 over plain 4-wire SPI -> native LovyanGFX Panel_ST7796 (no Arduino_GFX
// wrapper, unlike jc3248w535's AXS15231B QSPI). Two board quirks:
//   - LCD reset is NOT on a GPIO; it hangs off the TCA9554 I2C IO expander
//     (P1). So pin_rst = -1 here, and initDisplay() pulses the expander before
//     init() (see the BOARD_IS_WS350 block there).
//   - CS is hardwired on the board (the Waveshare demo uses LCD_CS = -1), so
//     pin_cs = -1.
// invert = true: the panel is IPS and the demo inits ST7796 with IPS=true,
// which sends INVON (0x21). LovyanGFX sends the same when cfg.invert is set.
// (UNTESTED on hardware - flip if colors come out inverted.)
class LGFX_WS350 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_WS350() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;   // conservative; demo drives ST7796 at SPI default
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 5;
      cfg.pin_mosi   = 1;
      cfg.pin_miso   = 2;
      cfg.pin_dc     = 3;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = -1;   // hardwired on-board (demo uses LCD_CS=-1)
      cfg.pin_rst  = -1;   // reset is on the TCA9554 expander - see initDisplay()
      cfg.pin_busy = -1;
      cfg.memory_width  = 320;
      cfg.memory_height = 480;
      cfg.panel_width   = 320;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      cfg.invert        = true;   // IPS panel (demo: ST7796 IPS=true -> INVON)
      cfg.rgb_order     = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_WS350 _tft_instance;

#elif defined(BOARD_IS_SC05X)
// --- Panlee / Smartpanle SC05_X (ZX2D80CE02S, 2.8" ST7789 240x320) ---------
// Vendor model mapping:
//   SC05_X / ZX2D80CE02S / WT32S3-28S PRO
// This is a 240x320 ST7789 panel on an 8-bit 8080 bus. Do not confuse it with
// the 3.5" WT32-SC01 Plus (ZX3D50CE08S), which is 320x480 ST7796 and uses a
// completely different pin map.
class Panel_SC05X_ST7789 : public lgfx::Panel_ST7789 {
protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
      0x11, 0 + CMD_INIT_DELAY, 120,    // Exit sleep mode
      0x36, 1, 0x00,
      0x3A, 1, 0x05,
      0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
      0xB7, 1, 0x46,
      0xBB, 1, 0x1B,
      0xC0, 1, 0x2C,
      0xC2, 1, 0x01,
      0xC3, 1, 0x0F,
      0xC4, 1, 0x20,
      0xC6, 1, 0x0F,
      0xD0, 2, 0xA4, 0xA1,
      0xD6, 1, 0xA1,
      0xE0, 14, 0xF0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31, 0x44, 0x48, 0x36, 0x12, 0x12, 0x2B, 0x34,
      0xE1, 14, 0xF0, 0x0B, 0x0F, 0x0F, 0x0D, 0x26, 0x31, 0x43, 0x47, 0x38, 0x14, 0x14, 0x2C, 0x32,
      0x21, 0,
      0x29, 0,
      0x2C, 0,
      0xFF, 0xFF,
    };

    return listno == 0 ? list0 : nullptr;
  }
};

class LGFX_SC05X : public lgfx::LGFX_Device {
  Panel_SC05X_ST7789 _panel;
  lgfx::Bus_Parallel8 _bus;
public:
  LGFX_SC05X() {
    {
      auto cfg = _bus.config();
      cfg.port       = 0;          // LCD_CAM (only 0 on ESP32-S3)
      cfg.freq_write = 20000000;   // vendor PanelLan config uses 20 MHz
      cfg.pin_wr     = 17;         // LCD_WR
      cfg.pin_rd     = -1;
      cfg.pin_rs     = 18;         // LCD_RS (D/C)
      cfg.pin_d0     = 16;
      cfg.pin_d1     = 40;
      cfg.pin_d2     = 15;
      cfg.pin_d3     = 7;
      cfg.pin_d4     = 41;
      cfg.pin_d5     = 42;
      cfg.pin_d6     = 2;
      cfg.pin_d7     = 1;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = -1;
      cfg.pin_rst  = 3;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = 2;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable      = false;
      cfg.invert        = true;
      cfg.rgb_order     = true;
      cfg.dlen_16bit    = false;
      cfg.bus_shared    = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_SC05X _tft_instance;

#elif defined(BOARD_IS_SC01PLUS)
// --- Panlee WT32-SC01 Plus (3.5" ST7796 320x480 IPS, 8-bit 8080 parallel) -----
// First parallel-bus board in the tree: ST7796 over lgfx::Bus_Parallel8 (ESP32-S3
// LCD_CAM). Pin map is dual-source confirmed - sukesh-ak's LovyanGFX config and
// the official Panlee "WT32-SC01 PLUS" datasheet (Tab.5 LCD Interface) agree on
// every pin. Two notes vs ws_lcd_350:
//   - LCD reset is a real GPIO (4, multiplexed with touch reset), so pin_rst = 4
//     and NO initDisplay() reset block is needed (ws_lcd_350 pulses a TCA9554).
//   - CS is hardwired on the board (the datasheet exposes no LCD_CS), so pin_cs = -1.
// invert = true: ST7796 IPS, same assumption as ws_lcd_350 (UNTESTED on hardware -
// flip if colors come out inverted). freq_write can be dropped to 20MHz for first
// bring-up if the parallel bus is unstable, then raised.
class LGFX_SC01PLUS : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796   _panel;
  lgfx::Bus_Parallel8  _bus;
public:
  LGFX_SC01PLUS() {
    {
      auto cfg = _bus.config();
      cfg.port       = 0;          // LCD_CAM (only 0 on ESP32-S3)
      cfg.freq_write = 40000000;
      cfg.pin_wr     = 47;         // LCD_WR
      cfg.pin_rd     = -1;
      cfg.pin_rs     = 0;          // LCD_RS (D/C)
      cfg.pin_d0     = 9;
      cfg.pin_d1     = 46;
      cfg.pin_d2     = 3;
      cfg.pin_d3     = 8;
      cfg.pin_d4     = 18;
      cfg.pin_d5     = 17;
      cfg.pin_d6     = 16;
      cfg.pin_d7     = 15;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = -1;   // hardwired on-board (no LCD_CS in datasheet)
      cfg.pin_rst  = 4;    // LCD_RESET (multiplexed with touch reset)
      cfg.pin_busy = -1;
      cfg.memory_width  = 320;
      cfg.memory_height = 480;
      cfg.panel_width   = 320;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      cfg.invert        = true;   // IPS panel (UNTESTED - flip if inverted)
      cfg.rgb_order     = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_SC01PLUS _tft_instance;

#elif defined(BOARD_IS_JC3248W535)
// --- Guition JC3248W535 + AXS15231B 320x480 ---------------------------------
// Panel_AXS15231B_AGFX wraps moononournation/Arduino_GFX's Arduino_AXS15231B
// driver inside a LovyanGFX Panel_Device subclass. Mainline LovyanGFX has
// neither an AXS15231B panel class nor a QSPI bus, and a hand-rolled custom
// driver didn't produce correct pixels on this hardware. Arduino_GFX does —
// this wrapper lets the whole codebase keep calling the LovyanGFX API on
// `tft` while the physical QSPI traffic is handled by Arduino_GFX.
// Backlight is a simple GPIO-high (LEDC PWM not required for on/off).
#include "lgfx_panel_axs15231b_agfx.hpp"
class LGFX_JC3248W535 : public lgfx::LGFX_Device {
  lgfx::Panel_AXS15231B_AGFX _panel;
public:
  LGFX_JC3248W535() {
    // Panel_AXS15231B_AGFX owns the Arduino_GFX bus+panel internally. Pins
    // are hard-coded in its constructor to the verified JC3248W535 map
    // (CS=45, SCK=47, D0=21, D1=48, D2=40, D3=39) since Arduino_GFX's
    // databus class hard-codes them at construction anyway.
    setPanel(&_panel);
  }
  lgfx::Panel_AXS15231B_AGFX* panelAXS() { return &_panel; }
};
static LGFX_JC3248W535 _tft_instance;
#elif defined(BOARD_IS_C3_ROUND)
// --- ESP32-C3 Super Mini + GC9A01 1.28" round 240x240 ------------------------
// Bare 7-pin SPI module, same wiring as the ST7789 C3 map (SCL=21, SDA=20,
// DC=7, CS=6, RES=10). GC9A01 GRAM is a full 240x240 square; the corners are
// simply not visible, so overdraw outside the inscribed circle is harmless.
class LGFX_C3_ROUND : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_C3_ROUND() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 21;
      cfg.pin_mosi   = 20;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 7;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 6;
      cfg.pin_rst  = 10;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      cfg.invert        = true;   // GC9A01 modules ship color-inverted
      cfg.rgb_order     = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_C3_ROUND _tft_instance;

#elif defined(BOARD_IS_C3)
// --- ESP32-C3 Super Mini + ST7789 240x240 ------------------------------------
class LGFX_C3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_C3() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 21;
      cfg.pin_mosi   = 20;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 7;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 6;
      cfg.pin_rst  = 10;
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;   // ST7789 chip GRAM is 240x320; visible rows 0-239
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_C3 _tft_instance;

#elif defined(BOARD_IS_SENSECAP)
// --- SenseCAP Indicator (ESP32-S3 + ST7701S 480x480 RGB) ---------------------
//
// Hardware:
//   ST7701S 480x480 RGB TFT with SPI init commands
//   PCA9535PW I2C IO expander (addr 0x20) for display CS/RST and touch INT/RST
//   FT5X06 capacitive touch (I2C addr 0x48)
//   Backlight PWM on GPIO45
//
// The display init sequence:
//   1. Initialize I2C bus and PCA9535PW IO expander
//   2. Toggle display reset via IO expander pin 5
//   3. Pull display CS low via IO expander pin 4
//   4. Send ST7701S init commands via SPI (3-wire: CLK=41, MOSI=48)
//   5. Release display CS (high) via IO expander pin 4
//   6. Switch to LCD_CAM RGB parallel mode for pixel data

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// PCA9535PW I2C IO expander definitions
#define PCA9535_I2C_SDA   39
#define PCA9535_I2C_SCL   40
#define PCA9535_ADDR       0x20
#define PCA9535_PIN_DISP_CS  4   // Display chip select (active LOW)
#define PCA9535_PIN_DISP_RST  5   // Display reset (active LOW)
#define PCA9535_PIN_TOUCH_RST 7 // Touch reset (active LOW)
// IO_EXPANDER flag for LovyanGFX: upper bits of I2C expander GPIO pin
// (pin | 0x40) tells LovyanGFX to use I2C expander for that GPIO
#define IO_EXPANDER 0x40

// Custom panel class for SenseCAP Indicator ST7701S.
// Uses default Panel_ST7701 init commands (0x3A=0x60 RGB666, 0x21 IPS inversion).
// The default LovyanGFX Panel_ST7701 init matches Meshtastic's working config.
// RGB666 (0x60) is correct even with 16-bit bus — the ST7701S maps the 16 data
// lines to its internal 18-bit RGB channels correctly when set to RGB666 mode.
// Using RGB565 (0x50) caused R↔G channel swap because the bit packing differs.
class Panel_ST7701_SenseCAP : public lgfx::Panel_ST7701 {
  // No getInitCommands override — use default Panel_ST7701 init sequence:
  // - 0x3A=0x60 (RGB666 pixel format)
  // - 0x21 (IPS inversion on)
  // - All voltage/gamma registers from default list0
};

class LGFX_SenseCAP : public lgfx::LGFX_Device {
  Panel_ST7701_SenseCAP _panel;
  lgfx::Bus_RGB          _bus;
public:
  LGFX_SenseCAP() {
    // --- Panel config (480x480 ST7701S) ---
    {
      auto cfg = _panel.config();
      cfg.memory_width  = 480;   // Match Meshtastic working config (ST7701S internal column count for 480px panel)
      cfg.memory_height = 480;
      cfg.panel_width   = 480;
      cfg.panel_height  = 480;
      cfg.offset_x    = 0;
      cfg.offset_y  = 0;
      cfg.offset_rotation = 2;  // Panel is mounted 180° rotated — apply 180° offset so rotation 0 = upright
      cfg.invert     = false;  // Default Panel_ST7701 list0 already sends 0x21 (IPS inversion on). Setting this true would send 0x21 AGAIN toggling inversion OFF.
      cfg.pin_rst    = -1;      // RST is via PCA9535PW — managed in initDisplay()
      _panel.config(cfg);
    }
    // --- SPI init pins for ST7701S command interface ---
    // Commands are sent via 3-wire SPI (9-bit) before the RGB data bus starts.
    // CS is routed through the PCA9535PW IO expander (pin 4), so we tell
    // LovyanGFX to use GPIO 4 | IO_EXPANDER (0x44) as the CS pin — this is
    // how Meshtastic configures it too. LovyanGFX will handle CS toggling.
    {
      auto detail = _panel.config_detail();
      detail.pin_cs    = (4 | IO_EXPANDER);  // CS via PCA9535 pin 4 — mverch67 fork handles IO expander GPIO
      detail.pin_sclk  = 41;                 // SPI clock for init commands
      detail.pin_mosi  = 48;                 // SPI data for init commands
      detail.use_psram = 1;                   // Use PSRAM for framebuffer (per Meshtastic working config)
      _panel.config_detail(detail);
    }
    // --- RGB data bus (via LCD_CAM peripheral) ---
    // Pin mapping from Seeed's official SenseCAP Indicator Arduino tutorial
    // and ESPHome ST7701S component. RGB565 = 16-bit, D0-D15.
    {
      auto bus_cfg = _bus.config();
      bus_cfg.panel = &_panel;  // CRITICAL: Bus_RGB needs panel reference for getWriteDepth()

      // Control signals
      bus_cfg.pin_pclk    = 21;
      bus_cfg.pin_vsync   = 17;
      bus_cfg.pin_hsync   = 16;
      bus_cfg.pin_henable = 18;  // DE (Data Enable)

      // RGB565 data pins — matched to Meshtastic 2.7.15 working config
      // R0-R4 = GPIOs 4,3,2,1,0 (d11-d15), G0-G5 = GPIOs 10,9,8,7,6,5 (d5-d10)
      // B0-B4 = GPIOs 15,14,13,12,11 (d0-d4)
      bus_cfg.pin_d0  = 15;  // B0
      bus_cfg.pin_d1  = 14;  // B1
      bus_cfg.pin_d2  = 13;  // B2
      bus_cfg.pin_d3  = 12;  // B3
      bus_cfg.pin_d4  = 11;  // B4
      bus_cfg.pin_d5  = 10;  // G0
      bus_cfg.pin_d6  =  9;  // G1
      bus_cfg.pin_d7  =  8;  // G2
      bus_cfg.pin_d8  =  7;  // G3
      bus_cfg.pin_d9  =  6;  // G4
      bus_cfg.pin_d10 =  5;  // G5
      bus_cfg.pin_d11 =  4;  // R0
      bus_cfg.pin_d12 =  3;  // R1
      bus_cfg.pin_d13 =  2;  // R2
      bus_cfg.pin_d14 =  1;  // R3
      bus_cfg.pin_d15 =  0;  // R4

      // Pixel clock frequency — 6 MHz per Meshtastic working config
      bus_cfg.freq_write = 6000000;

      // Timing — matched to Meshtastic 2.7.15 working config
      bus_cfg.hsync_polarity    = 0;   // Active high (per Meshtastic)
      bus_cfg.hsync_front_porch = 10;
      bus_cfg.hsync_pulse_width = 8;
      bus_cfg.hsync_back_porch  = 50;
      bus_cfg.vsync_polarity    = 0;   // Active high (per Meshtastic)
      bus_cfg.vsync_front_porch = 10;
      bus_cfg.vsync_pulse_width = 8;
      bus_cfg.vsync_back_porch  = 20;
      bus_cfg.pclk_active_neg   = 0;   // PCLK active high (per Meshtastic)
      bus_cfg.de_idle_high      = 1;   // DE idle high (per Meshtastic)
      bus_cfg.pclk_idle_high    = 0;   // PCLK idle low (per Meshtastic)

      _bus.config(bus_cfg);
      _panel.setBus(&_bus);
    }
    setPanel(&_panel);
  }
};
static LGFX_SenseCAP _tft_instance;

#elif defined(BOARD_IS_ES3N28P)
// --- QD electronic 2.8" IPS ESP32-S3 (ILI9341V 240x320 + FT6336) ------------
// Issue #125. ILI9341V over plain 4-wire SPI -> native LovyanGFX Panel_ILI9341.
// Pins from the vendor Arduino demo (spi_dev.h): CS=10 DC=46 BL=45 MOSI=11
// SCLK=12 MISO=13, RST=-1 (the demo never toggles a reset GPIO; relies on POR,
// same as tzt_2432 / ws_lcd_200). IPS panel: the vendor init sends 0x21 (INVON)
// and MADCTL 0x08 (BGR), so invert=true / rgb_order=false here.
// freq_write 40MHz is conservative for first bring-up (the demo runs the bus at
// 80MHz - raise once hardware confirms a stable panel).
// UNTESTED: if colors come out inverted, flip `invert`; if the image is
// mirrored/wrong, try lgfx::Panel_ILI9341_2 (the CYD-V2 variant).
class LGFX_ES3N28P : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX_ES3N28P() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = 13;
      cfg.pin_dc     = 46;
      cfg.use_lock   = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = -1;   // no reset GPIO on this board - relies on POR
      cfg.pin_busy = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 320;
      cfg.panel_width   = 240;
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = 0;
      cfg.invert        = true;   // IPS panel (vendor init sends 0x21 INVON)
      cfg.rgb_order     = false;  // BGR (vendor MADCTL 0x08)
      cfg.readable      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
static LGFX_ES3N28P _tft_instance;

#else
  #error "No board variant defined. Set BOARD_IS_<NAME> in your env's build_flags - see platformio.ini / boards/*.ini for the list of supported boards."
#endif

#endif // LGFX_BOARDS_H
