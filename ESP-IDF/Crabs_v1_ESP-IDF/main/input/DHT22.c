/*
 * DHT22.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#include "DHT22.h"
#include "modules_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kalman_filter.h"

static kalman1d_t s_temp_kalman;
static kalman1d_t s_hum_kalman;

static const char *TAG = "DHT22";

static bool s_ready = false;
static dht22_data_t s_data = {0};

// ---- Low level ----
static void dht_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

static int dht_wait_level(int level, uint32_t timeout_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(DHT22_GPIO) == level) {
        if ((esp_timer_get_time() - start) > timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - start);
}

static esp_err_t dht_read_raw(uint8_t *hum_h, uint8_t *hum_l,
                              uint8_t *temp_h, uint8_t *temp_l) {
    uint8_t data[5] = {0};

    // Start signal
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));       // butuh preemption ON di sini
    gpio_set_level(DHT22_GPIO, 1);
    dht_delay_us(30);
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_INPUT);

    // ===== KRITIKAL: disable preemption untuk baca 40 bit =====
    // Total window ~2 ms (40 bit × ~50 us). Aman untuk RTOS.
    portDISABLE_INTERRUPTS();

    // Response pulse
    if (dht_wait_level(1, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
    if (dht_wait_level(0, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
    if (dht_wait_level(1, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }

    // Read 40 bits
    for (int i = 0; i < 40; i++) {
        if (dht_wait_level(0, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
        int high_us = dht_wait_level(1, 100);
        if (high_us < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
        data[i / 8] <<= 1;
        if (high_us > 40) data[i / 8] |= 1;
    }

    portENABLE_INTERRUPTS();
    // ===== AKHIR KRITIKAL =====

    // Checksum
    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) return ESP_ERR_INVALID_CRC;

    *hum_h  = data[0];
    *hum_l  = data[1];
    *temp_h = data[2];
    *temp_l = data[3];
    return ESP_OK;
}

esp_err_t dht22_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DHT22_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
	
	kalman1d_init(&s_temp_kalman, 0.01f, 0.5f, 0.0f);
	kalman1d_init(&s_hum_kalman,  0.01f, 2.0f, 0.0f);

    s_ready = true;
    ESP_LOGI(TAG, "DHT22 siap di GPIO %d", DHT22_GPIO);
    return ESP_OK;
}

void dht22_update(void) {
    if (!s_ready) return;

    uint8_t hh, hl, th, tl;
    if (dht_read_raw(&hh, &hl, &th, &tl) == ESP_OK) {
        uint16_t raw_hum  = ((uint16_t)hh << 8) | hl;
        uint16_t raw_temp = ((uint16_t)(th & 0x7F) << 8) | tl;

        float temp_raw = raw_temp / 10.0f;
        float hum_raw  = raw_hum  / 10.0f;
        if (th & 0x80) temp_raw = -temp_raw;

        s_data.temperature = kalman1d_update(&s_temp_kalman, temp_raw);
        s_data.humidity    = kalman1d_update(&s_hum_kalman,  hum_raw);
        s_data.valid = true;
    } else {
        s_data.valid = false;
    }
}

bool dht22_get_data(dht22_data_t *out) {
    if (!s_ready || !out || !s_data.valid) return false;
    *out = s_data;
    return true;
}

bool dht22_is_ready(void) { return s_ready; }



