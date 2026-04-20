// Bare-metal AXS15231B diagnostic — no LovyanGFX, no Panel_LCD, no abstraction.
//
// Goal: answer one question — does our QSPI framing produce correct pixel data?
//
// The init sequence has been debugged to match the vendor byte-for-byte. If the
// init bytes + SPI mode 3 + pclk 40MHz + COLMOD 0x55 + RGB565 are all correct
// AND the panel is armed (DISPON sent), then a single writePixels call with a
// solid-color buffer should paint the screen that color. If it doesn't, the
// bug lives in the pixel-write code path (how we frame 0x32/0x002C00 + QIO data
// + CS management). If it does paint, the LovyanGFX integration was the bug.
//
// Touch-to-reset is kept so iteration loop remains fast.

#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>

// --- Pins (JC3248W535, verified from vendor pincfg.h) ---
#define PIN_CS    GPIO_NUM_45
#define PIN_SCK   GPIO_NUM_47
#define PIN_D0    GPIO_NUM_21
#define PIN_D1    GPIO_NUM_48
#define PIN_D2    GPIO_NUM_40
#define PIN_D3    GPIO_NUM_39
#define PIN_BL    1
#define PIN_SDA   4
#define PIN_SCL   8

#define LCD_W 320
#define LCD_H 480
#define CHUNK_PIXELS 1024

static spi_device_handle_t g_spi = nullptr;
static uint8_t* g_dma = nullptr;   // reused for pixel chunks

// Send a command via the AXS "write-cmd" path (opcode 0x02, single-line).
// 4-byte header on the wire: 0x02, 0x00, cmd, 0x00, then optional data bytes.
static void cs_low()  { gpio_set_level(PIN_CS, 0); }
static void cs_high() { gpio_set_level(PIN_CS, 1); }

static void tx_cmd(uint8_t cmd, const uint8_t* data, size_t n) {
  cs_low();
  spi_transaction_ext_t t = {};
  // Vendor BSP sends init commands over QIO (lcd_cmd_bits=32 + quad_mode=true).
  // Adding MODE_QIO here makes our init header go 4-line like the vendor's —
  // previous attempts without it went single-line and the chip's behaviour on
  // receiving an init byte sequence timed at SIO vs QIO may be the bug.
  t.base.flags = SPI_TRANS_MODE_QIO
               | SPI_TRANS_MULTILINE_CMD
               | SPI_TRANS_MULTILINE_ADDR;
  t.base.cmd   = 0x02;
  t.base.addr  = ((uint32_t)cmd) << 8;
  if (n) {
    t.base.tx_buffer = data;
    t.base.length    = n * 8;
  }
  spi_device_polling_transmit(g_spi, (spi_transaction_t*)&t);
  cs_high();
}

static inline void tx_cmd_noarg(uint8_t cmd) { tx_cmd(cmd, nullptr, 0); }

// Stream a chunk of pixels with the 0x32 RAMWR/RAMWRC header, QIO data.
// Caller is responsible for CS low at batch start and high at batch end.
static void tx_pixels_first(const uint8_t* buf_bswapped, size_t n_pixels,
                            bool use_ramwr) {
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_MODE_QIO;
  t.base.cmd   = 0x32;
  t.base.addr  = use_ramwr ? 0x002C00 : 0x003C00;
  t.base.tx_buffer = buf_bswapped;
  t.base.length    = n_pixels * 16;
  spi_device_polling_transmit(g_spi, (spi_transaction_t*)&t);
}

static void tx_pixels_cont(const uint8_t* buf_bswapped, size_t n_pixels) {
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_MODE_QIO
               | SPI_TRANS_VARIABLE_CMD
               | SPI_TRANS_VARIABLE_ADDR
               | SPI_TRANS_VARIABLE_DUMMY;
  t.command_bits = 0; t.address_bits = 0; t.dummy_bits = 0;
  t.base.tx_buffer = buf_bswapped;
  t.base.length    = n_pixels * 16;
  spi_device_polling_transmit(g_spi, (spi_transaction_t*)&t);
}

