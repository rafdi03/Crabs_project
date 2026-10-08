/*
 * TempWater.c — Robust DS18B20 Driver v3
 *
 * Fix penting:
 *   - Verify sensor dengan set_resolution di init
 *   - Full teardown + reinit supaya RMT channel bersih
 *   - Retry 60 detik, bukan spam tiap detik
 *   - Tidak pernah set s_ready=true kalau sensor tidak responsif
 */
#include "TempWater.h"
#include "modules_config.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "kalman_filter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "DS18B20";

/* ============================================================
 * KONFIGURASI
 * ============================================================ */
#define DS18B20_VALID_MIN_C         (-5.0f)
#define DS18B20_VALID_MAX_C         (60.0f)
#define DS18B20_MAX_DELTA_C         (1.0f)
#define DS18B20_MAE_WINDOW          (5)
#define DS18B20_STALE_TIMEOUT_MS    (10000)
#define DS18B20_FALLBACK_TEMP_C     (25.0f)
#define DS18B20_CONVERT_WAIT_US     (750000)
#define DS18B20_RETRY_INTERVAL_MS   (60000)     /* 60 detik */

/* ============================================================
 * STATE
 * ============================================================ */
static bool s_ready = false;

/* Bus & sensor handle — perlu disimpan supaya bisa di-delete */
static onewire_bus_handle_t    s_bus    = NULL;
static ds18b20_device_handle_t s_sensor = NULL;

/* Output */
static float   s_last_temp  = DS18B20_FALLBACK_TEMP_C;
static float   s_last_raw   = DS18B20_FALLBACK_TEMP_C;
static bool    s_ever_valid = false;
static int64_t s_last_valid_us = 0;

/* MAE */
static float   s_mae_buf[DS18B20_MAE_WINDOW];
static uint8_t s_mae_idx   = 0;
static uint8_t s_mae_count = 0;

/* Kalman */
static kalman1d_t s_temp_kalman;

/* Statistik */
static uint32_t s_total_reads  = 0;
static uint32_t s_total_valid  = 0;
static uint32_t s_total_reject = 0;
static uint32_t s_consec_fail  = 0;

/* Retry timer */
static int64_t s_last_retry_us = 0;

/* State machine */
typedef enum {
    DS18B20_STATE_IDLE,
    DS18B20_STATE_CONVERTING,
} ds18b20_state_t;
static ds18b20_state_t s_state = DS18B20_STATE_IDLE;
static int64_t s_convert_start_us = 0;

/* ============================================================
 * TEARDOWN — buang bus & sensor, biarkan RMT channel di-release
 * ============================================================ */
static void ds18b20_teardown(void)
{
    s_ready  = false;
    s_state  = DS18B20_STATE_IDLE;
    s_sensor = NULL;

    if (s_bus != NULL) {
        esp_err_t err = onewire_bus_del(s_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "onewire_bus_del gagal: %s", esp_err_to_name(err));
        }
        s_bus = NULL;
    }
}

/* ============================================================
 * HELPERS
 * ============================================================ */
static bool ds18b20_is_value_valid(float v)
{
    if (isnan(v) || isinf(v))       return false;
    if (v == -127.0f || v == 85.0f) return false;
    if (v < DS18B20_VALID_MIN_C)    return false;
    if (v > DS18B20_VALID_MAX_C)    return false;
    return true;
}

static float ds18b20_apply_mae(float sample, bool *rejected)
{
    *rejected = false;

    if (s_ever_valid && fabsf(sample - s_last_raw) > DS18B20_MAX_DELTA_C) {
        *rejected = true;
        return s_last_temp;
    }

    s_mae_buf[s_mae_idx] = sample;
    s_mae_idx = (s_mae_idx + 1) % DS18B20_MAE_WINDOW;
    if (s_mae_count < DS18B20_MAE_WINDOW) s_mae_count++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < s_mae_count; i++) sum += s_mae_buf[i];
    s_last_raw = sample;
    return sum / (float)s_mae_count;
}

static void ds18b20_mark_invalid(void)
{
    s_consec_fail++;
    s_total_reject++;

    if (!s_ever_valid) {
        s_last_temp = DS18B20_FALLBACK_TEMP_C;
        return;
    }
    int64_t now = esp_timer_get_time();
    if ((now - s_last_valid_us) / 1000 >= DS18B20_STALE_TIMEOUT_MS) {
        s_last_temp = DS18B20_FALLBACK_TEMP_C;
    }
}

static void ds18b20_mark_valid(float filtered)
{
    s_last_temp     = filtered;
    s_last_valid_us = esp_timer_get_time();
    s_ever_valid    = true;
    s_consec_fail   = 0;
    s_total_valid++;
}

/* ============================================================
 * INIT — full reinit dengan verifikasi
 * ============================================================ */
