#pragma once
#include <stdint.h>
#include "esp_err.h"

// Simple DHT driver API (DHT11/DHT22)
// Compatible names: DHT_TYPE_DHT11 / DHT_TYPE_DHT22
typedef enum { DHT_TYPE_DHT11 = 11, DHT_TYPE_DHT22 = 22 } dht_sensor_type_t;

// Read humidity and temperature as floats. Returns ESP_OK on success.
esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, int pin, float* humidity, float* temperature);
