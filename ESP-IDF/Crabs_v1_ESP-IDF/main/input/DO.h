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

esp_err_t do_sensor_init(void);
void      do_sensor_update(void);
float     do_get_value(void);

#endif /* INPUT_DO_H_ */
