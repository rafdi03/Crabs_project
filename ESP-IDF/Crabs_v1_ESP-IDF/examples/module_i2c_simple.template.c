#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static bool s_ready;

esp_err_t RAF_ExampleI2cInit(void) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = 16, /* TODO: move to board_config.h */
        .scl_io_num = 17, /* TODO: move to board_config.h */
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) return err;

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x40, /* TODO: device address */
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) return err;

    s_ready = true;
    return ESP_OK;
}

void RAF_ExampleI2cJob(void *arg) {
    (void)arg;
    if (!s_ready) return;

    /* TODO: read the device and update your module cache. */
}
