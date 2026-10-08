/*
 * TDS.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#include "TDS.h"
#include "modules_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <string.h>
#include "kalman_filter.h"

static kalman1d_t s_tds_kalman;

static const char *TAG = "TDS";

static bool s_ready = false;
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_channel_t s_channel;

static int s_buffer[TDS_SAMPLE_COUNT];
static int s_index = 0;
static int s_ppm = -999;
static int s_raw_adc = 0;

static int median_int(int *arr, int n) {
    int tmp[TDS_SAMPLE_COUNT];
    memcpy(tmp, arr, n * sizeof(int));
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (tmp[j] > tmp[j + 1]) {
                int t = tmp[j]; tmp[j] = tmp[j + 1]; tmp[j + 1] = t;
            }
        }
    }
    if (n & 1) return tmp[n / 2];
    return (tmp[n / 2] + tmp[n / 2 - 1]) / 2;
}

static int hitung_ppm(int adc_raw) {
    long ppm = (long)(adc_raw - TDS_ADC_AIR_MURNI) *
               (TDS_PPM_AIR_KERAN - TDS_PPM_AIR_MURNI) /
               (TDS_ADC_AIR_KERAN - TDS_ADC_AIR_MURNI) +
               TDS_PPM_AIR_MURNI;
    if (ppm < 0) ppm = 0;
    return (int)ppm;
}

esp_err_t tds_init(void) {
    /* Pakai ADC1 yang sudah dipakai bersama DO */
    esp_err_t ret = adc_shared_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC shared init gagal: %s", esp_err_to_name(ret));
        return ret;
    }
    s_adc = adc_shared_get();
    if (s_adc == NULL) {
        ESP_LOGE(TAG, "ADC handle NULL");
        return ESP_FAIL;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = TDS_ADC_ATTEN,     // pakai define, konsisten
        .bitwidth = ADC_BITWIDTH_12,
    };
    s_channel = TDS_ADC_CHANNEL;       // ADC_CHANNEL_6 (GPIO 34)

    ret = adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config gagal: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(s_buffer, 0, sizeof(s_buffer));

    kalman1d_init(&s_tds_kalman, 0.01f, 5.0f, 0.0f);

    s_ready = true;
    ESP_LOGI(TAG, "TDS siap di ADC1_CH6 (GPIO 34)");
    return ESP_OK;
}

void tds_update(void) {
    if (!s_ready) return;

    int raw = 0;
    if (adc_oneshot_read(s_adc, s_channel, &raw) != ESP_OK) return;

    s_buffer[s_index] = raw;
    s_index = (s_index + 1) % TDS_SAMPLE_COUNT;

    // Tahap 1: median (buang outlier ADC)
    int med = median_int(s_buffer, TDS_SAMPLE_COUNT);
    s_raw_adc = med;

    // Tahap 2: konversi ke ppm
    int ppm_raw = hitung_ppm(med);

    // Tahap 3: Kalman (haluskan noise konversi)
    float filtered = kalman1d_update(&s_tds_kalman, (float)ppm_raw);
    s_ppm = (int)(filtered + 0.5f);
}

int tds_get_ppm(void) { return s_ppm; }
int tds_get_raw_adc(void) { return s_raw_adc; }
bool tds_is_ready(void) { return s_ready; }



