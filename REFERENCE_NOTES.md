# REFERENCE_NOTES — ESP-Spotify-OLED (repo del usuario)

Fuente: https://github.com/alejandrosnz/ESP-Spotify-OLED, leído entero
(`ESP-Spotify-OLED.ino`, `config.h`, `secrets.h`, `weather_icons.h`, `README.md`).
Verificado por webfetch el 2026-10-14. No hay `platformio.ini` ni `.cpp`.

## 1. Librerías y versiones exactas (README.md, literal)

- Adafruit GFX Library 1.10.9
- Adafruit SH110X 2.1.10
- ArduinoJson 6.21.x
- ezTime 0.8.3
- SpotifyArduino (ZIP manual) — no aplica a BambuHelper-Mono

`platformio.ini` futuro debe fijar: `adafruit/Adafruit GFX Library@1.10.9`,
`adafruit/Adafruit SH110X@2.1.10`, `bblanchon/ArduinoJson@6.21.5`,
`ropg/ezTime@0.8.3` (solo si se muestra hora de fin).

## 2. Inicialización del display (`.ino`, literal)

```cpp
#define OLED_SDA D1
#define OLED_SCL D2
#define OLED_I2C_ADDR 0x3c
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
// setup():
Wire.begin(OLED_SDA, OLED_SCL);
delay(250);
display.begin(OLED_I2C_ADDR, true);
display.setTextColor(SH110X_WHITE);
display.clearDisplay();
```

Notas: clase `Adafruit_SH1106G` (de `Adafruit_SH110X.h`), reset `-1` = sin pin
de reset, `begin(addr, true)` = reset interno. Sin `setRotation()` ni
`setContrast()` — defaults. `config.h` repite los mismos 6 `#define`.

## 3. Pines y placas probadas

- README: `ESP8266 o ESP32 (ej. D1 Mini)`, cableado `SDA->D1, SCL->D2, VCC->3.3V, GND->GND`.
- Código: `#define OLED_SDA D1 / OLED_SCL D2`, `Wire.begin(SDA, SCL)`.
- Compila para ambas: `#if defined(ESP8266) / #elif defined(ESP32)` en includes
  WiFi y en `client.setFingerprint(...)` (8266) vs `client.setCACert(...)` (ESP32).
- En ESP32 `D1/D2` no existen como símbolos: traducir a GPIO reales en
  `platformio.ini`/`pins.h` de Fase 1. No inventar pines sin confirmación.

## 4. Cómo dibuja (estilo a reutilizar)

- Solo `SH110X_WHITE`, `clearDisplay()` + `display()` por refresco.
- Título: `FreeMonoBold9pt7b`, corte manual a 11 chars/línea, 2 líneas
  (`MAX_CHAR_TITLE_PER_LINE 11`).
- Secundario: fuente default `setFont()` + `setTextSize(1)`, 21 chars/línea
  (`MAX_CHAR_ARTIST_PER_LINE 21`).
- Barra: `drawRect(0,50,128,6)` + `fillRect(0,50,map(prog,0,dur,0,126),6)`.
- Tiempos: `sprintf("%02lu:%02lu/%02lu:%02lu")` abajo (`setCursor(60,57)`),
  glyph play/stop en `(0,57)` (`write(0x10)` / `write(0xFD)`).
- Iconos: bitmaps 30x30 en PROGMEM (`weather_icons.h`, 12 iconos + `degree_bmp`
  8x10), `drawBitmap(x,y,ptr,30,30,WHITE)`, selector `getWeatherIcon(code)`.
- Layout absoluto con `setCursor(x,y)`; tamaños 3 (reloj) / 2 (temperatura) / 1.
- `replaceSpecialCharacters()`: transliterar `áéíóúñü...` a ASCII (las GFX
  no tienen acentos) — reutilizar para nombres de fichero Bambu.

## 5. Estilo general del código

- Un sketch ~300 líneas + 3 headers con guardas, todo configurable por
  `#define` en `config.h` (delays, zona `Europe/Madrid`, límites de chars,
  `HTTP_INSECURE 1`, `SERIAL_BAUDRATE 115200`).
- `loop()` no bloqueante por `millis()` (Spotify 5 s, weather 5 min, reloj 1 s)
  + `events()` de ezTime; reintento de token con `TOKEN_RETRY_DELAY 30 s`.
- `StaticJsonDocument<80>` filtro + `<256>` doc para weather (patrón a copiar
  para el parser Bambu con filtros).
- `client.setBufferSizes(512,512)` + `setTimeout(15)` para ahorrar heap.
