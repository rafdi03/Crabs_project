/*
 * Relay.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#include "Relay.h"
#include "modules_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RELAY";

static const gpio_num_t RELAY_PINS[RELAY_COUNT] = {
    RELAY_GPIO_1, RELAY_GPIO_2, RELAY_GPIO_3, RELAY_GPIO_4, RELAY_GPIO_5
};

static bool s_state[RELAY_COUNT] = {false};

// Active LOW: LOW = ON, HIGH = OFF
#define RELAY_ON_LEVEL  0
#define RELAY_OFF_LEVEL 1

esp_err_t relay_init(void) {
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << RELAY_PINS[i]),
            .mode         = GPIO_MODE_OUTPUT,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) return err;
        gpio_set_level(RELAY_PINS[i], RELAY_OFF_LEVEL);
        s_state[i] = false;
    }
    ESP_LOGI(TAG, "Relay %d-channel siap", RELAY_COUNT);
    return ESP_OK;
}

void relay_set(uint8_t num, bool state) {
    if (num == 0) {
        relay_set_all(state);
        return;
    }
    if (num < 1 || num > RELAY_COUNT) return;
    int idx = num - 1;
    s_state[idx] = state;
    gpio_set_level(RELAY_PINS[idx], state ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    ESP_LOGI(TAG, "Relay %d (GPIO %d) -> %s",
             num, RELAY_PINS[idx], state ? "ON" : "OFF");
}

void relay_set_all(bool state) {
    for (int i = 0; i < RELAY_COUNT; i++) {
        s_state[i] = state;
        gpio_set_level(RELAY_PINS[i], state ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    }
    ESP_LOGI(TAG, "Semua relay -> %s", state ? "ON" : "OFF");
}

bool relay_get(uint8_t num) {
    if (num < 1 || num > RELAY_COUNT) return false;
    return s_state[num - 1];
}



