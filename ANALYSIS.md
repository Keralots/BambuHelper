# ANALYSIS — BambuHelper v3.9.0 → BambuHelper-Mono (Fase 0)

Base: `main @ 23347fb (v3.9.0)`. Verificado en árbol local el 2026-10-14.
Objetivo: ESP32 + 1 impresora LAN + OLED SH1106 128x64 (Adafruit GFX).

## 1. Build / particiones / placas

- `platformio.ini` (~342 lín.): `default_envs=esp32s3`, `extra_configs=boards/*.ini`,
  `extra_scripts=pre:tools/gen_web_assets.py`. `lib_deps` común:
  `LovyanGFX@^1.2.19 + PubSubClient@^2.8 + ArduinoJson@^7.0`
  (+ XPT2046 en CYD, Arduino_GFX en jc3248w535). `ENABLE_OTA_AUTO=1`.
  Envs mantenidos: `esp32s3, cyd, esp32s3_zero, ws_lcd_200, ws_lcd_154,
  jc3248w535, esp32c3` + 14 en `boards/*.ini`.
- Particiones: `partitions_4mb.csv` = `nvs 20 KB, otadata 8 KB,
  app0/app1 1.75 MB, spiffs 448 KB` (spiffs sin uso; ajustes en NVS).
  Hay variantes 8/16 MB.
- Veredicto: **SIMPLIFY** → un solo `[env]`, conservar esquema NVS + 2xOTA,
  borrar `boards/`, flags `DISPLAY_*/BOARD_*`, `merge_bins.py`,
  `tools/release.py` (flasher), `tools/companion`, `docs/flasher.js`.

## 2. Módulos

| Módulo | Ficheros (tamaño) | Rol / deps | Veredicto |
|---|---|---|---|
| Orchestration | `src/main.cpp` (70 KB) | máquina `ScreenState` (IDLE/PRINT/SPLIT/CLOCK/CAMERA/HMS/OTA…), rotación, gestos, timers luz | **SIMPLIFY** → setup/loop WiFi→Web→MQTT→Display |
| Settings/NVS | `src/settings.h` (26 KB) + `settings.cpp` (70 KB), `include/bambu_state.h` (24 KB), `include/config.h` (34 KB). NS `bambu`, **sin int de versión** (claves por campo + migraciones ad-hoc). `MAX_PRINTERS=4`, `MAX_ACTIVE_PRINTERS=2/4` | **SIMPLIFY** → 1 slot impresora; borrar gauges/colores/temas/HMS/cloud/plugs/rotación/LED/buzzer. Al borrar campos, añadir magic/versión |
| WiFi+AP+Improv | `src/wifi_manager.cpp` (14 KB), `improv_setup.*`, `lib/ImprovWiFi` (vendored 0.0.2) | STA 3 intentos (15 s) → AP `BambuHelper-XXXX`/`bambu1234` + DNS cautivo + Improv-Serial 180 s; backoff 10 s x5 → 30 s x5 → 60 s; fallback AP 15 min | **KEEP** |
| Portal web | Sin LittleFS: literales PROGMEM + gzip build-time (`tools/gen_web_assets.py`: `web/app.css` 28 KB + `app.js` 113 KB → ~26 KB flash) + `WebServer :80` en `src/web_server.cpp` (134 KB) + `web_template.cpp` (49 KB) + `include/web_pages.h` (94 KB). Endpoints: `/save/*, /status, /debug, /settings/export|import, /ota/*, /app.css|js, /cloud/*, /lan/scan, /light/*, /power/*, …` | **SIMPLIFY** → Printer (IP/serial/access-code, 1 slot) + Display + WiFi/System + Diagnostics + OTA manual; borrar resto + su CSS/JS |
| MQTT/TLS+parser | `src/bambu_mqtt.cpp` (104 KB) + `.h`. `PubSubClient@^2.8 + WiFiClientSecure` (CA bundle) + `MqttRxClient` no-bloqueante. `BAMBU_BUFFER_SIZE 40960` (`config.h:78`), keepalive 60 s LAN, stale 60 s LAN, heap guard 40 KB | **KEEP núcleo, SIMPLIFY parser** → solo `gcode_state, mc_percent, mc_remaining_time, layer_num/total_layer_num, nozzle/bed_temper(+target), subtask_name, print_error`; `ArduinoJson` **6.21.x** con filtro + doc fijo |
| Display/UI actual | `display_ui.cpp` (277 KB) + `display_gauges.cpp` (77 KB) + `display_card.cpp` (90 KB) + `display_split.cpp` (31 KB) + anim/edge_glow/clock/pong + `lgfx_boards.h` (40 KB) + VLW `include/fonts/inter_*.h` (~4.5 MB fuente) + `scripts/generate_vlw_fonts.py`. Todo **LovyanGFX** | **DELETE/REPLACE** → capa mínima Adafruit GFX/SH1106 (`init/showBoot/showStatus/showError/showMessage`) |
| Plugs | `src/tasmota.cpp` (37 KB) | **DELETE** |
| Buzzer/LED/batería/botones | `buzzer*.cpp` (4 backends), `led.cpp` (48 KB), `battery.*`, `button*.cpp` (7 backends touch) + `io_expander_tca9554.*` | **DELETE** |
| HMS/errores | `hms_lookup.*` + `include/error_tables/` (`hms_table.h` 263 KB, `hms_known.h` 26 KB, `print_error_table.h` 47 KB ≈ 336 KB fuente) + `tools/gen_error_tables.py` | **DELETE** → badge `ERR` + código crudo |
| AMS/cámara | `bambu_state.h (AmsState)`, `camera_client.*`, `thumb_fetch.*`, `clock_*` | **DELETE** (luz cámara: solo publish manual si trivial) |
| Cloud | `bambu_cloud.*`, `cloud_login.cpp` (24 KB), `ssdp_discovery.*` | **DELETE** (solo LAN en v1) |
| OTA | manual `Update.h /ota/upload` + auto `HTTPUpdate /ota/auto|slots|rollback|status` | **KEEP manual, DELETE auto-con-un-clic-a-Releases** |

