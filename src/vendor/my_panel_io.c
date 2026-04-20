#ifdef BOARD_IS_JC3248W535_VENDOR
// Custom esp_lcd_panel_io_t implementation for QSPI on arduino-esp32 3.0.x.
//
// Replicates stock `esp_lcd_panel_io_spi` behavior for lcd_cmd_bits=32 +
// flags.quad_mode=true, which arduino-esp32 3.0.x doesn't ship. Behavior
// verified against ESP-IDF v5.2 src/esp_lcd_panel_io_spi.c:
//
// * Cmd is a SEPARATE 32-bit QIO data transaction. Not packed with params.
//   Not split across cmd/addr phases.
// * Params (for tx_param) and color data (for tx_color) follow as additional
//   data-phase-only QIO transactions.
// * CS is held LOW across all transactions of one logical operation via
//   SPI_TRANS_CS_KEEP_ACTIVE — this requires the SPI peripheral to drive CS
//   (spics_io_num must be set to the CS pin, NOT -1).
// * No explicit RAMWRCONT header on continuation chunks. The chip stays in
//   write mode as long as CS stays asserted.
//
// Scope: display path only.

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_heap_caps.h"

#include "my_panel_io.h"

static const char *TAG = "my_panel_io";

#define SCRATCH_BYTES     4096
#define HEADER_BYTES      4

typedef struct {
    esp_lcd_panel_io_t base;
    spi_device_handle_t spi;
    uint8_t*            scratch;      // DMA-capable staging for PSRAM-sourced data
    uint8_t             cmdbuf[8];    // 32-bit packed cmd buffer (DMA-safe internal)
} my_io_t;

static inline void pack_header(uint8_t* buf, int lcd_cmd) {
    buf[0] = (lcd_cmd >> 24) & 0xFF;
    buf[1] = (lcd_cmd >> 16) & 0xFF;
    buf[2] = (lcd_cmd >>  8) & 0xFF;
    buf[3] = (lcd_cmd      ) & 0xFF;
}

// Send the 32-bit packed lcd_cmd as its own QIO data-phase transaction.
// CS is kept active so subsequent transactions in the same op continue the
// transfer under one CS assertion.
static esp_err_t send_cmd(my_io_t* self, int lcd_cmd, bool more_to_follow) {
    pack_header(self->cmdbuf, lcd_cmd);
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_MODE_QIO
                 | SPI_TRANS_VARIABLE_CMD
                 | SPI_TRANS_VARIABLE_ADDR
                 | SPI_TRANS_VARIABLE_DUMMY
                 | (more_to_follow ? SPI_TRANS_CS_KEEP_ACTIVE : 0);
    t.command_bits = 0;
    t.address_bits = 0;
    t.dummy_bits   = 0;
    t.base.tx_buffer = self->cmdbuf;
    t.base.length    = 32;            // 4 bytes = 32 bits
    return spi_device_polling_transmit(self->spi, (spi_transaction_t*)&t);
}

// Send a data-phase-only QIO transaction. Caller controls CS via the
// cs_keep_active flag: true = keep asserted after this one, false = release.
// tx_buffer must be in DMA-capable memory.
static esp_err_t send_data(my_io_t* self, const uint8_t* buf, size_t nbytes,
                           bool cs_keep_active) {
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_MODE_QIO
                 | SPI_TRANS_VARIABLE_CMD
                 | SPI_TRANS_VARIABLE_ADDR
                 | SPI_TRANS_VARIABLE_DUMMY
                 | (cs_keep_active ? SPI_TRANS_CS_KEEP_ACTIVE : 0);
    t.command_bits = 0;
    t.address_bits = 0;
    t.dummy_bits   = 0;
    t.base.tx_buffer = buf;
    t.base.length    = nbytes * 8;
    return spi_device_polling_transmit(self->spi, (spi_transaction_t*)&t);
}

static esp_err_t my_tx_param(esp_lcd_panel_io_t* io, int lcd_cmd,
                             const void* param, size_t param_size)
{
    my_io_t* self = (my_io_t*)io;
    bool has_param = (param && param_size > 0);
    esp_err_t r = send_cmd(self, lcd_cmd, /*more_to_follow=*/has_param);
    if (r != ESP_OK || !has_param) return r;

    // Copy param into DMA scratch (param is usually a stack-allocated
    // compound literal from the vendor driver, not DMA-capable).
    if (param_size > SCRATCH_BYTES) return ESP_ERR_INVALID_SIZE;
    memcpy(self->scratch, param, param_size);
    return send_data(self, self->scratch, param_size, /*cs_keep_active=*/false);
}

static esp_err_t my_tx_color(esp_lcd_panel_io_t* io, int lcd_cmd,
                             const void* color, size_t color_size)
{
    my_io_t* self = (my_io_t*)io;
    esp_err_t r = ESP_OK;
    // Per the jc3248w535 skill: AXS15231B wants CS toggled per chunk with a
    // RAMWR/RAMWRCONT header on each chunk. lcd_cmd from the vendor driver
    // is already (0x32<<24)|(0x2C<<8) (or 0x3C for continuation). We emit
    // that header on the first chunk and (0x32<<24)|(0x3C<<8) on all
    // continuations.
    const int cont_lcd_cmd = (0x32 << 24) | (0x3C << 8);
    const uint8_t* src = (const uint8_t*)color;
    size_t remaining = color_size;
    bool first = true;
    while (remaining) {
        size_t data_capacity = SCRATCH_BYTES - HEADER_BYTES;
        size_t chunk = remaining > data_capacity ? data_capacity : remaining;

        pack_header(self->scratch, first ? lcd_cmd : cont_lcd_cmd);
        memcpy(self->scratch + HEADER_BYTES, src, chunk);

        // CS cycles per chunk (spics_io_num is set, peripheral does the toggle).
        // No SPI_TRANS_CS_KEEP_ACTIVE.
        r = send_data(self, self->scratch, HEADER_BYTES + chunk,
                      /*cs_keep_active=*/false);
        if (r != ESP_OK) break;
        first      = false;
        src       += chunk;
        remaining -= chunk;
    }
    return r;
}

static esp_err_t my_del(esp_lcd_panel_io_t* io) {
    my_io_t* self = (my_io_t*)io;
    if (self->scratch) heap_caps_free(self->scratch);
    free(self);
    return ESP_OK;
}

esp_err_t my_panel_io_new(spi_device_handle_t spi, int cs_pin,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    (void)cs_pin;   // CS is managed by the SPI peripheral via spics_io_num
    if (!spi || !ret_io) return ESP_ERR_INVALID_ARG;
    my_io_t* self = (my_io_t*)calloc(1, sizeof(my_io_t));
    if (!self) return ESP_ERR_NO_MEM;
    self->spi = spi;
    self->scratch = (uint8_t*)heap_caps_aligned_alloc(16, SCRATCH_BYTES,
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!self->scratch) { free(self); return ESP_ERR_NO_MEM; }
    self->base.tx_param = my_tx_param;
    self->base.tx_color = my_tx_color;
    self->base.del      = my_del;
    *ret_io = &self->base;
    return ESP_OK;
}
#endif  // BOARD_IS_JC3248W535_VENDOR
