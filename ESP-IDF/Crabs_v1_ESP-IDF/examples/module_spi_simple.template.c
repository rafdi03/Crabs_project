#include "esp_err.h"
#include "driver/spi_master.h"
#include <stdbool.h>

static spi_device_handle_t s_device;
static bool s_ready;

esp_err_t RAF_ExampleSpiInit(void) {
    spi_bus_config_t bus_config = {
        .mosi_io_num = 23, /* TODO: move to board_config.h */
        .miso_io_num = 19,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = 5,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &device_config, &s_device);
    if (err != ESP_OK) return err;

    s_ready = true;
    return ESP_OK;
}

void RAF_ExampleSpiJob(void *arg) {
    (void)arg;
    if (!s_ready) return;

    /* TODO: build and transmit an spi_transaction_t. */
}
