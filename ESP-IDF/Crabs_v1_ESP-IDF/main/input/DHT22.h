/*
 * DHT22.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_DHT22_H_
#define MAIN_INCLUDE_DHT22_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float temperature;      // Celsius
    float humidity;         // Percent
    bool  valid;
} dht22_data_t;

esp_err_t dht22_init(void);

/* Baca hardware, update cache kalau sukses. Tidak mem-block lama. */
void      dht22_update(void);
bool      dht22_get_data(dht22_data_t *out);

bool      dht22_is_ready(void);
bool      dht22_is_stale(void);
uint32_t  dht22_get_success_rate_pct(void);

#endif