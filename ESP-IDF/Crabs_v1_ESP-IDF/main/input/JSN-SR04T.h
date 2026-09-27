/*
 * JSN-SR04T.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef INPUT_JSN_SR04T_H_
#define INPUT_JSN_SR04T_H_

#include "esp_err.h"
#include <stdbool.h>
#include "modules_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <string.h>
#include "kalman_filter.h"
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h" 

esp_err_t jsn_init(void);
void      jsn_update(void);
float     jsn_get_distance_cm(void);
bool      jsn_is_ready(void);

#endif /* INPUT_JSN_SR04T_H_ */
