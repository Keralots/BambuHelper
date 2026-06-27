# Experimental builds

Firmware here is **experimental and hardware-unverified**. These boards are not
in the web flasher and not part of the official release set. Use only if you own
the exact board and can report back.

## Panlee WT32-SC01 Plus 3.5" (issue #123)

- `BambuHelper-wt32_sc01_plus-v3.7.2-Full.bin` - full image for first flash via
  USB (ESP Web Flasher / esptool, flash offset `0x0`).
- `BambuHelper-wt32_sc01_plus-v3.7.2-ota.bin` - OTA update image (upload from the
  device web UI once already running).

Board: ESP32-S3 N16R2, ST7796 320x480 over an 8-bit 8080 parallel bus, FT6336
touch. The build compiles and links, but the panel has **not** been brought up on
real hardware here.

Please report on issue #123:
- Does the display show an image? If colors look inverted, say so (fix is a
  one-line `invert` flip).
- Is the rotation correct?
- Does touch work?
- Paste the serial boot log (115200 baud), especially the PSRAM line.
- Does printer status / MQTT work?
