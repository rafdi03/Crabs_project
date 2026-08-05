#include "dht.h"
#include <stdio.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_rom_sys.h>

#include "esp_err.h"

static const char *TAG = "dht";

// Very small, not-totally-robust DHT bit-banged reader. Works for basic use and
// returns 0 on success, negative on failure. Uses delays; not optimized.

static void drive_low(int pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void release_line(int pin) {
    gpio_set_direction(pin, GPIO_MODE_INPUT);
}

esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, int pin, float* humidity, float* temperature) {
    if (!humidity || !temperature) return ESP_ERR_INVALID_ARG;

    // send start signal
    drive_low(pin);
    vTaskDelay(pdMS_TO_TICKS(sensor_type == DHT_TYPE_DHT22 ? 1 : 20));
    release_line(pin);
    esp_rom_delay_us(40);

    // Wait for sensor response (low for ~80us then high for ~80us)
    int timeout = 1000;
    while (gpio_get_level(pin) == 1 && --timeout) esp_rom_delay_us(1);
    if (!timeout) { ESP_LOGW(TAG, "No response (1)"); return ESP_ERR_TIMEOUT; }
    timeout = 1000;
    while (gpio_get_level(pin) == 0 && --timeout) esp_rom_delay_us(1);
    if (!timeout) { ESP_LOGW(TAG, "No response (0)"); return ESP_ERR_TIMEOUT; }
    timeout = 1000;
    while (gpio_get_level(pin) == 1 && --timeout) esp_rom_delay_us(1);
    if (!timeout) { ESP_LOGW(TAG, "No response (1b)"); return ESP_ERR_TIMEOUT; }

    // Read 40 bits
    uint8_t data[5] = {0};
    for (int i = 0; i < 40; i++) {
        // wait for low
        timeout = 1000;
        while (gpio_get_level(pin) == 0 && --timeout) esp_rom_delay_us(1);
        if (!timeout) return ESP_ERR_TIMEOUT;
        // measure length of high
        int cnt = 0;
        while (gpio_get_level(pin) == 1 && cnt < 200) { esp_rom_delay_us(1); cnt++; }
        // >40us means 1 for most sensors
        int byte_index = i / 8;
        data[byte_index] <<= 1;
        if (cnt > 40) data[byte_index] |= 1;
    }

    // checksum
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        ESP_LOGW(TAG, "Checksum failed");
        return ESP_ERR_INVALID_CRC;
    }

    if (sensor_type == DHT_TYPE_DHT11) {
        *humidity = data[0];
        *temperature = data[2];
    } else {
        int raw_h = (data[0] << 8) | data[1];
        int raw_t = (data[2] << 8) | data[3];
        *humidity = raw_h / 10.0f;
        if (raw_t & 0x8000) raw_t = -(raw_t & 0x7fff);
        *temperature = raw_t / 10.0f;
    }

    return ESP_OK;
}
