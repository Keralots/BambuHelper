# Simplified Chinese CYD validation

Tested on 2026-10-24 against upstream commit
`a2a64ecb34b2b0ed3c9a1447cc5ff35fa62b1ca5`.

## Automated checks

- `python -m unittest discover -s tests -v`: eight tests passed. Covers HTML form
  structure, template placeholders, JavaScript protocol tokens, JSON machine
  fields, canonical printer states, and sorted font tables with complete
  translated screen glyph coverage, lossless compressed page reconstruction,
  feature gates and placeholder-safe compression boundaries.
- Both generated English and Chinese JavaScript passed `node --check`.
- A local browser smoke test in headless Edge opened all 11 settings sections
  and checked Chinese missing-SSID validation on the setup page. No page errors.
- All localized C++ printf format specifiers match their English originals.
- `pio run -e cyd_zh_cn -e cyd -j 4`: both environments built successfully.
  The final bilingual environment was subsequently rebuilt with LovyanGFX
  excluded from LTO, without the graphics type-compatibility warnings.

## Build size

The final bilingual build uses 67,372 bytes of static RAM and 1,942,093 bytes of
ELF flash sections. Its application `firmware.bin` is **1,948,672 bytes**, below
the existing 1,966,080-byte OTA slot, with 17,408 bytes spare. This is 10,944
bytes smaller than the initial Chinese-only build despite retaining both
languages. Check the
actual binary size when extending the translation or adding glyphs.

The original CYD partition layout is retained. The opt-in bilingual build uses
independent 4 KB DEFLATE template blocks, ESP32's ROM inflater, LTO and the
existing no-exceptions configuration. The inflater state and output block are
allocated only while serving a page, rather than on Arduino's small stack.
LovyanGFX keeps its original non-LTO compilation because its extension base
classes intentionally depend on include order. No features were removed to
make room. The existing English environment keeps its original build options.

## Hardware smoke test

- Classic 4MB ESP32 CYD, ESP32-D0WD-V3 rev 3.1, ILI9341_2 panel and XPT2046
  touch controller; no PSRAM.
- Backed up the original full flash privately before USB flashing.
- Restarted the final firmware and captured its boot log; no panic, brownout,
  abort, or heap-corruption signature appeared in the capture.
- Existing WiFi and printer configuration remained usable. The device
  reconnected to its configured printer and reported `RUNNING` with live
  progress after restart.
- The real device served `lang="zh-CN"` HTML and localized compressed
  JavaScript. Its `/card.bmp` capture was visually checked: Chinese state,
  remaining time, ETA, nozzle, bed, chamber and humidity labels rendered.
- USB-flashed the final bilingual build, then used its real browser language
  selector to switch Chinese -> English -> Chinese. Each change restarted the
  device and retained the selected language and existing WiFi/printer settings.
  Both Card screenshots were visually checked, and live printing data resumed
  after each restart. The final Chinese status reported `RUNNING` at 84%.
- The real bilingual portal opened all 11 settings sections without JavaScript
  page errors. An unsupported language returned HTTP 400. Ten sequential page
  requests all succeeded with the same reported free heap (81 KB), while the
  printer remained connected.
- No printer power, lighting, motion, pause or other control command was sent.
  Device captures, flash backups and credentials are not included in the PR.

This is a smoke test, not a long-running soak test. Physical touch gestures,
all error/finish/clock screens, OTA updates, fresh AP provisioning and other
board models were not hardware-tested. The AP form was tested in the browser.
The font subset covers translated UI text, not arbitrary Chinese filenames or
printer names. Upstream online updates currently install English firmware;
the Chinese portal warns users to upload a Chinese build manually.
