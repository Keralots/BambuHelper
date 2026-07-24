# Custom-Wired (DIY) Displays

Wired your own SPI display to a bare ESP32 and it does not match any of the
maintained boards? Instead of editing C++, BambuHelper has a **generic DIY
build target** (`BOARD_IS_DIY`) whose display driver, pins, and geometry all
come from `-D DIY_*` build flags in a `boards/*.ini` file. Set your wiring
once and build - no source changes.

The flags are resolved and validated at compile time in
[`include/diy_display_config.h`](../include/diy_display_config.h); the generic
`LGFX_DIY` panel class is the first entry in
[`src/lgfx_boards.h`](../src/lgfx_boards.h).

## Quick start

Two starter files ship in this folder:

- **`boards/diy_round240.ini`** - a ready-to-use example for a GC9A01 round
  240x240 module on an ESP32-C3 Super Mini. Edit the five `DIY_PIN_*` lines to
  match your wiring, then build:

  ```
  pio run -e diy_round240
  ```

- **`boards/diy_spi_template.ini`** - a fully commented template (a working
  ST7789 240x320 example). Copy it to a new `boards/<yourname>.ini`, rename the
  `[env:...]` header, edit the flags, and build `pio run -e <yourname>`.

Both are merged into the build through `extra_configs` in
[`platformio.ini`](../platformio.ini). These envs are user-compiled and are
**not** part of the web flasher or the official release set.

## Pick one driver and its matching layout

Set exactly one panel driver, and the **layout flag that matches it**. The
build refuses to compile if they disagree, so you cannot silently ship the
wrong layout - which is what makes text fall off the corners of a round
screen.

| Panel driver | Layout flag to set | Panel size |
|---|---|---|
| `DIY_PANEL_GC9A01` | `DISPLAY_ROUND_240` | 240x240 round |
| `DIY_PANEL_ST7789` (240x240) | *(none)* | 240x240 |
| `DIY_PANEL_ST7789` (240x320) | `DISPLAY_240x320` **+** `DIY_PANEL_H=320` | 240x320 |
| `DIY_PANEL_ILI9341` | `DISPLAY_240x320` | 240x320 |
| `DIY_PANEL_ST7796` | `DISPLAY_320x480` | 320x480 |

`DISPLAY_CYD` is rejected on a DIY build, and setting more than one
dimensional `DISPLAY_*` is a compile error.

## Flags

### Required

| Flag | Meaning |
|---|---|
| `BOARD_IS_DIY=1` | Enable the generic DIY target |
| `DIY_PANEL_<driver>=1` | Exactly one of `GC9A01` / `ST7789` / `ILI9341` / `ST7796` |
| `DISPLAY_<layout>=1` | The layout that matches the driver (see table above) |
| `DIY_PIN_SCLK` | SPI clock GPIO |
| `DIY_PIN_MOSI` | SPI data (SDA/MOSI) GPIO |
| `DIY_PIN_DC` | Data/command GPIO |
| `DIY_PIN_CS` | Chip-select GPIO |
| `BACKLIGHT_PIN` | Backlight GPIO, or `-1` if the backlight is hardwired on |

Leaving out any of the four `DIY_PIN_*` required pins, or setting zero or more
than one `DIY_PANEL_*`, is a compile error with a message telling you what is
missing.

### Optional pins

| Flag | Default | Meaning |
|---|---|---|
| `DIY_PIN_MISO` | `-1` | SPI MISO (most display-only modules do not use it) |
| `DIY_PIN_RST` | `-1` | Panel reset (set it if your module has a RST pin) |
| `DIY_SPI_HOST` | `SPI2_HOST` | SPI peripheral; use `VSPI_HOST` on a classic ESP32 |

### Optional tuning

| Flag | Default | Meaning |
|---|---|---|
| `DIY_FREQ_WRITE` | `40000000` | SPI write clock in Hz (40 MHz). Lower it if a long cable glitches |
| `DIY_INVERT` | per-driver* | Colour inversion. Flip (`0`/`1`) if colours look negative |
| `DIY_RGB_ORDER` | `0` | Set `1` if red and blue are swapped |
| `DIY_OFFSET_ROTATION` | `0` | Rotate/mirror the image, values `0`-`7`, if it comes up sideways |

\* `DIY_INVERT` defaults to on for GC9A01 and ST7796, off for ST7789 and
ILI9341. It is only a best-guess starting point - modules vary, so flip it if
your panel looks wrong. You can also toggle inversion from the web UI's
"invert colours" checkbox after the device boots.

### Geometry overrides (rarely needed)

Each driver sets a sensible panel/memory size by default. Override only if
your glass differs from the driver's native GRAM:

| Flag | Meaning |
|---|---|
| `DIY_PANEL_W` / `DIY_PANEL_H` | Visible panel width / height in pixels |
| `DIY_MEM_W` / `DIY_MEM_H` | Controller GRAM width / height |

(An ST7789 behind 240x240 glass still has a 240x320 GRAM, which the driver
default already accounts for.)

## Classic ESP32 (DevKitC / WROOM)

The examples above target S3/C3 boards. On a classic ESP32, override these in
your env instead of the S3 defaults:

```ini
board = esp32dev
board_build.partitions = min_spiffs.csv   ; the app overflows the default 1.3MB slot
build_flags =
    ...
    -D BOARD_LOW_RAM=1                     ; classic ESP32: single printer
    -D DIY_SPI_HOST=VSPI_HOST              ; classic ESP32 SPI host
    ; drop the ARDUINO_USB_CDC_ON_BOOT flag - classic ESP32 has no native USB CDC
```

The commented block at the top of `diy_spi_template.ini` has the same list.

## Buttons, buzzer, and LED

On a DIY build the button, buzzer, and status-LED pins default to **disabled**
(their normal per-board defaults often collide with a custom panel's SPI
pins), and the firmware refuses to assign any of them to a pin that is already
driving the display. Configure them from the web UI once the device boots.

## Combining with a chip-specific target

`BOARD_IS_DIY` is checked first in every hardware selector, so it can be
combined with a chip target such as `BOARD_IS_C3`: the DIY flags win the
display and pin configuration while the chip target keeps its radio and USB
fixes. See `diy_round240.ini`, which sets both `BOARD_IS_C3` and
`BOARD_IS_DIY`.
