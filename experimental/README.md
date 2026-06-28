# Experimental firmware - QD ES3N28P 2.8" (issue #125)

These are **community / hardware-unconfirmed** test builds for the QD electronic
2.8" IPS ESP32-S3 board (ILI9341V 240x320 + FT6336 touch, module codes
ES3C28P / ES3N28P). They are not in the web flasher or the official release set
yet - this folder exists so the requester can flash-test before anything merges.

Firmware version: **v3.7.2**

## Files

| File | For | How |
|---|---|---|
| `BambuHelper-es3n28p-v3.7.2-Full.bin` | first-time flash (blank board) | flash at offset **0x0** |
| `BambuHelper-es3n28p-v3.7.2-ota.bin` | updating a board that already runs BambuHelper | upload via the device web UI (Settings -> Firmware update) |

## Flashing the Full image (first time)

Use any of these. The Full image already contains bootloader + partitions +
app, so it goes at **0x0**.

**Web flasher (easiest, Chrome/Edge):**
1. Open https://espressif.github.io/esptool-js/
2. Connect, set the file to `BambuHelper-es3n28p-v3.7.2-Full.bin` at offset `0x0`
3. Program. Hold BOOT while plugging in if the port does not appear.

**esptool (command line):**
```
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 BambuHelper-es3n28p-v3.7.2-Full.bin
```

After flashing, the device boots into a `BambuHelper-XXXX` WiFi AP for setup
(or use the serial/Improv onboarding).

## What to check and report back (issue #125)

- [ ] **Display** comes up at boot - image is the right way up, not mirrored,
      and colors look correct (not photo-negative). If colors are inverted or
      mirrored, say so - it is a one-line fix.
- [ ] **Touch** works - tapping the screen wakes / navigates. Serial log at
      115200 should print a FocalTech "became responsive" line on first touch.
- [ ] **Battery** indicator (if your unit has a cell) reads a believable level.
      The voltage divider is an unverified guess and may need calibration.
- [ ] General: WiFi connects, printer status shows, no boot loops.

Please attach the serial monitor output (115200 baud) if anything misbehaves.
