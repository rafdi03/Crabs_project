/*
 * TempWater.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef INPUT_DS18B20_H_
#define INPUT_DS18B20_H_

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ds18b20_init(void);
void      ds18b20_update(void);
float     ds18b20_get_temp(void);
bool      ds18b20_is_ready(void);

#endif /* INPUT_DS18B20_H_ */
