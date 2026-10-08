/*
 * DHT22.c — Robust driver + last-good cache
 *
 * Layer:
 *   1. Sanity check     : tolak NaN/Inf, suhu/kelembapan di luar range
 *   2. Retry            : 3x percobaan hardware per update, jeda 20 ms
 *   3. Moving Average   : rolling window 3 sampel
 *   4. Kalman filter    : smoothing akhir
 *   5. Last-good cache  : kalau gagal, nilai valid terakhir dipertahankan
 *   6. Stale timeout    : >30 detik tanpa sukses → tandai stale
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
#include <math.h>

static const char *TAG = "DHT22";

/* ============================================================
 * KONFIGURASI
 * ============================================================ */
#define DHT_VALID_TEMP_MIN_C   (-40.0f)
#define DHT_VALID_TEMP_MAX_C   ( 80.0f)
#define DHT_VALID_HUM_MIN_PCT  (  0.0f)
#define DHT_VALID_HUM_MAX_PCT  (100.0f)

#define DHT_STALE_TIMEOUT_US   (30LL * 1000000LL)   /* 30 detik */
#define DHT_READ_RETRY         (3)
#define DHT_RETRY_DELAY_MS     (20)
#define DHT_MAE_WINDOW         (3)

/* ============================================================
 * STATE
 * ============================================================ */
static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

static kalman1d_t s_temp_kalman;
static kalman1d_t s_hum_kalman;

static bool s_ready = false;

/* Last-good cache */
static dht22_data_t s_last_good     = {0};
static bool         s_ever_valid    = false;
static int64_t      s_last_valid_us = 0;
static uint32_t     s_fail_streak   = 0;

/* Moving average buffer */
static float   s_mae_temp[DHT_MAE_WINDOW];
static float   s_mae_hum [DHT_MAE_WINDOW];
static uint8_t s_mae_idx   = 0;
static uint8_t s_mae_count = 0;

/* Statistik */
static uint32_t s_read_attempts = 0;
static uint32_t s_read_success  = 0;