// --- Bus init ---
static bool bus_init() {
  gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_CS, 1);

  spi_bus_config_t bcfg = {};
  bcfg.mosi_io_num     = PIN_D0;
  bcfg.miso_io_num     = PIN_D1;
  bcfg.sclk_io_num     = PIN_SCK;
  bcfg.quadwp_io_num   = PIN_D2;
  bcfg.quadhd_io_num   = PIN_D3;
  bcfg.max_transfer_sz = CHUNK_PIXELS * 2 + 16;
  bcfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
  esp_err_t r = spi_bus_initialize(SPI2_HOST, &bcfg, SPI_DMA_CH_AUTO);
  Serial.printf("[bus] spi_bus_initialize -> %d\n", r);
  if (r != ESP_OK) return false;

  spi_device_interface_config_t dcfg = {};
  dcfg.command_bits     = 8;
  dcfg.address_bits     = 24;
  dcfg.dummy_bits       = 0;
  dcfg.mode             = 3;                     // vendor
  dcfg.cs_ena_pretrans  = 0;
  dcfg.cs_ena_posttrans = 0;
  dcfg.clock_speed_hz   = 40 * 1000 * 1000;      // vendor pclk
  dcfg.spics_io_num     = -1;
  dcfg.queue_size       = 1;
  dcfg.flags            = SPI_DEVICE_HALFDUPLEX;
  r = spi_bus_add_device(SPI2_HOST, &dcfg, &g_spi);
  Serial.printf("[bus] spi_bus_add_device -> %d (handle=%p)\n", r, g_spi);
  if (r != ESP_OK) return false;

  g_dma = (uint8_t*)heap_caps_aligned_alloc(16, CHUNK_PIXELS * 2, MALLOC_CAP_DMA);
  Serial.printf("[bus] dma_buf=%p\n", g_dma);
  return g_dma != nullptr;
}

// --- Vendor init table (from JC3248W535 skill: vendor_specific_init_default) ---
struct InitOp { uint8_t cmd; const uint8_t* data; uint16_t n; uint16_t delay_ms; };

