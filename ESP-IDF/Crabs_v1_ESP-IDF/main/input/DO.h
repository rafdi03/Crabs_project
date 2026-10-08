/*
 * DO.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
#ifndef INPUT_DO_H_
#define INPUT_DO_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inisialisasi sensor DO analog
 */
esp_err_t do_sensor_init(void);

/**
 * @brief Membaca dan memperbarui nilai ADC sensor DO
 */
void do_sensor_update(void);

/**
 * @brief Menghitung nilai DO aktual dengan kompensasi suhu air
 */
void do_sensor_calculate(float temp_c);

/**
 * @brief Mengambil nilai DO aktual (mg/L)
 */
float do_get_value(void);

/**
 * @brief Mengambil estimasi tegangan sensor DO (Volt)
 */
float do_get_voltage(void);

/**
 * @brief Mengambil nilai raw ADC rata-rata sensor DO
 */
float do_get_adc_raw(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_DO_H_ */

