# BambuHelper v2.5 BETA Release Notes

## Power Monitoring via Tasmota (NEW)

- **Live watt display** - shows current power consumption from a Tasmota-flashed smart plug in the bottom status bar, alongside the layer counter
- **Two display modes**: alternate between layer count and watts every 4 seconds, or always show watts
- **Configurable poll interval** - HTTP API polling every 10, 15, 20, or 30 seconds (Tasmota HTTP, no MQTT broker required)
- **Print energy summary** - on the "Print Complete" screen, total kWh consumed during the print job is shown between the filename and the status bar (e.g. `⚡ 0.234 kWh`)
- Graceful fallback: if the plug is offline or not yet polled, layer count is shown instead - no crashes
- Configured in a new "Power Monitoring" section in the web UI; disabled by default, zero impact when not configured

## Screensaver brightness (NEW)
*contributed by [@theNailz](https://github.com/theNailz) in [#16](https://github.com/Keralots/BambuHelper/pull/16)*

- **Separate brightness for clock/screensaver screen** - display can automatically dim when idle without affecting brightness during active printing
- Slider appears in the Display section under "Show clock after print" (hidden when clock is disabled)
- Respects night mode: during night hours, whichever brightness is lower wins
- Included in settings export/import, backwards-compatible with existing configs

## ESP32-C3 Super Mini support (NEW)
*contributed by [@noliran](https://github.com/noliran) in [#13](https://github.com/Keralots/BambuHelper/pull/13)*

- **New build target** for ESP32-C3 Super Mini with ST7789 1.69" 240x280 display
- Separate PlatformIO environment (`pio run -e esp32c3`), does not affect existing S3 or CYD builds
- Includes pre-build patch script for a TFT_eSPI bug on C3: `SPI2_HOST` (host enum = 1) was used where the bus number (2) is required, causing a Store access fault on the first SPI write. Patched automatically before compilation until upstream fix is merged

## Web UI security fixes
*contributed by [@theNailz](https://github.com/theNailz) in [#18](https://github.com/Keralots/BambuHelper/pull/18)*

- **XSS hardening** - dynamic content from API responses (printer names, MQTT error text, state values) is now HTML-escaped before injection via `innerHTML`
- Status messages (import, OTA, cloud logout) switched from `innerHTML` to `textContent` where no markup is needed - cleaner and safer
- Exploitation would require a malicious printer name or compromised API response, but this is now properly handled regardless

## Web UI error handling improvements
*contributed by [@theNailz](https://github.com/theNailz) in [#19](https://github.com/Keralots/BambuHelper/pull/19)*

- **Meaningful error messages** - generic "Error" toast replaced with descriptive messages ("Save failed", "Network error", "Buzzer test failed", etc.) across all settings actions
- Empty `catch` blocks now log to `console.warn` with function name, making browser-side debugging possible when troubleshooting network or API failures

## Web UI polling optimization
*contributed by [@theNailz](https://github.com/theNailz) in [#20](https://github.com/Keralots/BambuHelper/pull/20)*

- **Timers stop when sections are collapsed** - live stats (3s) and diagnostics (5s) polling now only runs when the relevant section is actually open
- Previously both `setInterval` timers ran continuously from page load regardless of what was visible, wasting bandwidth and ESP32 CPU cycles on long-lived browser sessions

---

> A huge thank you to [@theNailz](https://github.com/theNailz) for yet another round of quality contributions to this release. From security hardening to UX improvements to real bug fixes - the kind of thoughtful, well-tested work that makes this project better for everyone. It means a lot.

---

## Display settings redesign (NEW)
*contributed by [@theNailz](https://github.com/theNailz) in [#24](https://github.com/Keralots/BambuHelper/pull/24)*

- **Logical sub-sections** in Display settings: Brightness, After Print Completes, Screen, Clock Settings, Gauge Colors
- **Single "When print finishes" dropdown** replaces the previous combination of timeout input + "keep display on" checkbox + "show clock" checkbox - impossible to create broken combinations, all existing NVS values and export/import work unchanged
- Custom duration option revealed inline when selected; Breakout clock option automatically disabled when "Keep finish screen on" is chosen

## Fix: clock screen not appearing when finish timeout is 0
*contributed by [@theNailz](https://github.com/theNailz) in [#23](https://github.com/Keralots/BambuHelper/pull/23)*

- When "Display off after print complete" was set to 0 and "Show clock after print" was enabled, the clock screen was never reached - device stayed on the finish screen permanently
- Fixed: `finishDisplayMins == 0` now correctly transitions to clock immediately when clock/screensaver is enabled

## Timezone and date format improvements (NEW)
*contributed by [@theNailz](https://github.com/theNailz) in [#22](https://github.com/Keralots/BambuHelper/pull/22)*

- **Timezone dropdown** restyled to Windows-style format, sorted by UTC offset (e.g. `(UTC+01:00) Amsterdam, Berlin, Rome, Stockholm, Vienna`)
- Timezone selection now migrates safely across database reorders by matching the stored POSIX TZ string, not the index
- **Date format selection** - 6 options: DD.MM.YYYY, DD-MM-YYYY, MM/DD/YYYY, YYYY-MM-DD, DD MMM YYYY, MMM DD YYYY
- Date format applied on the TFT clock screen; persisted in NVS and included in settings export/import

## Brightness slider UX fix
*contributed by [@theNailz](https://github.com/theNailz) in [#25](https://github.com/Keralots/BambuHelper/pull/25)*

- Night mode and screensaver brightness sliders no longer change the live display brightness while dragging - they only set the stored value for that mode
- All three brightness sliders now snap in increments of 5 for consistency

## Security hardening
*contributed by [@theNailz](https://github.com/theNailz) in [#26](https://github.com/Keralots/BambuHelper/pull/26)*

- **Credentials cleared from RAM** before wiping NVS on factory reset (WiFi password, cloud email, access codes, cloud user IDs were previously left in RAM until reboot)
- **Cloud token zeroed from stack** immediately after MQTT connect
- **TLS certificate verification** enabled for cloud API calls using the same CA bundle already used for MQTT; falls back to unverified only if the CA handshake fails (protects deployed devices against CA chain rotation)

## Fix: display going blank when applying settings in clock/screensaver mode
*contributed by [@theNailz](https://github.com/theNailz) in [#29](https://github.com/Keralots/BambuHelper/pull/29)*

- Applying any display setting while the clock or screensaver was active caused the screen to go black for up to 60 seconds
- Root causes: `configTzTime()` was called unconditionally on every settings save (resetting SNTP sync), `getLocalTime()` had no fallback when SNTP failed, and the 250ms display throttle delayed the redraw
- Fixed: timezone is only reconfigured when it actually changed; hardware RTC fallback added to clock rendering; display throttle reset on settings apply for immediate redraw

## Memory safety fixes
*contributed by [@theNailz](https://github.com/theNailz) in [#32](https://github.com/Keralots/BambuHelper/pull/32)*

- **HTTP client leak** fixed in `bambu_cloud`: `http.end()` now called on all return paths
- **Unsigned underflow** fixed in AMS search loop in `bambu_mqtt`: subtraction guarded against wrapping to a huge value causing out-of-bounds read
- **Array bounds** enforced in `clock_pong`: added `BRICK_COLOR_COUNT` constant and `static_assert` to make the `brickColors` size contract compiler-enforced
- **Null-pointer crash** fixed in gauge cache: when cache was full, callers received `nullptr` which was never checked - replaced with FIFO eviction so a valid slot is always returned
- **Null check** added in `htmlToRgb565` before dereferencing the argument
- **DNS server leak** fixed in `wifi_manager`: AP DNS server is now properly stopped and freed when transitioning to STA mode

## Concurrency and state-guard fixes
*contributed by [@theNailz](https://github.com/theNailz) in [#33](https://github.com/Keralots/BambuHelper/pull/33)*

- **Timer sentinel** replaced: `millis() == 0` used as "not started" flag would misfire after ~49 days of uptime; replaced with explicit boolean flags
- **Array bounds check** added to `displayedPrinter()` to prevent out-of-bounds access on invalid printer index
- **Stale fetch guard** added for rapid printer tab switching in the config portal

## WiFi RSSI in /debug endpoint
*contributed by [@theNailz](https://github.com/theNailz) in [#31](https://github.com/Keralots/BambuHelper/pull/31)*

- WiFi signal strength (dBm) now included in the `GET /debug` JSON response - useful for diagnosing connectivity issues without a serial connection

## Fix: H2 series printers not working in LAN mode

- H2S, H2C, and H2D printers connected via LAN mode would show "connected" but never receive any data - the display stayed on the loading/idle screen indefinitely
- **Root cause**: H2 printers with multiple AMS units send ~33KB MQTT payloads (full state on every report, not incremental diffs like P1S/X1C). The 16KB PubSubClient receive buffer silently dropped every message
- **Fix**: MQTT buffer increased from 16KB to 40KB to accommodate H2 payloads; pushall request updated with `version` and `push_target` fields matching the current Bambu MQTT spec
- H2 LAN mode requires both LAN-only mode and Developer Mode enabled on the printer

## Fix: false "Ready" screen during cloud printing

- During long prints on cloud-connected printers (H2C/H2D), the screen could occasionally switch to the "Ready" idle view mid-print, showing correct temperatures but no print progress
- **Root cause**: the Bambu cloud MQTT broker occasionally stops pushing status messages for >5 minutes during steady printing (no state changes). The stale-data timer would then clear the printing state, sending the display to idle
- **Fix 1**: when the stale timer fires during an active print and MQTT is still connected, a recovery pushall is sent to re-request current printer state. The printing state is only cleared if no response arrives within 30 seconds
- **Fix 2**: any received MQTT message (AMS updates, extruder data, etc.) now resets the stale timer - previously only messages containing a `print` object did so
