#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

// Create a custom esp_lcd_panel_io_t that sends commands + params over QSPI
// using a single data-phase transaction (no cmd/addr phase split).
//
// The SPI device must already be added to the bus. The caller retains
// ownership of the spi handle; my_del does not free it.
//
// cs_pin is the GPIO pin for CS. This implementation drives CS manually
// around each transaction so the SPI device must have been configured with
// spics_io_num = -1.
esp_err_t my_panel_io_new(spi_device_handle_t spi, int cs_pin,
                          esp_lcd_panel_io_handle_t *ret_io);

#ifdef __cplusplus
}
#endif