/* ============================================================
 * LOW-LEVEL
 * ============================================================ */
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

    /* Start signal */
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(DHT22_GPIO, 1);
    dht_delay_us(30);
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_INPUT);

    /* Critical section ~2 ms */
    portDISABLE_INTERRUPTS();

    if (dht_wait_level(1, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
    if (dht_wait_level(0, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
    if (dht_wait_level(1, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }

    for (int i = 0; i < 40; i++) {
        if (dht_wait_level(0, 100) < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
        int high_us = dht_wait_level(1, 100);
        if (high_us < 0) { portENABLE_INTERRUPTS(); return ESP_ERR_TIMEOUT; }
        data[i / 8] <<= 1;
        if (high_us > 40) data[i / 8] |= 1;
    }

    portENABLE_INTERRUPTS();

    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) return ESP_ERR_INVALID_CRC;

    *hum_h  = data[0];
    *hum_l  = data[1];
    *temp_h = data[2];
    *temp_l = data[3];
    return ESP_OK;
}

/* ============================================================
 * HELPERS — Layer 1, 3
 * ============================================================ */
static bool dht_value_valid(float t, float h) {
    if (isnan(t) || isnan(h) || isinf(t) || isinf(h))     return false;
    if (t < DHT_VALID_TEMP_MIN_C || t > DHT_VALID_TEMP_MAX_C) return false;
    if (h < DHT_VALID_HUM_MIN_PCT || h > DHT_VALID_HUM_MAX_PCT) return false;
    return true;
}

static void dht_mae_reset(void) {
    for (int i = 0; i < DHT_MAE_WINDOW; i++) {
        s_mae_temp[i] = 0.0f;
        s_mae_hum[i]  = 0.0f;
    }
    s_mae_idx   = 0;
    s_mae_count = 0;
}

static void dht_mae_push(float t, float h, float *out_t, float *out_h) {
    s_mae_temp[s_mae_idx] = t;
    s_mae_hum[s_mae_idx]  = h;
    s_mae_idx = (s_mae_idx + 1) % DHT_MAE_WINDOW;
    if (s_mae_count < DHT_MAE_WINDOW) s_mae_count++;

    float st = 0.0f, sh = 0.0f;
    for (uint8_t i = 0; i < s_mae_count; i++) {
        st += s_mae_temp[i];
        sh += s_mae_hum[i];
    }
    *out_t = st / (float)s_mae_count;
    *out_h = sh / (float)s_mae_count;
}

/* ============================================================
 * INIT
 * ============================================================ */
esp_err_t dht22_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DHT22_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config gagal: %s", esp_err_to_name(err));
        return err;
    }

    kalman1d_init(&s_temp_kalman, 0.01f, 0.5f, 0.0f);
    kalman1d_init(&s_hum_kalman,  0.01f, 2.0f, 0.0f);

    dht_mae_reset();
    s_last_good     = (dht22_data_t){0};
    s_ever_valid    = false;
    s_last_valid_us = 0;
    s_fail_streak   = 0;
    s_read_attempts = 0;
    s_read_success  = 0;

    s_ready = true;
    ESP_LOGI(TAG, "DHT22 siap di GPIO %d (robust + last-good cache)", DHT22_GPIO);
    return ESP_OK;
}

/* ============================================================
 * UPDATE
 * ============================================================ */
void dht22_update(void) {
    if (!s_ready) return;

    uint8_t hh, hl, th, tl;
    esp_err_t err = ESP_FAIL;

    /* Layer 2: Retry */
    for (int i = 0; i < DHT_READ_RETRY; i++) {
        s_read_attempts++;
        err = dht_read_raw(&hh, &hl, &th, &tl);
        if (err == ESP_OK) break;
        if (i < DHT_READ_RETRY - 1) {
            vTaskDelay(pdMS_TO_TICKS(DHT_RETRY_DELAY_MS));
        }
    }

    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_dht_mux);
        s_fail_streak++;
        portEXIT_CRITICAL(&s_dht_mux);
        /* Cache TIDAK diubah — nilai valid terakhir tetap dipakai */
        return;
    }

    /* Parse */
    uint16_t raw_hum  = ((uint16_t)hh << 8) | hl;
    uint16_t raw_temp = ((uint16_t)(th & 0x7F) << 8) | tl;

    float temp_raw = raw_temp / 10.0f;
    float hum_raw  = raw_hum  / 10.0f;
    if (th & 0x80) temp_raw = -temp_raw;

    /* Layer 1: Sanity */
    if (!dht_value_valid(temp_raw, hum_raw)) {
        portENTER_CRITICAL(&s_dht_mux);
        s_fail_streak++;
        portEXIT_CRITICAL(&s_dht_mux);
        return;
    }

    s_read_success++;

    /* Layer 3: Moving Average */
    float mae_t, mae_h;
    dht_mae_push(temp_raw, hum_raw, &mae_t, &mae_h);

    /* Layer 4: Kalman */
    float temp_f = kalman1d_update(&s_temp_kalman, mae_t);
    float hum_f  = kalman1d_update(&s_hum_kalman,  mae_h);

    /* Layer 5: Refresh last-good cache */
    portENTER_CRITICAL(&s_dht_mux);
    s_last_good.temperature = temp_f;
    s_last_good.humidity    = hum_f;
    s_last_good.valid       = true;
    s_ever_valid            = true;
    s_last_valid_us         = esp_timer_get_time();
    s_fail_streak           = 0;
    portEXIT_CRITICAL(&s_dht_mux);
}

/* ============================================================
 * GETTER
 * ============================================================ */
bool dht22_get_data(dht22_data_t *out) {
    if (!s_ready || out == NULL) return false;

    portENTER_CRITICAL(&s_dht_mux);
    bool         ever_valid = s_ever_valid;
    int64_t      last_valid = s_last_valid_us;
    dht22_data_t cached     = s_last_good;
    portEXIT_CRITICAL(&s_dht_mux);

    if (!ever_valid) return false;

    /* Layer 6: Stale check */
    int64_t age = esp_timer_get_time() - last_valid;
    if (age > DHT_STALE_TIMEOUT_US) {
        return false;
    }

    *out = cached;
    return true;
}

bool dht22_is_ready(void) { return s_ready; }

bool dht22_is_stale(void) {
    if (!s_ever_valid) return true;
    int64_t age = esp_timer_get_time() - s_last_valid_us;
    return age > DHT_STALE_TIMEOUT_US;
}

uint32_t dht22_get_success_rate_pct(void) {
    if (s_read_attempts == 0) return 0;
    return (uint32_t)((s_read_success * 100ULL) / s_read_attempts);
}