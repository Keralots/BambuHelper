# Simplified Chinese CYD validation

Tested on 2026-10-24 against upstream commit
`a2a64ecb34b2b0ed3c9a1447cc5ff35fa62b1ca5`.

## Automated checks

- `python -m unittest discover -s tests -v`: six tests passed. Covers HTML form
  structure, template placeholders, JavaScript protocol tokens, JSON machine
  fields, canonical printer states, and sorted font tables with complete
  translated screen glyph coverage.
- Generated localized JavaScript passed `node --check`.
- A local browser smoke test in headless Edge opened all 11 settings sections
  and checked Chinese missing-SSID validation on the setup page. No page errors.
- All localized C++ printf format specifiers match their English originals.
- `pio run -e cyd_zh_cn -e cyd -j 4`: both environments built successfully.
  The final protocol-state correction was rebuilt and USB-flashed successfully
  with `pio run -e cyd_zh_cn -t upload --upload-port COM4`.

## Build size

The final Chinese build uses 67,632 bytes of static RAM and 1,953,045 bytes of
ELF flash sections. Its application `firmware.bin` is **1,959,616 bytes**, below
the existing 1,966,080-byte OTA slot, with only 6,464 bytes spare. Check the
actual binary size when extending the translation or adding glyphs.

The original CYD partition layout is retained. Chinese builds use the existing
no-exceptions configuration to accommodate the font subsets.

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
- No printer power, lighting, motion, pause or other control command was sent.
  Device captures, flash backups and credentials are not included in the PR.

This is a smoke test, not a long-running soak test. Physical touch gestures,
all error/finish/clock screens, OTA updates, fresh AP provisioning and other
board models were not hardware-tested. The AP form was tested in the browser.
The font subset covers translated UI text, not arbitrary Chinese filenames or
printer names. Upstream online updates currently install English firmware;
the Chinese portal warns users to upload a Chinese build manually.
