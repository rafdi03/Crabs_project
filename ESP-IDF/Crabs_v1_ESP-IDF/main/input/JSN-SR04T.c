/*
 * JSN-SR04T.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#include "JSN-SR04T.h"

static kalman1d_t s_jsn_kalman;
static const char *TAG = "JSN_SR04T";

static bool s_ready = false;
static float s_buffer[JSN_MEDIAN_COUNT];
static int s_index = 0;
static bool s_buf_init = false;
static float s_distance = -1.0f;

static float median_float(float *arr, int n) {
    float tmp[JSN_MEDIAN_COUNT];
    memcpy(tmp, arr, n * sizeof(float));
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (tmp[j] > tmp[j + 1]) {
                float t = tmp[j]; tmp[j] = tmp[j + 1]; tmp[j + 1] = t;
            }
        }
    }
    if (n & 1) return tmp[n / 2];
    return (tmp[n / 2] + tmp[n / 2 - 1]) / 2.0f;
}

esp_err_t jsn_init(void) {
    gpio_config_t trig_cfg = {
        .pin_bit_mask = (1ULL << JSN_TRIG_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config_t echo_cfg = {
        .pin_bit_mask = (1ULL << JSN_ECHO_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&trig_cfg));
    ESP_ERROR_CHECK(gpio_config(&echo_cfg));

    gpio_set_level(JSN_TRIG_GPIO, 0);
	
	kalman1d_init(&s_jsn_kalman, 0.1f, 1.0f, 0.0f);
	
    s_ready = true;
    ESP_LOGI(TAG, "JSN-SR04T siap (TRIG=%d, ECHO=%d)",
             JSN_TRIG_GPIO, JSN_ECHO_GPIO);
    return ESP_OK;
}

static float jsn_read_raw(void) {
    // 10us Trigger Pulse
    gpio_set_level(JSN_TRIG_GPIO, 0);
    esp_rom_delay_us(4);
    gpio_set_level(JSN_TRIG_GPIO, 1);
    esp_rom_delay_us(12);
    gpio_set_level(JSN_TRIG_GPIO, 0);

    // Tunggu awal echo HIGH (timeout 25ms)
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(JSN_ECHO_GPIO) == 0) {
        if ((esp_timer_get_time() - t0) > 25000) {
            return -1.0f;
        }
    }

    // Ukur durasi pulsa HIGH (timeout 30ms ~ 5 meter)
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(JSN_ECHO_GPIO) == 1) {
        if ((esp_timer_get_time() - start) > 30000) {
            return -1.0f;
        }
    }
    int64_t dur = esp_timer_get_time() - start;

    return (dur * 0.0343f) / 2.0f;
}

void jsn_update(void) {
    if (!s_ready) return;

    float raw = jsn_read_raw();
    if (raw <= 0) return;

    // Tahap 1: median (buang outlier echo palsu)
    if (!s_buf_init) {
        for (int i = 0; i < JSN_MEDIAN_COUNT; i++) s_buffer[i] = raw;
        s_buf_init = true;
    } else {
        s_buffer[s_index] = raw;
        s_index = (s_index + 1) % JSN_MEDIAN_COUNT;
    }
    float median_val = median_float(s_buffer, JSN_MEDIAN_COUNT);

    // Tahap 2: Kalman (haluskan sisa noise)
    // Guard: kalau median lompat >50cm dari estimasi, reset (kemungkinan pindah posisi)
    if (s_jsn_kalman.initialized) {
        float diff = median_val - s_jsn_kalman.x;
        if (diff < 0) diff = -diff;
        if (diff > 50.0f) kalman1d_reset(&s_jsn_kalman);
    }

    s_distance = kalman1d_update(&s_jsn_kalman, median_val);
}

float jsn_get_distance_cm(void) { return s_distance; }
bool jsn_is_ready(void) { return s_ready; }



