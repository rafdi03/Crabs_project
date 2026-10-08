/*
 * TempWater.h
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef INPUT_DS18B20_H_
#define INPUT_DS18B20_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t ds18b20_init(void);
void      ds18b20_update(void);

/* ==== Getter utama (selalu return nilai valid) ==== */
float     ds18b20_get_temp(void);
bool      ds18b20_is_ready(void);

/* ==== Health & diagnostics ==== */
bool      ds18b20_is_stale(void);                /* data > 10s tidak ada update valid */
uint32_t  ds18b20_get_success_rate_pct(void);    /* % pembacaan valid */
void      ds18b20_get_health(uint32_t *total,
                             uint32_t *valid,
                             uint32_t *rejected,
                             uint32_t *consec_fail);

#endif /* INPUT_DS18B20_H_ */