esp_err_t ds18b20_init(void)
{
    /* Bersihkan dulu (kalau dipanggil ulang) */
    ds18b20_teardown();

    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = DS18B20_GPIO,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };

    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal buat 1-Wire bus: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }

    ds18b20_config_t cfg = {};
    err = ds18b20_new_device_from_bus(s_bus, &cfg, &s_sensor);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 tidak terdeteksi di GPIO %d", DS18B20_GPIO);
        ds18b20_teardown();
        return err;
    }

    /* VERIFIKASI: sensor harus bisa di-set resolusi.
     * Kalau tidak, berarti bus broken → cleanup. */
    err = ds18b20_set_resolution(s_sensor, DS18B20_RESOLUTION_12B);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor tidak responsif (%s), buang bus",
                 esp_err_to_name(err));
        ds18b20_teardown();
        return err;
    }

    kalman1d_init(&s_temp_kalman, 0.001f, 0.1f, 0.0f);

    for (int i = 0; i < DS18B20_MAE_WINDOW; i++) s_mae_buf[i] = 0.0f;
    s_mae_idx   = 0;
    s_mae_count = 0;

    s_ready        = true;
    s_ever_valid   = false;
    s_last_valid_us = 0;
    s_last_temp    = DS18B20_FALLBACK_TEMP_C;
    s_last_raw     = DS18B20_FALLBACK_TEMP_C;
    s_state        = DS18B20_STATE_IDLE;
    s_last_retry_us = esp_timer_get_time();

    ESP_LOGI(TAG, "DS18B20 siap di GPIO %d (verified, robust)",
             DS18B20_GPIO);
    return ESP_OK;
}

/* ============================================================
 * UPDATE
 * ============================================================ */
void ds18b20_update(void)
{
    /* ---- Mode retry ---- */
    if (!s_ready) {
        int64_t now = esp_timer_get_time();
        if ((now - s_last_retry_us) / 1000 >= DS18B20_RETRY_INTERVAL_MS) {
            s_last_retry_us = now;
            ESP_LOGI(TAG, "Retry init DS18B20...");
            if (ds18b20_init() == ESP_OK) {
                ESP_LOGI(TAG, "Late-init DS18B20 OK");
            }
        }
        return;
    }

    switch (s_state) {

    case DS18B20_STATE_IDLE:
        if (ds18b20_trigger_temperature_conversion(s_sensor) == ESP_OK) {
            s_convert_start_us = esp_timer_get_time();
            s_state = DS18B20_STATE_CONVERTING;
        } else {
            /* Driver failure — berpotensi bus rusak */
            s_total_reads++;
            ds18b20_mark_invalid();

            /* Paksa re-init untuk bersihkan RMT state */
            ESP_LOGW(TAG, "Trigger gagal, re-init bus...");
            ds18b20_teardown();
        }
        break;

    case DS18B20_STATE_CONVERTING:
        if ((esp_timer_get_time() - s_convert_start_us) >=
            DS18B20_CONVERT_WAIT_US) {

            float raw = 0.0f;
            s_total_reads++;

            if (ds18b20_get_temperature(s_sensor, &raw) != ESP_OK ||
                !ds18b20_is_value_valid(raw)) {
                ds18b20_mark_invalid();
                s_state = DS18B20_STATE_IDLE;
                break;
            }

            bool rejected = false;
            float mae_val = ds18b20_apply_mae(raw, &rejected);
            if (rejected) {
                ESP_LOGW(TAG, "Rate limit: %.2f -> %.2f", s_last_raw, raw);
                ds18b20_mark_invalid();
                s_state = DS18B20_STATE_IDLE;
                break;
            }

            float filtered = kalman1d_update(&s_temp_kalman, mae_val);
            ds18b20_mark_valid(filtered);
            s_state = DS18B20_STATE_IDLE;
        }
        break;
    }
}

/* ============================================================
 * GETTERS
 * ============================================================ */
float ds18b20_get_temp(void) { return s_last_temp; }
bool  ds18b20_is_ready(void) { return s_ready; }

bool ds18b20_is_stale(void)
{
    if (!s_ever_valid) return true;
    int64_t now = esp_timer_get_time();
    return ((now - s_last_valid_us) / 1000) >= DS18B20_STALE_TIMEOUT_MS;
}

uint32_t ds18b20_get_success_rate_pct(void)
{
    if (s_total_reads == 0) return 0;
    return (uint32_t)((s_total_valid * 100ULL) / s_total_reads);
}

void ds18b20_get_health(uint32_t *total, uint32_t *valid,
                        uint32_t *rejected, uint32_t *consec_fail)
{
    if (total)       *total       = s_total_reads;
    if (valid)       *valid       = s_total_valid;
    if (rejected)    *rejected    = s_total_reject;
    if (consec_fail) *consec_fail = s_consec_fail;
}