static void apply_vendor_init_table() {
  static const uint8_t d_BB1[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5};
  static const uint8_t d_A0[]  = {0x00,0x10,0x00,0x02,0x00,0x00,0x64,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00};
  static const uint8_t d_A2[]  = {0x30,0x04,0x0A,0x3C,0xEC,0x54,0xC4,0x30,0xAC,0x28,0x7F,0x7F,0x7F,0x20,0xF8,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xC0,0x20,0x7F,0xFF,0x00,0x54};
  static const uint8_t d_D0[]  = {0x30,0xAC,0x21,0x24,0x08,0x09,0x10,0x01,0xAA,0x14,0xC2,0x00,0x22,0x22,0xAA,0x03,0x10,0x12,0x40,0x14,0x1E,0x51,0x15,0x00,0x40,0x10,0x00,0x03,0x3D,0x12};
  static const uint8_t d_A3[]  = {0xA0,0x06,0xAA,0x08,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55};
  static const uint8_t d_C1[]  = {0x33,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40};
  static const uint8_t d_C3[]  = {0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01};
  static const uint8_t d_C4[]  = {0x00,0x24,0x33,0x90,0x50,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x04,0x03,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50};
  static const uint8_t d_C5[]  = {0x18,0x00,0x00,0x03,0xFE,0x78,0x33,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x78,0x33,0x20,0x10,0x10,0x80};
  static const uint8_t d_C6[]  = {0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x00,0x00,0x3F,0x6A,0x18,0xC8,0x22};
  static const uint8_t d_C7[]  = {0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9F,0x6F,0xFF,0x26,0x0C,0x0D,0x0E,0x0F};
  static const uint8_t d_C9[]  = {0x33,0x44,0x44,0x01};
  static const uint8_t d_CF[]  = {0x34,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0xF7,0x00,0x65,0x0C,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x04,0x04,0x12,0xA0,0x08};
  static const uint8_t d_D5[]  = {0x3E,0x3E,0x88,0x00,0x44,0x04,0x78,0x33,0x20,0x78,0x33,0x20,0x04,0x28,0xD3,0x47,0x03,0x03,0x03,0x03,0x86,0x00,0x00,0x00,0x30,0x52,0x3F,0x40,0x40,0x96};
  static const uint8_t d_D6[]  = {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x95,0x00,0x01,0x83,0x75,0x36,0x20,0x75,0x36,0x20,0x3F,0x03,0x03,0x03,0x10,0x10,0x00,0x04,0x51,0x20,0x01,0x00};
  static const uint8_t d_D7[]  = {0x0A,0x08,0x0E,0x0C,0x1E,0x18,0x19,0x1F,0x00,0x1F,0x1A,0x1F,0x3E,0x3E,0x04,0x00,0x1F,0x1F,0x1F};
  static const uint8_t d_D8[]  = {0x0B,0x09,0x0F,0x0D,0x1E,0x18,0x19,0x1F,0x01,0x1F,0x1A,0x1F};
  static const uint8_t d_D9[]  = {0x00,0x0D,0x0F,0x09,0x0B,0x1F,0x18,0x19,0x1F,0x01,0x1E,0x1A,0x1F};
  static const uint8_t d_DD[]  = {0x0C,0x0E,0x08,0x0A,0x1F,0x18,0x19,0x1F,0x00,0x1E,0x1A,0x1F};
  static const uint8_t d_DF[]  = {0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90};
  static const uint8_t d_E0[]  = {0x19,0x20,0x0A,0x13,0x0E,0x09,0x12,0x28,0xD4,0x24,0x0C,0x35,0x13,0x31,0x36,0x2F,0x03};
  static const uint8_t d_E1[]  = {0x38,0x20,0x09,0x12,0x0E,0x08,0x12,0x28,0xC5,0x24,0x0C,0x34,0x12,0x31,0x36,0x2F,0x27};
  static const uint8_t d_E2[]  = {0x19,0x20,0x0A,0x11,0x09,0x06,0x11,0x25,0xD4,0x22,0x0B,0x33,0x12,0x2D,0x32,0x2F,0x03};
  static const uint8_t d_E3[]  = {0x38,0x20,0x0A,0x11,0x09,0x06,0x11,0x25,0xC4,0x21,0x0A,0x32,0x11,0x2C,0x32,0x2F,0x27};
  static const uint8_t d_E4[]  = {0x19,0x20,0x0D,0x14,0x0D,0x08,0x12,0x2A,0xD4,0x26,0x0E,0x35,0x13,0x34,0x39,0x2F,0x03};
  static const uint8_t d_E5[]  = {0x38,0x20,0x0D,0x13,0x0D,0x07,0x12,0x29,0xC4,0x25,0x0D,0x35,0x12,0x33,0x39,0x2F,0x27};
  static const uint8_t d_BB2[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
  static const uint8_t d_2C[]  = {0x00,0x00,0x00,0x00};

  static const InitOp ops[] = {
    {0xBB, d_BB1, sizeof(d_BB1), 0}, {0xA0, d_A0, sizeof(d_A0), 0},
    {0xA2, d_A2, sizeof(d_A2), 0},   {0xD0, d_D0, sizeof(d_D0), 0},
    {0xA3, d_A3, sizeof(d_A3), 0},   {0xC1, d_C1, sizeof(d_C1), 0},
    {0xC3, d_C3, sizeof(d_C3), 0},   {0xC4, d_C4, sizeof(d_C4), 0},
    {0xC5, d_C5, sizeof(d_C5), 0},   {0xC6, d_C6, sizeof(d_C6), 0},
    {0xC7, d_C7, sizeof(d_C7), 0},   {0xC9, d_C9, sizeof(d_C9), 0},
    {0xCF, d_CF, sizeof(d_CF), 0},   {0xD5, d_D5, sizeof(d_D5), 0},
    {0xD6, d_D6, sizeof(d_D6), 0},   {0xD7, d_D7, sizeof(d_D7), 0},
    {0xD8, d_D8, sizeof(d_D8), 0},   {0xD9, d_D9, sizeof(d_D9), 0},
    {0xDD, d_DD, sizeof(d_DD), 0},   {0xDF, d_DF, sizeof(d_DF), 0},
    {0xE0, d_E0, sizeof(d_E0), 0},   {0xE1, d_E1, sizeof(d_E1), 0},
    {0xE2, d_E2, sizeof(d_E2), 0},   {0xE3, d_E3, sizeof(d_E3), 0},
    {0xE4, d_E4, sizeof(d_E4), 0},   {0xE5, d_E5, sizeof(d_E5), 0},
    {0xBB, d_BB2, sizeof(d_BB2), 0},
    {0x13, nullptr, 0, 0},               // NORON
    {0x11, nullptr, 0, 200},             // SLPOUT + 200ms
    {0x29, nullptr, 0, 200},             // DISPON + 200ms
    {0x2C, d_2C, sizeof(d_2C), 0},       // vendor trailing RAMWR param
  };

  spi_device_acquire_bus(g_spi, portMAX_DELAY);
  for (auto& e : ops) {
    tx_cmd(e.cmd, e.data, e.n);
    if (e.delay_ms) delay(e.delay_ms);
  }
  spi_device_release_bus(g_spi);
}

static void panel_init() {
  spi_device_acquire_bus(g_spi, portMAX_DELAY);
  tx_cmd_noarg(0x01);                  // SWRESET — no HW reset pin on this board
  delay(200);
  uint8_t madctl = 0x00;
  tx_cmd(0x36, &madctl, 1);
  uint8_t colmod = 0x55;               // RGB565 per AXS encoding
  tx_cmd(0x3A, &colmod, 1);
  spi_device_release_bus(g_spi);

  apply_vendor_init_table();           // includes its own SLPOUT+DISPON
}

// --- Draw: fill whole panel with one color ---
// CASET to full width, skip RASET (QSPI rule), then stream pixels until we've
// painted W*H. Pixel order in RAM is row-major from CASET top-left.
static void fill_screen(uint16_t color565) {
  uint16_t sw = __builtin_bswap16(color565);

  spi_device_acquire_bus(g_spi, portMAX_DELAY);

  uint8_t col[4] = { 0, 0, (uint8_t)((LCD_W - 1) >> 8), (uint8_t)((LCD_W - 1) & 0xFF) };
  tx_cmd(0x2A, col, 4);

  // Preload DMA buffer with the swapped color (CHUNK_PIXELS at a time)
  uint16_t* dst = (uint16_t*)g_dma;
  for (size_t i = 0; i < CHUNK_PIXELS; ++i) dst[i] = sw;

  size_t total = (size_t)LCD_W * LCD_H;
  bool first = true;
  cs_low();
  while (total) {
    size_t n = total > CHUNK_PIXELS ? CHUNK_PIXELS : total;
    if (first) { tx_pixels_first(g_dma, n, /*use_ramwr=*/true); first = false; }
    else       { tx_pixels_cont(g_dma, n); }
    total -= n;
  }
  cs_high();

  spi_device_release_bus(g_spi);
}

// --- Setup / loop ---
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BARE-METAL AXS15231B DIAGNOSTIC ===");

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  Serial.println("[bl] backlight on");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  if (!bus_init()) { Serial.println("bus_init FAILED"); return; }
  Serial.println("[bus] ready");

  panel_init();
  Serial.println("[panel] init done");

  Serial.println("fill_screen RED");
  fill_screen(0xF800);
  delay(1500);

  Serial.println("fill_screen GREEN");
  fill_screen(0x07E0);
  delay(1500);

  Serial.println("fill_screen BLUE");
  fill_screen(0x001F);
  delay(1500);

  Serial.println("fill_screen WHITE");
  fill_screen(0xFFFF);
  delay(1500);

  Serial.println("fill_screen BLACK");
  fill_screen(0x0000);

  Serial.println("Diagnostic halted");
}

static bool axs_touch_read() {
  static const uint8_t read_cmd[11] = {
    0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
  };
  Wire.beginTransmission(0x3B);
  Wire.write(read_cmd, sizeof(read_cmd));
  if (Wire.endTransmission() != 0) return false;
  uint8_t buf[8] = {0};
  size_t got = Wire.requestFrom((uint8_t)0x3B, (uint8_t)8);
  if (got < 6) return false;
  for (size_t i = 0; i < got && i < sizeof(buf); ++i) buf[i] = Wire.read();
  return (buf[0] == 0) && ((buf[1] & 0x0F) != 0);
}

void loop() {
  if (axs_touch_read()) {
    Serial.println("Touch detected — restarting");
    delay(50);
    ESP.restart();
  }
  delay(30);
}
