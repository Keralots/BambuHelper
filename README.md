# BambuHelper

Dedicated Bambu Lab printer monitor built with ESP32-S3 Super Mini and a 1.54" 240x240 color TFT display (ST7789).

Connects to your printer via MQTT over TLS and displays a real-time dashboard with arc gauges, animations, live stats, and optional buzzer notifications.

Additional supported boards include Guition JC3248W535 320x480, CYD 240x320, Waveshare ESP32-S3-Zero (with 240x240 or 240x320 panel), Waveshare 2" 240x320, Waveshare 1.54" 240x240, and ESP32-C3 DIY builds using the same 240x240 display as the ESP32-S3 version.

> **One-click setup:** as of v3.2, you can flash your board and configure WiFi entirely from the browser at **[keralots.github.io/BambuHelper](https://keralots.github.io/BambuHelper/)** - no PlatformIO, no esptool, no captive portal hopping.

> **Project status:** BambuHelper is stable and feature-rich. Development continues, but new features will land at a more relaxed pace from here on rather than the near-weekly cadence of the last stretch. Bug fixes and occasional features will keep coming - contributions are welcome.

### Supported Printers

| Connection Mode | Printers | How it connects |
|---|---|---|
| **LAN Direct** | P1P, P1S, X1, X1C, X1E, A1, A1 Mini | Local MQTT via printer IP + LAN access code |
| **LAN Direct (Developer Mode)** | H2S, H2C, H2D | LAN-only mode + Developer Mode required - see note below |
| **Bambu Cloud (All printers)** | Any Bambu printer | Cloud MQTT - sign in to your Bambu account from the device, or paste an access token. No LAN mode needed |

> **H2 series LAN mode:** H2S, H2C, and H2D printers require both **LAN-only mode** and **Developer Mode** enabled in printer settings for local MQTT to work. Without Developer Mode, the printer accepts connections but does not respond to status requests. If you prefer not to enable Developer Mode, use Bambu Cloud mode instead.

> **Tip:** Use "Bambu Cloud (All printers)" if you don't want to enable LAN/Developer mode on your printer (for example to keep Bambu Handy working), if your ESP32 is on a different network than the printer, or if your printer only supports cloud mode (P2S).

### Cloud Mode Security Notice

When using Bambu Cloud, BambuHelper connects through Bambu Lab's cloud MQTT service. There are **three ways to get it access**, and they differ in what your device ends up holding:

| Way in | What the device stores | Good to know |
|---|---|---|
| **Email code** | The token and your email address | No password is ever typed or sent. Bambu mails you a 6-digit code, you enter it once. |
| **Password** | The token, your email, and the password **only if you tick "Remember the password"** | Remembering it lets the device renew the token by itself when it expires. Accounts with two-factor authentication cannot do that, so the device **discards the password** as soon as it sees a 2FA prompt. |
| **Access token** | The token only | The original method: copy the token out of your browser and paste it in. Still fully supported. |

- **Whatever you choose, the token is what talks to Bambu.** It expires after about 3 months. With a remembered password the device signs in again on its own; otherwise you sign in again or paste a fresh token.
- **Read-only access** - BambuHelper only reads printer status. It never sends commands or modifies printer settings, beyond the chamber-light and smart-plug actions you trigger yourself.
- **The config portal has no password and runs over plain HTTP.** Anything you type into it, including a Bambu password, crosses your network unencrypted and is readable by anything else on that network. On a network you do not fully trust, prefer the **email code** or paste a token - a token expires on its own, a password does not.
- **Same approach as other community projects** - the token is the same credential used by the [Home Assistant Bambu Lab integration](https://github.com/greghesp/ha-bambulab), [OctoPrint-Bambu](https://github.com/jneilliii/OctoPrint-BambuPrinter), and other trusted third-party tools.
- **Signing out wipes all of it** - token, email and any stored password - from the device.

## Supported Boards

| Preview | Board | Notes |
|---|---|---|
| ![ESP32-S3 Super Mini dashboard](img/interface1.jpg) | **ESP32-S3 Super Mini + 1.54" ST7789** | Base implementation this project started from. Uses an ESP32-S3 Super Mini with a `1.54"` TFT SPI ST7789 (`240x240`) display. Use the `esp32s3` firmware build. Supports **up to 2 printers** (no PSRAM). |
| ![Guition JC3248W535](img/jc3248w535.jpg) | **Guition JC3248W535** | `320x480` IPS all-in-one board with **AXS15231B QSPI** display driver, ESP32-S3-N16R8 (`16MB` flash / `8MB` PSRAM), and capacitive touch (AXS15231B touch controller on I2C). Use the `jc3248w535` firmware build. Supports **up to 2 printers** (up to 4 with the experimental opt-in - see Multi-Printer Monitoring). Includes an onboard **NS4168 I2S speaker** for notifications and a **battery indicator** (GPIO5 BAT divider), and supports portrait and landscape (480x320) layouts. Initial port contributed by Niels ([@theNailz](https://github.com/theNailz)). AliExpress: [pl.aliexpress.com/item/1005007566315926.html](https://pl.aliexpress.com/item/1005007566315926.html) |
| ![Waveshare 2 inch](img/waveshare2inch.png) | **Waveshare ESP32-S3-Touch-LCD-2** | `240x320` ST7789 version with ESP32-S3R8 (`16MB` flash / `8MB` PSRAM), sold as a more plug-and-play option. Use the `ws_lcd_200` firmware build. Supports **up to 2 printers** (up to 4 with the experimental opt-in - see Multi-Printer Monitoring). Product page: [waveshare.com/esp32-s3-touch-lcd-2.htm](https://www.waveshare.com/esp32-s3-touch-lcd-2.htm) Case (Horizontal and vertical): [MakerWorld model](https://makerworld.com/en/models/2773835) |
| ![Waveshare 1.54 inch](img/waveshare1.54inch.png) | **Waveshare ESP32-S3-Touch-LCD-1.54** | `240x240` ST7789 with ESP32-S3R8 (`16MB` flash / `8MB` PSRAM), touchscreen, battery holder, and 3 built-in buttons. Use the `ws_lcd_154` firmware build. Supports **up to 2 printers** (up to 4 with the experimental opt-in - see Multi-Printer Monitoring). The left button (BOOT) works as a screen switcher alongside the touchscreen. **Battery power:** press and hold the center PWR button to power on. To power off, hold the left (BOOT) and right buttons simultaneously for 1.5 seconds. Product page: [waveshare.com/esp32-s3-touch-lcd-1.54.htm](https://www.waveshare.com/esp32-s3-touch-lcd-1.54.htm) |
| ![ESP32-C3 board](img/ESP32c3Board.png) | **ESP32-C3 Super Mini** | DIY version, just like the main ESP32-S3 build, using the same `240x240` ST7789 display. Use the `esp32c3` firmware build. Due to RAM limits, this board supports **1 printer only**. |
| ![Round GC9A01 display](img/roundDisplay.jpg) | **ESP32-S3 / ESP32-C3 Super Mini + 1.28" GC9A01 round** | DIY variant with a round `1.28"` **GC9A01** (`240x240`) 7-pin SPI module and a dedicated round dashboard with three selectable print skins (Rim / Speedo / Rings, chosen in the web UI). Use the `esp32s3_round` build on an ESP32-S3 Super Mini (**up to 2 printers**) or the `esp32c3_round` build on an ESP32-C3 Super Mini (**1 printer only**). Wiring uses the same SPI pins as the square ST7789 builds - see the wiring table below. The 7-pin module has no backlight pin, so brightness control and night dimming are not available. Display module (AliExpress, pick the **Type Y** variant): [aliexpress.com/item/1005007702290129.html](https://aliexpress.com/item/1005007702290129.html) Case (ESP32-S3 and ESP32-C3 variants): [MakerWorld model](https://makerworld.com/en/models/3037776) |
| ![Waveshare ESP32-S3-Zero](img/es32s3zero.png) | **Waveshare ESP32-S3-Zero + 1.54" ST7789** | DIY version for the ESP32-S3FH4R2 module with `4MB` flash and `2MB` PSRAM. Use the `esp32s3_zero` firmware build. It uses the same external ST7789 wiring as the ESP32-S3 Super Mini build and supports **up to 2 printers** (up to 4 with the experimental opt-in). GPIO21 is occupied by the onboard WS2812 RGB LED. Product page: [waveshare.com/esp32-s3-zero.htm](https://www.waveshare.com/esp32-s3-zero.htm) |
| ![ESP32-S3-Zero with 2" 240x320 panel](img/esp32s3_zero_320.jpg) | **Waveshare ESP32-S3-Zero + 2.0" ST7789V (240x320)** | DIY variant of the ESP32-S3-Zero build using a larger `240x320` **ST7789V** panel module instead of the 1.54" 240x240. Same pinout as the standard `esp32s3_zero` build - only the panel differs (driven through the existing 240x320 layout). Use the `esp32s3_zero_320` firmware build. Supports **up to 2 printers** (up to 4 with the experimental opt-in). Display module (AliExpress): [pl.aliexpress.com/item/1005007523612119.html](https://pl.aliexpress.com/item/1005007523612119.html) |
| ![CYD display](img/CYD.png) | **CYD / ESP32-2432S028** (ILI9341) | `240x320` **ILI9341** all-in-one board. Use the `cyd` firmware build. Due to RAM limits, this board supports **1 printer only**. When flashing from [ESP Web Flasher](https://espressif.github.io/esptool-js/), set **Baudrate: 115200** before clicking **Connect**. If the first attempt fails, click **Disconnect** and then **Connect** again without unplugging the USB cable. If colors look reversed (white background instead of dark), enable **Invert display colors (fix white background)** in the web UI under **Display**. Case shown in the photo: [MakerWorld model](https://makerworld.com/models/2721746). |

### Additional Boards (Hardware Not Owned by Maintainer)

> Firmware for these boards exists and builds cleanly, but the maintainer doesn't physically own the hardware. They may require custom toolchain forks, aren't validated on every release, and individual features (e.g. touch) might still be experimental. For issues specific to these boards, please ping the listed maintainer / tester.

| Preview | Board | Notes | Maintainer / Tester |
|---|---|---|---|
| ![SenseCAP Indicator](img/sensecapBoard.png) | **Seeed SenseCAP Indicator** | `480x480` ST7701S RGB panel with ESP32-S3, FT5X06 capacitive touch, `8MB` flash with OPI PSRAM, all-in-one industrial enclosure. Use the `sensecap_indicator` firmware build. Supports **up to 2 printers** (up to 4 with the experimental opt-in). Requires custom forks of `arduino-esp32` (TCA9535 IO expander) and `LovyanGFX` (ST7701S RGB panel), both pinned to specific commits in `boards/sensecap_indicator.ini`. Product page: [seeedstudio.com/SenseCAP-Indicator-D1-p-5695.html](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5695.html) | [@kjames2001](https://github.com/kjames2001) |
| ![Waveshare 2.8 inch](img/ws_lcd_280.png) | **Waveshare ESP32-S3-Touch-LCD-2.8** | `240x320` ST7789 with ESP32-S3 and **CST328** capacitive touch (different chip from the 2.0" board, so the `ws_lcd_200` firmware will boot but the screen stays black). Use the `ws_lcd_280` firmware build. Same 240x320 layout as the 2.0", same ESP32-S3R8 core (`16MB` flash / `8MB` PSRAM) - supports **up to 2 printers** (up to 4 with the experimental opt-in). Battery / IMU / audio not wired up in firmware. Pinout from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8). Product page: [waveshare.com/esp32-s3-touch-lcd-2.8.htm](https://www.waveshare.com/esp32-s3-touch-lcd-2.8.htm) | [@FranciscoSaoMarcos](https://github.com/FranciscoSaoMarcos) |
| ![QD ES3N28P 2.8 inch](img/es3n28p-2.8.png) | **QD ES3N28P 2.8"** (ILI9341V) | `240x320` **ILI9341V** IPS panel over 4-wire SPI with ESP32-S3-N16R8 (`16MB` flash / `8MB` PSRAM) and **FT6336** capacitive touch on I2C. Use the `es3n28p` firmware build. Reuses the same `240x320` layout as the Waveshare 2"/2.8" boards and supports **up to 2 printers** (up to 4 with the experimental opt-in). The board carries an onboard WS2812 and ES8311 audio that the firmware does not drive, and the buzzer is disabled by default because its default pin is an I2S clock line. Sold as the QD electronic 2.8" IPS ESP32-S3 module (board codes ES3C28P / ES3N28P); see issue [#125](https://github.com/Keralots/BambuHelper/issues/125). | [@gwbuss](https://github.com/gwbuss) |
| ![Waveshare 3.5 inch](img/ws_lcd_350.png) | **Waveshare ESP32-S3-Touch-LCD-3.5** | `320x480` **ST7796** IPS panel over plain 4-wire SPI (native LovyanGFX, not the Guition board's AXS15231B QSPI), ESP32-S3R8 (`16MB` flash / `8MB` PSRAM), and **FT6336** capacitive touch on I2C. The LCD reset line is driven through an on-board **TCA9554** I2C IO expander rather than a GPIO. Use the `ws_lcd_350` firmware build. Reuses the same `320x480` layout as the JC3248W535 and supports **up to 2 printers** (up to 4 with the experimental opt-in). The board has no buzzer hardware. Pinout from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5). Product page: [waveshare.com/product/esp32-s3-touch-lcd-3.5.htm](https://www.waveshare.com/product/esp32-s3-touch-lcd-3.5.htm) | _community_ |
| ![Panlee SC05_X](img/sc05_x.jpg) | **Panlee SC05_X / ZX2D80CE02S 2.8"** | `240x320` **ST7789** IPS panel driven over an **8-bit 8080 parallel** bus, ESP32-S3 with `8MB` flash / QSPI PSRAM, and **FT5X06** capacitive touch on I2C. Use the `sc05_x` firmware build. This is not the 3.5" WT32-SC01 Plus; selecting the `wt32_sc01_plus` firmware on this board leaves the screen black because the LCD driver, resolution, backlight, reset, touch, and data pins are different. The onboard USB-C connector carries native ESP32-S3 USB data for serial and Improv; the separate 7-pin debug header exposes UART0, EN, and BOOT for the vendor's external ZXACC-ESPDB flashing adapter. Pinout and photo from the official Wireless-Tag / PanelLan datasheet and PanelLan Arduino library. | [@nunnypern](https://github.com/nunnypern) |
| ![Panlee WT32-SC01 Plus](img/SC01Plus3.5.png) | **Panlee WT32-SC01 Plus 3.5"** | `320x480` **ST7796** IPS panel driven over an **8-bit 8080 parallel** bus (the project's first `Bus_Parallel8` board, not SPI or QSPI), ESP32-S3-N16R8 (`16MB` flash / `8MB` PSRAM), and **FT6336** capacitive touch on I2C. Use the `wt32_sc01_plus` firmware build. Reuses the same `320x480` layout as the JC3248W535 and supports **up to 2 printers** (up to 4 with the experimental opt-in). The board has an I2S audio amp, but the buzzer is disabled by default because its default pin is the touch I2C clock line. Sold as the *WT32-SC01 Plus*. | [@cliomjh](https://github.com/cliomjh) |
| ![CYD TZT variant](img/CYD_tzt.png) | **CYD / TZT L1435-2.4** (ST7789) | Looks almost identical to the standard CYD, but uses a `240x320` **ST7789V** panel (instead of ILI9341) and the backlight is on GPIO27. Use the `tzt_2432` firmware build - the regular `cyd` build will give you a black screen on this hardware. Due to RAM limits, this board supports **1 printer only**. Often sold on Aliexpress as *"TZT ESP32 LVGL 2.4 inch LCD TFT 240*320 With Touch"*. | _community_ |

### Custom-Wired Displays (generic DIY env)

Wired your own SPI display to a bare ESP32 and it doesn't match any board above? Instead of editing C++, there is a **generic DIY build** whose display driver and pins come from build flags in a `boards/*.ini` file. Set your wiring once and build - no source changes.

**Supported drivers:** `GC9A01` (round 240x240), `ST7789` (240x240 or 240x320), `ILI9341` (240x320), `ST7796` (320x480).

**Two starter files** live in [`boards/`](boards/):

- **`boards/diy_round240.ini`** - a ready-to-use example for a GC9A01 round module on an ESP32-C3 Super Mini. Edit the five `DIY_PIN_*` lines to match your wiring and build with `pio run -e diy_round240`.
- **`boards/diy_round240_esp32.ini`** - the same round module on a **classic ESP32** (DevKitC / WROOM). Build with `pio run -e diy_round240_esp32`.
- **`boards/diy_spi_template.ini`** - a fully commented template (a working ST7789 240x320 example). Copy it to a new `boards/<yourname>.ini`, rename the `[env:...]`, and edit the flags.

**Required flags** in the env's `build_flags`:

```ini
-D BOARD_IS_DIY=1
-D DIY_PANEL_GC9A01=1        ; pick ONE panel driver
-D DISPLAY_ROUND_240=1       ; the layout that MATCHES the driver (see table)
-D DIY_PIN_SCLK=4            ; SPI clock
-D DIY_PIN_MOSI=3            ; SPI data (SDA)
-D DIY_PIN_DC=10             ; data/command
-D DIY_PIN_CS=1              ; chip select
-D BACKLIGHT_PIN=-1          ; a GPIO, or -1 if the backlight is hardwired on
```

**Driver &harr; layout pairing** (the build refuses to compile if they disagree, so you can't silently ship the wrong layout - which is what makes text fall off a round screen):

| Panel driver | Layout flag to set |
|---|---|
| `DIY_PANEL_GC9A01` | `DISPLAY_ROUND_240` |
| `DIY_PANEL_ST7789` (240x240) | *(none)* |
| `DIY_PANEL_ST7789` (240x320) | `DISPLAY_240x320` **+** `DIY_PANEL_H=320` |
| `DIY_PANEL_ILI9341` | `DISPLAY_240x320` |
| `DIY_PANEL_ST7796` | `DISPLAY_320x480` |

**Optional flags:** `DIY_PIN_MISO` / `DIY_PIN_RST` (default `-1`), `DIY_FREQ_WRITE` (default 40 MHz), `DIY_INVERT` (flip if colours are wrong), `DIY_RGB_ORDER=1` (if red/blue are swapped), `DIY_OFFSET_ROTATION` (0-7, if the image is mirrored/rotated), `DIY_OFFSET_X` / `DIY_OFFSET_Y` (default `0` - where the glass starts inside the controller's GRAM; set these if the image is *shifted* by a fixed number of pixels with a noisy or blank strip along one edge, e.g. `DIY_OFFSET_Y=80` on a 240x240 ST7789 bonded to the far end of its 240x320 die).

**Classic ESP32 (DevKitC / WROOM)** rather than S3/C3: add `board = esp32dev`, `board_build.partitions = min_spiffs.csv`, `-D BOARD_LOW_RAM=1` and `-D DIY_SPI_HOST=VSPI_HOST` (drop the `ARDUINO_USB_CDC_ON_BOOT` flag - classic ESP32 has no native USB). See the commented block in `diy_spi_template.ini`.

The button, buzzer, and status-LED pins default to *disabled* on a DIY build (their normal defaults often collide with a custom display's SPI pins), and the firmware refuses to assign any of them to a pin that's already driving the display. Configure them in the web UI once the device boots. These envs are user-compiled and are **not** part of the web flasher / release set.

## Features

- **One-click web flasher** - install firmware directly from [keralots.github.io/BambuHelper](https://keralots.github.io/BambuHelper/) in desktop Chrome/Edge - no PlatformIO, no esptool, no flash offsets
- **In-browser WiFi setup (Improv-Serial)** - on first boot the install dialog asks for your WiFi over USB; no captive-portal switching needed (AP fallback still available)
- **Bambu account sign-in on the device** - cloud setup without digging a token out of browser cookies: sign in with a password, an emailed code, or an authenticator app, then pick your printer from the account's own list
- **Live dashboard** - progress arc, temperature gauges, fan speed, layer count, time remaining
- **H2-style LED progress bar** - full-width glowing bar inspired by Bambu H2 series
- **Anti-aliased arc gauges** - smooth nozzle and bed temperature arcs with color zones
- **AMS visualization** - per-tray colors, drying status, plus an optional bottom-row strip view on 240x240 screens, configurable per printer
- **Smart-plug power monitoring** - per-printer Tasmota, Shelly Gen2/Gen3, Shelly Power Strip Gen4, or TP-Link Kasa legacy plug with live wattage, per-print kWh + cost, and optional auto-off after print finishes (with hot-end gate)
- **Button plug power control** - double-click the device button (or touchscreen) to switch the shown printer's smart plug on or off, with a hold-to-confirm safety screen - power a switched-off printer back on without opening a browser
- **Chamber light control** - manual on/off buttons in the web UI plus per-printer automation: light off after a successful or failed print (with delay), on when a print starts; dual-bar printers (H2C/H2D) switch both bars
- **Printer error reporting (HMS)** - a red `ERR` badge when the printer reports an HMS code or print error, a tap-to-open detail screen with the official Bambu wording, and optional edge-glow / buzzer / LED / wake-the-screen alerts
- **Animations** - loading spinner, progress pulse, completion celebration
- **Web config portal** - dark-themed settings page for WiFi, network, printer, display, power, buzzer, and LED settings
- **Network configuration** - DHCP or static IP, with optional IP display at startup
- **Display auto-off** - configurable timeout after print completion, auto-off when printer is off
- **NVS persistence** - all settings survive reboots
- **Auto AP mode** - creates WiFi hotspot on first boot or when WiFi is lost
- **Smart redraw** - only redraws changed UI elements for smooth performance
- **Customizable gauge colors** - per-gauge arc/label/value colors with preset themes
- **Customizable accent colors** - recolor the finish-time (ETA / Remaining) line, the "Print Complete!" headline, the healthy status badge and the printer name in every screen header, plus the two neutral text colors (readout text and the muted secondary text), so a themed screen has no stray green or white left in it. Warning, pause and error colors stay fixed on purpose
- **Customizable gauge labels** - rename any gauge label (Nozzle, Bed, Power, Layer, Door, AMS, dual-nozzle L/R) from the web UI; leave a field blank to keep the built-in default, with an optional smaller-label font for fitting longer names
- **European characters** - print file names, printer names, gauge labels, and the currency symbol render accented text (German, Polish, Czech, Hungarian, Turkish, Romanian, French, Spanish, Nordic, and more) plus the euro sign
- **Configurable gauge behavior** - adjustable arc full-scale ranges (nozzle, bed, chamber/AMS, power), arc smoothing speed, and an optional warning color when a gauge runs hot
- **Power gauge** - optional arc gauge showing live smart-plug wattage (W/kW) in a gauge slot
- **Configurable status bar** - alternate layer count / watts, always show watts, or always show the layer count; or hide the readout entirely to give the filament name more room
- **Per-nozzle temperature gauges** - fixed left/right nozzle arcs for dual-nozzle printers (H2 series)
- **Split dual-printer dashboard** - show two printers at once (stacked in portrait, side-by-side in landscape) with progress bar, status, ETA, and drying view per band
- **Multi-printer support** - monitor 2 printers simultaneously on ESP32-S3 boards; PSRAM-equipped boards add an experimental opt-in for up to 4 (see Multi-Printer Monitoring for the list); CYD, TZT, and ESP32-C3 have an experimental opt-in 2-printer mode but default to 1 printer
- **Smart rotation** - automatically shows the printing printer; cycles between both when both are printing
- **Physical button / touchscreen** - cycle printers and wake display via optional push button or TTP223, board-built-in buttons (Waveshare 1.54"), or the built-in capacitive touchscreens on CYD / TZT / Waveshare 2" / Waveshare 1.54"
- **Optional LED** - status LED on user-configurable pins: a single PWM channel, a 3-pin RGB LED or a WS2812, with a colour per printer state; hold the button/touch to dim
- **Optional buzzer** - passive buzzer notifications for print finished, connected, and error events; Waveshare 1.54" uses its built-in ES8311 audio codec and Guition JC3248W535 its onboard NS4168 I2S speaker instead
- **OTA updates** - update firmware from the device's web interface (manual upload or one-click from GitHub Releases)
- **Battery support (Waveshare 2" and 1.54")** - on-screen battery indicator, charging detection, hold-to-power-off
- **Exponential backoff** - reconnect attempts to offline printers gradually slow down to conserve resources

## Multi-Printer Monitoring

BambuHelper monitors 2 printers simultaneously on full-RAM ESP32-S3 boards, each over an independent MQTT connection. PSRAM-equipped boards can opt in to a third and fourth printer.

> **3-4 printers need PSRAM and an opt-in.** Each MQTT connection takes ~85 KB (TLS session + 40 KB message buffer). PSRAM-equipped S3 boards (esp32s3_zero, esp32s3_zero_320, ws_lcd_154, ws_lcd_200, ws_lcd_280, jc3248w535, ws_lcd_350, wt32_sc01_plus, es3n28p, sensecap_indicator) place the message buffers in PSRAM, leaving internal RAM free for extra TLS handshakes. They still **default to 2 printers**; printers 3 and 4 only appear after you enable **4-printer mode** in **Advanced > Danger zone**. This mode is **experimental and not yet validated with 3-4 live printers**, so expect the occasional disconnect, and note the split dual-screen layout still shows only the first two printers. The Waveshare Touch-LCD boards (1.54" / 2" / 2.8") gained PSRAM support in firmware after v3.7.4 - update to the latest release first. No-PSRAM S3 boards (esp32s3, esp32s3_round) run two - a third connection would exhaust internal RAM during the TLS handshake. The low-RAM boards (**CYD**, **TZT L1435-2.4**, **ESP32-C3** square and round) ship with a single printer slot by default, but expose an **experimental opt-in 2-printer mode** in **Printer Settings** - try it if you really need two, but expect tighter memory and the occasional disconnect under load.

### Rotation Modes

| Mode | Behavior |
|---|---|
| **Smart** (default) | Shows the printing printer. If both are printing, cycles between them. If neither is printing, shows last active. |
| **Auto-rotate** | Cycles through all connected printers at a configurable interval (10s - 10min). |
| **Off** | Manually switch between printers using the physical button only. |

### Split-Screen Mode

Instead of rotating between printers one at a time, you can show two printers at once. Enable **Split screen when two printers are printing** in the Multi-Printer section. When both configured printers are printing, the dashboard divides into two bands - stacked top/bottom in portrait, or left/right in landscape - each with its own progress bar, status label, ETA, and drying view. When only one printer is active it falls back to the normal full-screen dashboard. A **Always show split screen (testing)** toggle forces the split layout regardless of activity so you can preview it without two live prints.

### Physical Button

An optional physical button can be connected to cycle between printers and wake the display from sleep.

| Type | Wiring | How it works |
|---|---|---|
| **Push button** | One pin to configured GPIO, other pin to GND | Active LOW with internal pull-up |
| **TTP223 touch sensor** | VCC->3.3V, GND->GND, SIG->configured GPIO | Active HIGH |

The button type and GPIO pin are configurable in the web interface (Multi-Printer section) - no recompilation needed.

The same button (or the built-in touchscreen) can also switch the printer's smart plug on and off - see **Power Monitoring -> Button power control** below.

### MQTT Reconnect Backoff

When a printer is physically powered off, BambuHelper uses exponential backoff to avoid wasting resources on repeated connection attempts:

| Phase | Attempts | Interval |
|---|---|---|
| Normal | First 5 | Every 10 seconds (LAN) / 30 seconds (Cloud) |
| Phase 2 | Next 10 | Every 60 seconds |
| Phase 3 | Beyond 15 | Every 120 seconds |

When the printer comes back online, the backoff resets once the connection has held for 30 seconds. A connection that drops again within 30 seconds keeps the slower pace - this protects against a broker that accepts and immediately rejects the session (e.g. an expired cloud token or a duplicate client), which would otherwise hammer the server at full speed forever.

## Power Monitoring

| | |
|---|---|
| <img src="img/PowerMonitoring.png" width="360" alt="Power Monitoring"> | BambuHelper can display live power consumption from a **[Tasmota](https://tasmota.github.io/docs/)-flashed smart plug**, a **Shelly Gen2/Gen3 plug**, a **Shelly Power Strip Gen4**, or a **TP-Link Kasa legacy-protocol plug** connected to your printer. All communication stays on the local network - no smart-plug cloud connection is required.<br><br>**What it shows:**<br>- Live wattage in the bottom status bar on the idle and printing screens<br>- Total kWh used during the print job, shown on the "Print Complete" screen<br><br>**Setup:** open the web interface, go to **Power Monitoring**, pick the plug type, enter the plug's reserved local IP address, set your preferred poll interval (10-30s), and choose how the status bar shows it - alternate watts with the layer counter, always show watts, or always show the layer count (when power has its own gauge). For the Shelly Power Strip Gen4, also pick which output (0-3) this slot tracks/controls.<br><br>**Requirements:** any Tasmota-flashed smart plug with energy monitoring (e.g. Sonoff S31, BlitzWolf BW-SHP6, Nous A1), a Shelly Gen2/Gen3 plug (Plus Plug S, Plug S Gen3, etc.), a Shelly Power Strip Gen4, or a TP-Link Kasa plug using the legacy local protocol on TCP port 9999 (tested with KP115; HS110 uses the same protocol). Newer Kasa KLAP/Matter models are not supported. The plug must be on the same reachable network as the ESP32. **Shelly/Kasa notes:** Shelly must not be password-protected (digest auth is unsupported). Their realtime APIs report cumulative Total but not Today's/Yesterday's energy, so those fields stay blank.<br><br>**Auto power-off:** each plug can power itself off N minutes (1-240) after the print finishes, with a 50&nbsp;°C nozzle gate so it never triggers while the hot end is hot. Configure under **Power Monitoring -> Auto-off**.<br><br>If you like to harden your Tasmota plug you can use the following rule. <br><i>Rule1 on Energy#Power>=30 do Backlog Powerlock 1 endon on Energy#Power<30 do Backlog Powerlock 0 endon</i><br><br>Threshold is 30W. Change it according to the idle consumption of your setup. Every action is blocked if there is consumption above this threshold (power button, MQTT, web interface, etc.).
| <img src="img/ButtonPowerControl.png" width="360" alt="Button power control"> | **Button power control (on-device plug switch):** switch the plug on or off straight from the device - no browser needed. Enable **Button power control** under **Power Monitoring** (off by default), then **double- or triple-click** the device button or touchscreen while the printer is on screen. A full-screen confirmation opens: **hold ~1.5 s** until the ring fills to toggle the plug; a short tap (or 10 s without input) cancels. The screen turns **red** when that printer is currently printing, since confirming would cut power to a live print. It also works from the "Connecting to printer" screen, so a switched-off printer can be powered back on - the main use case. Only active when a plug is mapped to the shown printer; while armed, single-tap printer switching waits ~0.5 s for a possible second click. |

## Chamber Light Control

| | |
|---|---|
| <img src="img/ChamberLight.png" width="360" alt="Chamber light"> | BambuHelper can control the printer's **chamber light** over LAN and Cloud connections (X1, P-series, H2 series). Each printer card in the web UI has a **Chamber light** section with manual **Light On / Light Off** buttons and per-printer automation:<br><br>- **Turn off after a successful print**<br>- **Turn off after a failed or cancelled print**<br>- **Turn on when a print starts**<br><br>The off rules share a configurable **delay** (0 = immediate), so the light stays on long enough to eyeball the finished print. If a new print starts during the delay, the pending off is cancelled. On dual-bar printers (H2C/H2D) both light bars switch together. |

## Printer Errors (HMS)

When a printer reports a problem, it publishes an **HMS code** (hardware/maintenance system) or a **print error**. BambuHelper decodes both and turns the raw hex into the same sentence Bambu Studio shows.

**On the display:** the status badge turns into a red **`ERR`**. Press the button or touchscreen to open the error screen, which lists each active code as a coloured severity bar, a `MODULE SEVERITY` line (for example `AMS SERIOUS`), the code on the line below it (`0701-2000-0002-0021`), and the official description underneath. Long descriptions wrap and end in `...` when they run out of room; entries that do not fit are counted as `+N more`. Round 240x240 screens have room for the worst entry only, plus the count. The screen closes on the next tap or after 30 seconds.

**In the web interface**, the **Printer Errors** section lists everything the printers are reporting right now, per printer, with a `wiki` link on each row. Codes are written the way Bambu writes them - `HMS_0701-2000-0002-0021` - so they can be pasted straight into the wiki or a search engine. Codes that were already active when the device connected are tagged `standing` - they are the printer's normal state, not news, so they never raise an alert.

**Codes Bambu does not describe are not shown.** Bambu's error feed registers some code numbers without publishing any wording for them, and its own printer screen and Bambu Studio stay silent about those - an X2D, for instance, stands permanently on `HMS_0500-0100-0002-000B` and `HMS_0503-0000-0003-0027` while reporting nothing wrong. BambuHelper drops them the same way, so they never reach the badge, the error screen or the web interface. An open door is hidden too, on printers that show it in the door indicator instead. Two exceptions, both deliberate: a **fatal** code is always shown even if nothing describes it, because a stopped machine reporting nothing is worse than a stopped machine reporting a bare number; and print errors are never hidden, because they mean the job actually stopped. If a printer reports a code and BambuHelper shows nothing, `GET /debug` lists the dropped ones under `hms_suppressed_codes`, and the serial log names every one of them with **MQTT debug logging** on.

### Settings

| Setting | What it does |
|---|---|
| **Report printer errors** | Master switch. Off hides the badge, the error screen and every alert. Codes keep being read, so switching it back on takes effect immediately. |
| **Include low-priority codes** | Off shows only fatal and serious codes. On adds the common ones - advisories the printer expects you to notice but not to act on immediately. |
| **Show the error screen automatically** | *Never - badge only* / *Briefly, then go back* / *Until dismissed*. |
| **Alert on a new error** | Any combination of edge glow, buzzer, status LED, and waking a sleeping screen. Alerts fire once per new code, never repeatedly. |
| **Look up error text on this page** | Only appears on boards that do not store the HMS sentences - see below. |

### Where the text comes from

The HMS table is large - roughly 4000 codes and 400 KB - so it is compiled in only on boards with 8 MB or 16 MB of flash. The print-error table is smaller (about 540 codes, ~36 KB) and fits everywhere except the ESP32-C3, which is the tightest board in the range:

| Boards | Error text on the display | Error text in the web interface |
|---|---|---|
| Waveshare 1.54" / 2" / 2.8" / 3.5", Guition JC3248W535, WT32-SC01 Plus, QD ES3N28P, Panlee SC05_X, SenseCAP Indicator | Print errors **and** HMS codes | From the device, no internet needed |
| ESP32-S3 Super Mini and ESP32-S3-Zero (square and round), CYD, TZT L1435-2.4, DIY builds | Print errors only. HMS codes show the code, severity and module | Fetched by **your browser**, once per visit, from `keralots.github.io` |
| ESP32-C3 Super Mini (square and round) | Code, severity and module | Fetched by **your browser**, once per visit, from `keralots.github.io` |

On the last two groups, **the device itself never goes online for this** - the config page you are looking at does the lookup, so an isolated printer network changes nothing. Untick **Look up error text on this page** to stop even that; every row keeps its code, severity, module and wiki link.

> **About the wiki links:** Bambu's wiki documents only a small fraction of the HMS codes (about 225 of 4000). Rows for a documented code link straight to its page; every other row links to the [HMS index](https://wiki.bambulab.com/en/hms/home), where you can search. The device screen always shows the full code, so you can look it up anywhere.

## Hardware Assembly for the DIY Version (ESP32-S3 Super Mini)

> **If you bought an all-in-one board** (CYD, TZT L1435-2.4, Waveshare 2" or 1.54", SenseCAP Indicator), **skip this section** - everything is already wired on the PCB. The tables below apply only to the **DIY** builds (ESP32-S3 SuperMini, ESP32-S3-Zero, ESP32-C3 SuperMini) that need an external display soldered up - either the square 1.54" ST7789 or the round 1.28" GC9A01.

AliExpress links (DIY parts):
- Display 1.54" ST7789: https://a.aliexpress.com/_EG9y7wc
- Display 1.28" GC9A01 round: https://aliexpress.com/item/1005007702290129.html (pick the **Type Y** variant)
- ESP32-S3 SuperMini: https://a.aliexpress.com/_Eyk9GdA (the **S3** variant supports 2 printers; the **C3** variant supports 1 printer. If you use C3, the wiring is different; check the default wiring table)
- Case for the 1.54" DIY build: https://makerworld.com/en/models/2501721

Optional accessories - all configurable from the web interface, none required:
- **Touch / push button** (TTP223 or standard push button) for cycling printers and waking the display. See the wiring section below.
- **Passive buzzer / mini speaker** for print-finished, connected, and error notifications. See the wiring section below.
- **Status LED** (any common LED with a series resistor, an RGB LED or a WS2812 pixel) for at-a-glance progress / connection state. See the wiring section below.

Links for optional accessories:
- TTP223 touch button: https://aliexpress.com/item/1005006246380749.html
- LDO6AJSA LED driver: https://aliexpress.com/item/1005005344841325.html
- Filament LED 3V 39 mm: https://aliexpress.com/item/1005007883702652.html
- Passive buzzer: https://aliexpress.com/item/1005010400627387.html

### Default Wiring

| Display Pin ST7789 (240x240) | ESP32-S3 GPIO | ESP32-S3-Zero GPIO | ESP32-C3 GPIO |
|---|---|---|---|
| MOSI (SDA) | 11 | 11 | 20 |
| SCLK (SCL) | 12 | 12 | 21 |
| CS | 10 | 10 | 6 |
| DC | 9 | 9 | 7 |
| RST | 8 | 8 | 10 |
| BL | 13 | 13 | 5 |
| GND | GND | GND | GND |
| VCC | 3.3V | 3.3V | 3.3V |

**Round 1.28" GC9A01 display** (`esp32s3_round` / `esp32c3_round` builds) - same SPI pins as the ST7789 table above, but the 7-pin module has **no BL pin** (backlight is hardwired on, so brightness control is not available):

| Display Pin GC9A01 (1.28" round) | ESP32-S3 GPIO | ESP32-C3 GPIO |
|---|---|---|
| SDA (MOSI) | 11 | 20 |
| SCL | 12 | 21 |
| CS | 10 | 6 |
| DC | 9 | 7 |
| RST | 8 | 10 |
| GND | GND | GND |
| VCC | 3.3V | 3.3V |

Adjust pin assignments in `platformio.ini` `build_flags` to match your wiring (only needed if you are flashing from source; the prebuilt binaries use the defaults above).

> **ESP32-S3-Zero:** GPIO21 is connected to the onboard WS2812 RGB LED. It cannot be reused as a plain status-LED GPIO, but the status LED can drive that pixel directly - pick the WS2812 wiring under **Status LED**.

### Optional Input: Button, Touch Sensor, or Touchscreen

Cycles between printers, wakes the display from sleep, and (when held) dims the optional status LED. All input methods are configured from the web interface under **Multi-Printer** - no recompilation needed.

**Built-in touchscreens** (no wiring needed):
- **CYD / ESP32-2432S028** - XPT2046 resistive touch, automatic
- **TZT L1435-2.4** - XPT2046 resistive touch, same pins as CYD, automatic
- **Waveshare ESP32-S3-Touch-LCD-2** - CST816D capacitive on I2C (GPIO48/47), automatic
- **Waveshare ESP32-S3-Touch-LCD-1.54** - CST816 capacitive on I2C (GPIO42/41), plus three hardware buttons - BOOT (GPIO0), PWR centre (GPIO5), AUX (GPIO4)
- **Waveshare ESP32-S3-Touch-LCD-2.8** - CST328 capacitive on I2C (GPIO1/3), reset on GPIO2, automatic
- **Guition JC3248W535** - AXS15231B capacitive touch on I2C (GPIO4/8), polled (no IRQ wired), automatic

**External TTP223 capacitive touch sensor** (DIY ESP32-S3 / ESP32-S3-Zero / ESP32-C3 builds):

| TTP223 Pin | ESP32-S3 GPIO | ESP32-S3-Zero GPIO | ESP32-C3 GPIO |
|---|---|---|---|
| `VCC` | `3.3V` | `3.3V` | `3.3V` |
| `GND` | `GND` | `GND` | `GND` |
| `SIG` | `GPIO 4` | `GPIO 4` | `GPIO 4` |

**Standard push button:** connect one leg to `GPIO 4` and the other to `GND`. The internal pull-up is enabled automatically. Select **Push Button** in the web interface.

### Optional Buzzer Wiring

The buzzer is completely optional. If you do not connect one, BambuHelper works normally.

Use a **passive buzzer** (or a mini speaker on CYD) and connect it like this:

| Buzzer Pin | ESP32-S3 GPIO | ESP32-C3 GPIO | CYD GPIO |
|---|---|---|---|
| `+` / `SIG` | `GPIO 5` | `GPIO 3` | `GPIO 26` |
| `-` / `GND` | `GND` | `GND` | `GND` |

> **CYD speaker:** The CYD board has a dedicated speaker connector on the PCB - just plug a mini speaker into it and set the buzzer pin to `GPIO 26` in the web interface.

> **Note:** The firmware default buzzer pin is `GPIO 5` on both ESP32-S3 and ESP32-C3. The table above shows the **recommended wiring**. If you wire an ESP32-C3 buzzer to `GPIO 3`, you must change the buzzer pin to `GPIO 3` in the web interface after the first boot.

> **Waveshare ESP32-S3-Touch-LCD-2.0:** the generic `GPIO 5` above does **not** apply - that pin is the battery ADC on this board. Wire the buzzer to `GPIO 4`, which is this board's default from v3.7.7 on. On firmware up to v3.7.6 the board incorrectly defaulted to the CYD's `GPIO 26` (a flash/PSRAM bus pin on the ESP32-S3, so the buzzer stayed silent); after updating, the stored pin is migrated to `GPIO 4` automatically, or you can set it by hand under **Buzzer**.

> **Waveshare ESP32-S3-Touch-LCD-2.8 / ESP32-S3-Zero (320):** same pre-v3.7.7 `GPIO 26` problem. The 2.8" board now starts with the buzzer pin **disabled** (`0`) because `GPIO 5` is its backlight and `GPIO 4` its touch IRQ - pick a free GPIO in the web interface. The S3-Zero 320 build uses `GPIO 5` like the 240x240 version.
You can change the buzzer GPIO later in the web interface under **Buzzer**. The buzzer can be used for print-finished, connected, and error notifications.

> **Waveshare ESP32-S3-Touch-LCD-1.54** has a built-in **ES8311 audio codec** + speaker on its own pins (no buzzer GPIO to set) - it produces the same notifications using the onboard amplifier. No external buzzer needed.

### Optional Status LED Wiring

A status LED can be wired to any free GPIO for at-a-glance notifications (connection, progress, finish, error). **Every board supports all three wirings below** - including the flash-tight ESP32-C3 boards, which now carry the WS2812 driver too - so you can solder on a single, RGB, or addressable LED on any hardware. Pick the wiring under **Status LED** in the web interface and set the behaviour (heartbeat, finish flash, off) there too. Three wirings are supported:

| Wiring | Pins | Colour |
|---|---|---|
| **Single colour** | one GPIO | brightness only |
| **RGB LED** | three GPIOs, one per channel | full colour per printer state |
| **WS2812 pixel** | one data GPIO | full colour per printer state |

Wiring a single-colour LED is done using: LD06AJSA Constant Current driver

Things to know:

- The pin is set in the web UI - there is no default. The setting starts **disabled** with pin `0`.
- The firmware refuses to attach the LED to the configured buzzer pin or the configured button pin (it will silently disable LED output to avoid a conflict).
- On the **Waveshare ESP32-S3-Touch-LCD-2.0**, `GPIO 2` is free and is a good choice for the LED driver's control input. The firmware rejects the pins this board already uses: `1` (backlight), `5` (battery ADC), `19`/`20` (USB), `26`-`37` (flash/PSRAM), `38`/`39`/`40`/`42`/`45` (display SPI) and `47`/`48` (touch I2C).
- The LED is also a dimmer target: **hold the optional button / touchscreen** to ramp brightness down/up while the LED is on. The chosen brightness is debounced and saved to NVS after ~2 s of release.
- For a **single-colour** LED, inverted-logic wiring (LED to VCC instead of GND) is not supported - the firmware always drives the pin active-HIGH. An **RGB** LED has a *Common anode* tick for exactly this, because most RGB packages are wired that way.

#### Colour per printer state

With the RGB or WS2812 wiring selected, the card grows a colour picker for each printer state. Defaults are white idle, blue printing, amber paused, green finished, red error. The brightness slider still sets the overall level, and every existing effect - pause breathing, finish pulse, error strobe, hold-to-dim - keeps working, just in colour.

Colours preview live on the LED as you pick them, even when the panel has gone to sleep.

#### Onboard RGB LEDs

Several supported boards already have an RGB LED soldered on, and the card offers a **Use this board's onboard RGB LED** button that fills in the right pins:

| Board | Type | Pins |
|---|---|---|
| **CYD** (ESP32-2432S028) and **TZT L1435-2.4** | 3-pin, common anode | `4` (red, or `22` on the ESP32-32E clone), `16`, `17` |
| **ESP32-S3-Zero** | WS2812 | `21` |
| **QD ES3N28P** | WS2812 | `42` |

These pins are otherwise reserved and cannot be picked for a single-colour LED - the colour driver is the only thing allowed to claim them. On the CYD they were previously left floating, which is why an untouched board sits there glowing dim red; the firmware now parks them dark whenever the LED feature is not using them.

> **ESP32-C3 boards** get the single-colour and 3-pin RGB wirings only. The WS2812 driver needs the RMT peripheral driver, which does not fit in the 1.75 MB app slot those boards have.

### Complete wiring example for ESP32-S3 Super Mini and ST7789 (240x240) display.

![wiring](img/wiring.png)

### Assembly Video

[![Assembly Video](https://img.youtube.com/vi/hsyamsU5UZE/maxresdefault.jpg)](https://youtu.be/hsyamsU5UZE)

## Requirements

- For flashing: a desktop browser (Chrome or Edge) is enough - use the [web flasher](https://keralots.github.io/BambuHelper/). [PlatformIO](https://platformio.org/) is only needed if you want to modify the firmware yourself.
- **LAN mode:** Bambu Lab printer with LAN mode enabled, printer and ESP32 on the same local network
- **Cloud mode:** Bambu Lab account, ESP32 with internet access

## Flashing

### Easy: BambuHelper Web Flasher (recommended for first-time setup)

Open **[keralots.github.io/BambuHelper](https://keralots.github.io/BambuHelper/)** in Chrome or Edge on a desktop or laptop, pick your board, plug it in over USB, and click **Install**. That's it - no downloads, no offsets, no baudrate dialogs.

After the flash, the install dialog runs a 3-minute **Configure WiFi** step right in the browser using Improv-Serial - type your home SSID/password and the device joins your network without you ever having to connect to the captive portal. The device still falls back to AP mode (showing the SSID and password on its screen) if you dismiss the dialog or run out of time.

Supports the 16 most common boards (ESP32-S3 SuperMini with 1.54" square or 1.28" round panel, ESP32-S3-Zero with 1.54" or 2.0" panel, ESP32-C3 SuperMini with 1.54" square or 1.28" round panel, Waveshare ESP32-S3-Touch-LCD-2, Waveshare ESP32-S3-Touch-LCD-1.54, Waveshare ESP32-S3-Touch-LCD-2.8, QD ES3N28P 2.8", Panlee SC05_X / ZX2D80CE02S 2.8", Waveshare ESP32-S3-Touch-LCD-3.5, Panlee WT32-SC01 Plus, Guition JC3248W535, CYD / ESP32-2432S028, TZT L1435-2.4). For the community-maintained SenseCAP Indicator use the manual flow below.

### Manual: Generic ESP Web Flasher

1. Download the latest firmware from [Releases](../../releases). **If you are flashing a new device for the first time**, use the file ending with **-Full** (for example `BambuHelper-esp32s3-v3.7.5-Full.bin`). The regular `-ota.bin` file is for OTA updates on devices that already have BambuHelper installed.
2. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge
3. If you are flashing a **CYD** or **TZT L1435-2.4**, set **Baudrate** to **115200** before clicking **Connect**. Two or more attempts may be needed - the first one will fail. This applies to both CYD-shaped boards (they use a CH340 USB-Serial chip that does not tolerate high baud rates on first contact).
4. Connect your ESP32 via USB
5. Click **Connect** and select your device
6. Set flash address to **0x0**
7. Select the downloaded `.bin` file
8. Click **Program**

### Updating an Existing Device (OTA)

Once you have BambuHelper running, you do not need to re-flash over USB to update. From the device's web interface:

1. Open the device's IP in a browser
2. Scroll to **Other** -> **OTA Update**
3. Click **Check for updates** - the device queries GitHub Releases and, if a newer build is available, shows a one-click **Install Update** button that pulls the matching `*-ota.bin` straight from the release
4. If you prefer to upload manually (e.g. a custom build), the same panel accepts a local `*-ota.bin` file via drag-and-drop

The device reboots automatically once the update is written; the web page reloads when it comes back online.

### Build Files

| Board | Use this `Full` file for first flash / recovery |
|---|---|
| ESP32-S3 Super Mini | `BambuHelper-esp32s3-v3.7.4-Full.bin` |
| ESP32-S3 Super Mini + 1.28" GC9A01 round | `BambuHelper-esp32s3_round-v3.7.4-Full.bin` |
| Guition JC3248W535 | `BambuHelper-jc3248w535-v3.7.4-Full.bin` |
| Waveshare ESP32-S3-Zero | `BambuHelper-esp32s3_zero-v3.7.4-Full.bin` |
| Waveshare ESP32-S3-Zero + 2.0" 240x320 panel | `BambuHelper-esp32s3_zero_320-v3.7.4-Full.bin` |
| CYD / ESP32-2432S028 | `BambuHelper-cyd-v3.7.4-Full.bin` |
| TZT L1435-2.4 | `BambuHelper-tzt_2432-v3.7.4-Full.bin` |
| Waveshare ESP32-S3-Touch-LCD-2 | `BambuHelper-ws_lcd_200-v3.7.4-Full.bin` |
| Waveshare ESP32-S3-Touch-LCD-1.54 | `BambuHelper-ws_lcd_154-v3.7.4-Full.bin` |
| Waveshare ESP32-S3-Touch-LCD-2.8 | `BambuHelper-ws_lcd_280-v3.7.4-Full.bin` |
| QD ES3N28P 2.8" | `BambuHelper-es3n28p-v3.7.4-Full.bin` |
| Panlee SC05_X / ZX2D80CE02S 2.8" | `BambuHelper-sc05_x-v3.7.5-Full.bin` |
| Waveshare ESP32-S3-Touch-LCD-3.5 | `BambuHelper-ws_lcd_350-v3.7.4-Full.bin` |
| Panlee WT32-SC01 Plus 3.5" | `BambuHelper-wt32_sc01_plus-v3.7.4-Full.bin` |
| ESP32-C3 Super Mini | `BambuHelper-esp32c3-v3.7.4-Full.bin` |
| ESP32-C3 Super Mini + 1.28" GC9A01 round | `BambuHelper-esp32c3_round-v3.7.4-Full.bin` |

> The SenseCAP Indicator is not part of the automated release pipeline - build it locally with `pio.exe run -e sensecap_indicator` and flash the resulting `.pio/build/sensecap_indicator/firmware.bin`.

## Setup

### Configuration Guide

[![Configuration Guide](https://img.youtube.com/vi/n2RdbeHTMz0/maxresdefault.jpg)](https://youtu.be/n2RdbeHTMz0)

> **If you used the web flasher**, steps 2-4 happen automatically in the install dialog (Configure WiFi step). The device joins your home WiFi straight away and the dialog gives you a link to its IP - jump to step 5. The AP captive-portal path below is the fallback when you skipped or timed out of the Configure WiFi dialog, or when you flashed via the generic ESP Web Flasher.

1. **Flash** the firmware (see above)
2. **Connect** to the `BambuHelper-XXXX` WiFi network (password: `bambu1234`)
3. **Open** `192.168.4.1` in your browser
4. **Enter** your home WiFi credentials and **Save** - the device restarts and connects to your WiFi
5. **Note the IP address** shown on the ESP32 display after it connects to WiFi
6. **Open** that IP address in your browser to access the full web interface
7. **Configure your printer:**

   **LAN Direct** (P1P, P1S, X1, X1C, X1E, A1, A1 Mini):
   - Printer IP address (found in printer Settings > Network)
   - Serial number (see note below)
   - LAN access code (8 characters, from printer Settings > Network)

   **Bambu Cloud (All printers)**:
   - Under **Account access**, either **sign in with your Bambu account** or paste an access token (see [Connecting to Bambu Cloud](#connecting-to-bambu-cloud) below)
   - Click **My printers** and pick yours from the list, or enter the serial number by hand (see note below)

   > **Important: Serial number is NOT the printer name.** The serial number is a 15-character code (for example `01P00A000000000`) found on the printer LCD under **Settings > Device > Serial Number**, or on the physical label on the back or bottom of the printer. Do not confuse it with the printer name shown in Bambu Studio (for example `3DP-01P-110`), which is a shortened version and will not work.

8. **Save Printer Settings** - the device connects to your printer

### Connecting to Bambu Cloud

Cloud mode runs on an access token from your Bambu Lab account. There are three ways to put one on the device - all of them end in the same stored token, so pick whichever suits you. Read the [Cloud Mode Security Notice](#cloud-mode-security-notice) first if you plan to type your password into the portal.

#### 1. Sign in on the device (no copying anything)

In the web interface, open **Printer**, set the connection mode to **Bambu Cloud**, and under **Account access** choose **Sign in with your Bambu account**. Then either:

- **Email code** - enter your account email and press **Email me a code**. Bambu sends a 6-digit code; type it in. No password is involved at any point.
- **Password** - enter your email and password. If your account uses two-factor authentication, the device asks for the code next: either the 6 digits from your authenticator app, or a code Bambu emails you, depending on how your account is set up. A wrong code can simply be retyped.

Tick **Remember the password** only if you want the device to renew the token by itself every ~3 months. That works only on accounts **without** two-factor authentication - with 2FA the device deletes the password as soon as it sees the prompt, because it could never complete the sign-in unattended anyway.

Once signed in, press **My printers** to pick your printer from the account's own list instead of transcribing a serial number.

#### 2. Companion Tool (sign in on your PC, push to the device)

The [Companion Tool](tools/DIAGNOSTICS-HOWTO.md) (`tools/BambuHelper-CompanionTool.exe` on Windows, `python tools/companion/bambu_companion.py` on Mac/Linux) signs in on your computer, fetches your printer list, and pushes the token + serial to BambuHelper over your LAN. It is a normal app window, not a terminal: open **Set up a device**, sign in, pick your printer, and let it scan your network for the device rather than typing its IP. Useful when you would rather not type your password into the device's portal at all.

#### 3. Paste an access token (the original method)

Copy the token from your browser cookies on https://bambulab.com (you must be logged in), then paste it into **Account access -> Paste an access token**.

**Using browser DevTools (Chrome / Edge):**
1. Open https://bambulab.com and log in to your account
2. Press **F12** to open DevTools
3. Go to the **Application** tab (click `>>` if you do not see it)
4. In the left sidebar, expand **Cookies** -> click `https://bambulab.com`
5. Find the row named `token` in the cookie list
6. Double-click the **Value** cell to select it, then **Ctrl+C** to copy
7. Paste the value into BambuHelper's "Access Token" field in the web interface

**Using browser DevTools (Firefox):**
1. Open https://bambulab.com and log in to your account
2. Press **F12** to open DevTools
3. Go to the **Storage** tab
4. In the left sidebar, expand **Cookies** -> click `https://bambulab.com`
5. Find the row named `token`
6. Double-click the **Value** cell to select it, then **Ctrl+C** to copy
7. Paste the value into BambuHelper's "Access Token" field

**Using browser DevTools (Safari):**
1. Open https://bambulab.com and log in to your account
2. Open **Develop** -> **Show Web Inspector** (enable the Develop menu first in Safari Preferences -> Advanced)
3. Go to the **Storage** tab -> **Cookies** -> `bambulab.com`
4. Find and copy the `token` value
5. Paste it into BambuHelper's "Access Token" field

> **Note:** The token is valid for approximately 3 months. When it expires, the ESP32 will fail to connect - sign in again on the device, or repeat the process above and paste a fresh token. Make sure the **Server region** matches your Bambu account (Europe / Americas share one setting; China is separate).

> **Browser cookie token expires very quickly (after one session, on next reboot, etc.)?** Sign in on the device or use the Companion Tool instead - tokens obtained that way tend to be more stable than browser cookies that get invalidated unexpectedly soon after extraction.

## Web Interface

The built-in configuration portal (open the device's IP in any browser) covers everything - no recompiling, no config files. Every setting has inline help text, so the summary below only maps out where things live:

<img src="img/WebInterface.png" width="760" alt="Web interface">

| Sidebar section | What's inside |
|---|---|
| **Printer** | One tab per printer slot: LAN or Cloud connection (serial, access code, or a Bambu account sign-in / pasted token), the account's printer list and a LAN scan to fill the serial in, live status readout, per-printer gauge layout for the print screen plus the Ready / Print complete pair, AMS view, chamber light control |
| **Display** | Brightness and night mode, screen rotation, after-print behavior, clock screen, screensaver, and Gauge Appearance: theme presets, per-gauge colors, accent colors (finish time, print complete, status OK, printer name, text, muted text), custom labels (accented European characters supported) |
| **Hardware** | Printer rotation and split-screen mode, external button / TTP223, buzzer with quiet hours, status LED, detected-hardware readout |
| **Printer Errors** | Live list of every HMS code and print error the printers report, plus the badge / error-screen / alert settings - see Printer Errors |
| **Advanced** | Gauge full-scale ranges and behavior (smoothing, warning color), clock-screen info footer, and the Danger zone: reboot, factory reset, experimental multi-printer opt-ins |
| **WiFi & System** | WiFi credentials, DHCP / static IP, mDNS hostname, settings backup (JSON export / import), firmware update - one-click install from GitHub Releases or manual `.bin` upload |
| **Power** | Smart-plug power monitoring: Tasmota / Shelly / TP-Link Kasa plug slots, tariff and currency, auto power-off, button power control, live plug stats |
| **Diagnostics** | Live connection state per printer and a verbose serial-logging toggle |

## Dashboard Screens

| Screen | When |
|---|---|
| Splash | Boot (2 seconds) |
| AP Mode | First boot / no WiFi configured |
| Connecting WiFi | Attempting WiFi connection |
| WiFi Connected | Shows IP for 1.5 seconds (if enabled) |
| Connecting Printer | WiFi connected, waiting for MQTT |
| Idle | Connected, printer not printing |
| Printing | Active print with full dashboard |
| Printer error | Tap the button/touchscreen while the `ERR` badge is up, or automatically if configured - see Printer Errors |
| Finished | Print complete with animation (auto-off after timeout) |
| Clock | After finish timeout (if enabled) - shows digital clock with date |
| Display Off | After finish timeout (if clock disabled) or printer powered off |

## Display Power Management

The display is managed from the **Display** section of the web interface (see above for the full list of fields). In short:

- After a print completes, the finish screen is shown for the configured number of minutes (default 3), then the **After the finish screen** setting decides what happens next: the digital clock / screensaver (default), or **display and status LED fully off**. The off choice needs a button or touchscreen configured so the device can be woken; without one the clock is shown regardless.
- When the printer goes offline (powered off or disconnected), the display stays in whatever state it was in - it does not flicker back to the connecting screen.
- When the printer comes back online or starts a new print, the display wakes automatically (including from the full-off state).
- **Keep display always on** overrides the auto-off behaviour.
- For a dark-but-alive alternative to full off, set **Screensaver brightness** to 0 - the clock keeps running with the backlight dark.

## Custom Smooth Fonts

BambuHelper embeds smooth fonts directly in the firmware as VLW tables in `PROGMEM`. The default font is **Inter** (Regular for small/body, Bold for large headings), shipped as TTF in `fonts/` and pre-converted to C headers in `include/fonts/`. Swapping the font means regenerating those headers and reflashing - there is no runtime upload, because the font lives in flash next to the code.

Steps:

1. Drop your `.ttf` files into `fonts/` (e.g. `MyFont-Regular.ttf`, `MyFont-Bold.ttf`).
2. Edit the `FONTS` list in [`scripts/generate_vlw_fonts.py`](scripts/generate_vlw_fonts.py) - each entry is `(output_name, ttf_filename, pixel_size)`. Keep the names `inter_10`, `inter_14`, `inter_19`, `inter_22` unless you also rename the includes in `src/fonts.cpp`.
3. Install the converter dependency once: `pip install freetype-py`.
4. Regenerate the headers:
   ```bash
   python scripts/generate_vlw_fonts.py
   ```
5. Rebuild the firmware for your target:
   ```bash
   pio.exe run -e cyd
   ```
6. Flash the new `.pio/build/<env>/firmware.bin` over USB or push it OTA via the web UI's firmware update page.

Tips:

- Pick a font that renders well at small pixel sizes - thin or highly stylised faces will look smudged at 10-14 px. Sans-serif faces designed for UI work best.
- Each VLW table grows roughly linearly with pixel size and glyph count; the default Inter set is ~150 KiB total (three sizes; 320x480 boards add a fourth). Watch the flash usage line at the end of the build if you push to bigger sizes.
- The baked-in character set covers printable ASCII, Latin-1 Supplement, Latin Extended-A, and the euro sign (322 glyphs) - enough for most European languages. Add codepoints by editing `CHARSET` in the generator; keep it sorted ascending (the glyph lookup is a binary search). CJK and emoji are out of scope - they would need megabytes of glyphs.

## Troubleshooting

### WiFi won't connect / drops frequently

**SPI display cables near the ESP32 antenna can cause WiFi interference.** The ESP32-S3 Super Mini has a PCB antenna at one end of the board. If the SPI wires to the display run close to or over this antenna area, RF interference can prevent WiFi from connecting or cause frequent disconnections.

**Fix:** Route the display cables away from the antenna end of the ESP32-S3. Even 1-2 cm of separation can make a significant difference. If using a breadboard, ensure the wires do not loop back over the ESP32 module.

**Symptoms:**
- "Connecting to WiFi" screen appears briefly, then falls back to AP mode
- WiFi connects sometimes but drops after a few seconds
- Works fine when display is disconnected

**If WiFi issues persist**
Perform an antenna mod by soldering two individual goldpins to the antenna pads, as shown in the picture.

![wiring](img/antenamod.png)

> **Check your external antenna orientation first.** On boards with a detachable IPEX/U.FL antenna (e.g. ESP32-C3), the antenna is often mounted the wrong way around. The white mark on the antenna is the input - simply rotating it to the correct orientation can fix poor WiFi reception, often better than soldering wires to the pads. See [issue #106](https://github.com/Keralots/BambuHelper/issues/106) - it may help as well.

### Printer shows "Connecting" but never connects

- **LAN Direct:** Make sure the printer and ESP32 are on the same network. Check that LAN mode is enabled on the printer and the access code is correct.
- **Bambu Cloud:** Verify the access token has not expired (about 3 months validity). Sign in again from the device's **Account access** section, or re-extract the token from your browser and paste it again. Check the server region matches your Bambu account.
- If a printer is physically powered off, reconnect attempts will gradually slow down (backoff). It will reconnect automatically when the printer comes back online.

### Display shows wrong printer / does not switch

- Check rotation mode in the web interface (Multi-Printer section). Smart mode only switches automatically when a printer is actively printing.
- Press the physical button (if configured) to manually cycle between printers.

## License

MIT
