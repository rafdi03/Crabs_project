/*
 * DS18B20.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
 #include "TempWater.h"
 #include "modules_config.h"
 #include "onewire_bus.h"
 #include "ds18b20.h"
 #include "kalman_filter.h"         
 #include "esp_log.h"
 #include "esp_timer.h"            
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 static const char *TAG = "DS18B20";

 static bool s_ready = false;
 static float s_last_temp = -999.0f;
 static ds18b20_device_handle_t s_sensor = NULL;

 // ---- State machine non-blocking ----
 typedef enum {
     DS18B20_STATE_IDLE,
     DS18B20_STATE_CONVERTING,
 } ds18b20_state_t;

 static ds18b20_state_t s_state = DS18B20_STATE_IDLE;
 static int64_t s_convert_start_us = 0;

 // ---- Kalman filter ----
 static kalman1d_t s_temp_kalman;

 esp_err_t ds18b20_init(void) {
     onewire_bus_handle_t bus = NULL;
     onewire_bus_config_t bus_cfg = {
         .bus_gpio_num = DS18B20_GPIO,
     };
     onewire_bus_rmt_config_t rmt_cfg = {
         .max_rx_bytes = 10,
     };
     esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "Gagal buat 1-Wire bus: %s", esp_err_to_name(err));
         return err;
     }

     ds18b20_config_t cfg = {};
     err = ds18b20_new_device_from_bus(bus, &cfg, &s_sensor);
     if (err != ESP_OK) {
         ESP_LOGW(TAG, "DS18B20 tidak terdeteksi di GPIO %d", DS18B20_GPIO);
         return err;
     }

     err = ds18b20_set_resolution(s_sensor, DS18B20_RESOLUTION_12B);
     if (err != ESP_OK) {
         ESP_LOGW(TAG, "Gagal set resolusi: %s", esp_err_to_name(err));
         return err;
     }

     // Kalman: DS18B20 sudah akurat → Q kecil, R kecil
     kalman1d_init(&s_temp_kalman, 0.001f, 0.1f, 0.0f);

     s_ready = true;
     ESP_LOGI(TAG, "DS18B20 siap di GPIO %d (non-blocking)", DS18B20_GPIO);
     return ESP_OK;
 }

 void ds18b20_update(void) {
     if (!s_ready) return;

     switch (s_state) {

     case DS18B20_STATE_IDLE:
         // Trigger konversi baru, lalu langsung keluar (tidak nunggu)
         if (ds18b20_trigger_temperature_conversion(s_sensor) == ESP_OK) {
             s_convert_start_us = esp_timer_get_time();
             s_state = DS18B20_STATE_CONVERTING;
         } else {
             s_last_temp = -127.0f;
         }
         break;

     case DS18B20_STATE_CONVERTING:
         // Cek apakah 750 ms sudah lewat
         if ((esp_timer_get_time() - s_convert_start_us) >= 750000) {
             float temp;
             if (ds18b20_get_temperature(s_sensor, &temp) == ESP_OK) {
                 s_last_temp = kalman1d_update(&s_temp_kalman, temp);
             } else {
                 s_last_temp = -127.0f;
             }
             s_state = DS18B20_STATE_IDLE;
         }
         break;
     }
 }

 float ds18b20_get_temp(void) { return s_last_temp; }
 bool  ds18b20_is_ready(void) { return s_ready; }