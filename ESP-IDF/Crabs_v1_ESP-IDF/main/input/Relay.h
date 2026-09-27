/*
 * Relay.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_RELAY_H_
#define MAIN_INCLUDE_RELAY_H_

#include "esp_err.h"
#include <stdbool.h>

esp_err_t relay_init(void);
void      relay_set(uint8_t num, bool state);  // num 0 = semua
bool      relay_get(uint8_t num);              // num 1-5
void      relay_set_all(bool state);

#endif
