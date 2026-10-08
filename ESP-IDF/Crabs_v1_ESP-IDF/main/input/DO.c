#include "DO.h"

#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "modules_config.h"
#include "adc_shared.h"

static const char *TAG = "DO_SENSOR";

static bool  s_configured  = false;
static float s_adc_average = 0.0f;
static float s_voltage     = 0.0f;
static float s_do_mg       = 0.0f;

/* ============================================================
 * PARAMETER KALIBRASI
 * ------------------------------------------------------------
 * DO_V_SATURATED_25C : tegangan output sensor saat probe ada di
 *                      air jenuh oksigen pada 25°C.
 *                      -> WAJIB diukur/di-tuning saat kalibrasi.
 * DO_V_ZERO          : tegangan saat kadar O2 ~ 0 (opsional).
 * ============================================================ */
#define DO_V_SATURATED_25C   2.30f
#define DO_V_ZERO            0.00f

/* Kelarutan O2 jenuh (mg/L) di air tawar 1 atm sebagai fungsi suhu.
 * Rumus polinomial APHA (0..40 °C). */
static float do_saturation_mg_per_l(float t)
{
    if (t < 0.0f)  t = 0.0f;
    if (t > 40.0f) t = 40.0f;
    float t2 = t * t;
    float t3 = t2 * t;
    return 14.652f - 0.41022f * t + 0.007991f * t2 - 0.000077774f * t3;
}

esp_err_t do_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing DO sensor...");

    esp_err_t ret = adc_shared_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC shared init gagal: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_unit_handle_t adc = adc_shared_get();
    if (adc == NULL) return ESP_FAIL;

    adc_oneshot_chan_cfg_t cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = DO_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc, DO_ADC_CHANNEL, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config gagal: %s", esp_err_to_name(ret));
        return ret;
    }

    s_configured  = true;
    s_adc_average = 0.0f;
    s_voltage     = 0.0f;
    s_do_mg       = 0.0f;

    ESP_LOGI(TAG, "DO sensor OK | GPIO %d | ch %d | samples %d",
             DO_ADC_GPIO, DO_ADC_CHANNEL, DO_SAMPLE_COUNT);
    return ESP_OK;
}

void do_sensor_update(void)
{
    if (!s_configured) return;

    adc_oneshot_unit_handle_t adc = adc_shared_get();
    if (adc == NULL) return;

    uint32_t sum   = 0;
    int      valid = 0;

    for (int i = 0; i < DO_SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc, DO_ADC_CHANNEL, &raw) == ESP_OK) {
            sum += (uint32_t)raw;
            valid++;
        }
    }

    if (valid > 0) {
        s_adc_average = (float)sum / (float)valid;
        s_voltage     = (s_adc_average / 4095.0f) * 3.3f;
    }
}

void do_sensor_calculate(float temp_c)
{
    if (!s_configured || s_voltage <= 0.0f) {
        s_do_mg = 0.0f;
        return;
    }

    /* Defense-in-depth: kalau suhu dari luar tidak valid */
    if (isnan(temp_c) || isinf(temp_c) ||
        temp_c < -50.0f || temp_c > 80.0f) {
        temp_c = 25.0f;
    }

    float sat_mg = do_saturation_mg_per_l(temp_c);
    float denom  = DO_V_SATURATED_25C - DO_V_ZERO;
    if (denom <= 0.01f) denom = DO_V_SATURATED_25C;

    float do_mg = ((s_voltage - DO_V_ZERO) / denom) * sat_mg;

    if (do_mg < 0.0f)  do_mg = 0.0f;
    if (do_mg > 20.0f) do_mg = 20.0f;

    s_do_mg = do_mg;
}

float do_get_value(void)   { return s_do_mg; }
float do_get_voltage(void) { return s_voltage; }
float do_get_adc_raw(void) { return s_adc_average; }