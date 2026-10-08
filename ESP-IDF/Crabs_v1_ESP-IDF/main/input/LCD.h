/*
 * LCD.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

 #ifndef MAIN_INCLUDE_LCD_H_
 #define MAIN_INCLUDE_LCD_H_

 #include "esp_err.h"
 #include <stdbool.h>
 #include <stdint.h>
 #include "modules_config.h"
 #include "driver/spi_master.h"
 #include "esp_lcd_panel_io.h"
 #include "esp_lcd_panel_vendor.h"
 #include "esp_lcd_panel_ops.h"
 #include "esp_log.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include <string.h>
 #include "lcd_draw.h"

 /* -------- Snapshot status sistem -------- */
 typedef struct {
     // WiFi
     bool  wifi_connected;
     int   wifi_rssi;
     char  wifi_ip[16];
     uint8_t wifi_state;        // ← TAMBAH: 0=off, 1=connecting, 2=connected, 3=failed

     // GSM
     bool  gsm_connected;
     int   gsm_signal;
     char  gsm_operator[16];
     uint8_t gsm_state;         // ← TAMBAH

     // MQTT
     bool  mqtt_connected;
     uint8_t mqtt_state;        // ← TAMBAH
     uint32_t mqtt_tx_count;
     uint32_t last_tx_ms;       // ← TAMBAH: tick saat terakhir publish

     // System
     uint32_t uptime_sec;
     uint32_t free_heap_kb;

     // Relay
     bool  relay_state[5];
 } lcd_sys_status_t;

 /* -------- Data sensor yang ditampilkan -------- */
 typedef struct {
     float suhu_air;
     int   tds_ppm;
     float jarak_cm;
     float do_mg;
     float suhu_udara;
     float lembap_udara;
     bool  dht_valid;
 } lcd_display_data_t;

 esp_err_t lcd_init(void);
void      lcd_update(void);
void      lcd_set_display_data(const lcd_display_data_t *data);
void      lcd_set_sys_status(const lcd_sys_status_t *st);
bool      lcd_is_ready(void);
void      lcd_set_rotation(uint8_t rotation);
void      lcd_force_full_redraw(void);
void      lcd_toggle_heartbeat(void);

#endif /* MAIN_INCLUDE_LCD_H_ */