## 3. Dependencias entre módulos

`main` → {wifi, web, mqtt, settings, display}. `web` → settings + (mqtt status,
ota, export/import). `mqtt` → settings (IP/serial/access-code) + WiFi.
`display` ← mqtt/BambuState. Corte Mono: display nuevo solo depende de
`PrinterState` mínimo; web solo de settings+mqtt-status; mqtt solo de
settings+WiFi.

## 4. Tamaño/RAM base (medido 2026-10-14, `pio run -e esp32s3`)

```
RAM:   [==        ]  20.1% (used 65924 bytes from 327680 bytes)
Flash: [========= ]  94.9% (used 1740981 bytes from 1835008 bytes)
SUCCESS 00:05:08
```

Slot app 1.75 MB (`partitions_4mb.csv`) al 94.9% — casi lleno.
RAM estática libre ≈ 262 KB; el heap en marcha cae con TLS (~40-50 KB/sesión)
+ `BAMBU_BUFFER_SIZE` 40 KB por conexión (x2-4 en multi-impresora).
Re-medir tras Fase 2 y poner tabla antes/después.

### Base `mono-c3` (Fase 1, código completo + capa SH1106, 2026-10-14)

```
RAM:   [==        ]  17.7% (used 58036 bytes from 327680 bytes)
Flash: [========= ]  94.7% (used 1738636 bytes from 1835008 bytes)
```

## 5. Riesgo de acoplamiento / alternativa

La UI color (layouts por resolución, gauges, split, rotación, temas) está
entrelazada con `main.cpp`/`web_server.cpp`/`settings` (decenas de campos
`dsp_*`, `gaugeSlots`, endpoints `/save/gaugelayout|rotation|power…`).
Si la demolición arrastra demasiados `#ifdef DISPLAY_*`, aplicar la
alternativa del plan: proyecto nuevo y copiar solo MQTT+parser+portal+NVS+
WiFi/Improv. Decidir en Fase 2 tras el primer corte.
