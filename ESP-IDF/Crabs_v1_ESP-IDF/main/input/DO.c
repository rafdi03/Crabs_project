/*
 * DO.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#include "DO.h"
#include "TempWater.h"
#include "modules_config.h"
#include "esp_random.h"

static bool s_ready = false;
static float s_value = -999.0f;

esp_err_t do_sensor_init(void) {
    s_ready = true;
    return ESP_OK;
}

void do_sensor_update(void) {
    float suhu = ds18b20_get_temp();
    float base = DO_BASE_DEFAULT;
    if (suhu > 0 && suhu < 50) {
        base = 10.0f - (0.15f * suhu);
    }
    // Noise -100..+100 -> -0.1..+0.1
    float noise = ((int)(esp_random() % 201) - 100) / 1000.0f;
    float result = base + noise;
    if (result < DO_MIN_VALID) result = DO_MIN_VALID;
    if (result > DO_MAX_VALID) result = DO_MAX_VALID;
    s_value = result;
}

float do_get_value(void) { return s_value; }



