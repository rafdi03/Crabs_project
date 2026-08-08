/*
 * ESP-IDF Main C File: monitoring_tambak
 * Output Bersih & Ringkas untuk Monitoring Tambak Udang / Ikan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Library DHT Component
#include "dht.h"

// ==========================================
// KONFIGURASI WIFI & MQTT
// ==========================================
#define WIFI_SSID      "Bayu"
#define WIFI_PASS      "12345678"
#define MQTT_BROKER    "mqtt://broker.emqx.io:1883"
#define DEVICE_ID      "ESP32-001"
#define MQTT_TOPIC     "tambak/ESP32-001/sensor"

// ==========================================
// KONFIGURASI PIN SENSOR
// ==========================================
#define DS18B20_PIN    GPIO_NUM_33
#define DHT_PIN        GPIO_NUM_32
#define DHT_TYPE       DHT_TYPE_DHT22  // Ubah ke DHT_TYPE_DHT11 jika menggunakan DHT11

#define JSN_TRIG_PIN   GPIO_NUM_18
#define JSN_ECHO_PIN   GPIO_NUM_34

#define TDS_ADC_CHANNEL ADC_CHANNEL_3  // GPIO 39 (Sensor_VN)

// ==========================================
// STATUS GLOBAL
// ==========================================
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static bool is_wifi_connected = false;
static bool is_mqtt_connected = false;
static char ip_address_str[32] = "Belum Terhubung";

static adc_oneshot_unit_handle_t tds_adc_handle = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// EVENT HANDLER (WIFI & IP)
// ==========================================
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_wifi_connected = false;
        snprintf(ip_address_str, sizeof(ip_address_str), "Terputus");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(ip_address_str, sizeof(ip_address_str), IPSTR, IP2STR(&event->ip_info.ip));
        is_wifi_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ==========================================
// MQTT EVENT HANDLER
// ==========================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        is_mqtt_connected = true;
    } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        is_mqtt_connected = false;
    }
}

// ==========================================
// INISIALISASI WIFI
// ==========================================
void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(50); // Mencegah lonjakan arus berlebih dari port USB
}

// ==========================================
// SINKRONISASI WAKTU (SNTP)
// ==========================================
void initialize_sntp(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 5) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    setenv("TZ", "UTC0", 1);
    tzset();
}

// ==========================================
// BIT-BANGING DS18B20 (SUHU AIR)
// ==========================================
static bool ds18b20_reset(void) {
    taskENTER_CRITICAL(&mux);
    gpio_set_level(DS18B20_PIN, 0);
    esp_rom_delay_us(480);
    gpio_set_level(DS18B20_PIN, 1);
    esp_rom_delay_us(70);
    int presence = gpio_get_level(DS18B20_PIN);
    esp_rom_delay_us(410);
    taskEXIT_CRITICAL(&mux);
    return (presence == 0);
}

static void ds18b20_write_bit(int bit) {
    taskENTER_CRITICAL(&mux);
    gpio_set_level(DS18B20_PIN, 0);
    if (bit) {
        esp_rom_delay_us(6);
        gpio_set_level(DS18B20_PIN, 1);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        gpio_set_level(DS18B20_PIN, 1);
        esp_rom_delay_us(10);
    }
    taskEXIT_CRITICAL(&mux);
}

static int ds18b20_read_bit(void) {
    taskENTER_CRITICAL(&mux);
    gpio_set_level(DS18B20_PIN, 0);
    esp_rom_delay_us(6);
    gpio_set_level(DS18B20_PIN, 1);
    esp_rom_delay_us(9);
    int bit = gpio_get_level(DS18B20_PIN);
    esp_rom_delay_us(55);
    taskEXIT_CRITICAL(&mux);
    return bit;
}

static void ds18b20_write_byte(uint8_t value) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit((value >> i) & 1);
    }
}

static uint8_t ds18b20_read_byte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (ds18b20_read_bit() << i);
    }
    return value;
}

static bool ds18b20_read_temperature(float *temperature) {
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0x44); // Start conversion

    vTaskDelay(pdMS_TO_TICKS(750)); // Tunggu konversi suhu

    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0xBE); // Read Scratchpad

    uint8_t temp_lsb = ds18b20_read_byte();
    uint8_t temp_msb = ds18b20_read_byte();
    for (int i = 0; i < 7; i++) {
        ds18b20_read_byte();
    }

    int16_t raw = (int16_t)((temp_msb << 8) | temp_lsb);
    *temperature = raw * 0.0625f;
    return (*temperature > -55.0f && *temperature < 125.0f);
}

// ==========================================
// BACA JSN-SR04T (LEVEL / JARAK AIR)
// ==========================================
static float jsn_read_distance(void) {
    gpio_set_level(JSN_TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(JSN_TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(JSN_TRIG_PIN, 0);

    int64_t start_time = esp_timer_get_time();
    // Tunggu sinyal Echo mulai HIGH (timeout 30ms)
    while (gpio_get_level(JSN_ECHO_PIN) == 0) {
        if (esp_timer_get_time() - start_time > 30000) {
            return 0.0f; // Tidak terdeteksi
        }
    }

    int64_t echo_start = esp_timer_get_time();
    // Tunggu sinyal Echo kembali LOW (timeout 30ms)
    while (gpio_get_level(JSN_ECHO_PIN) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) {
            return 0.0f; // Timeout
        }
    }
    int64_t echo_end = esp_timer_get_time();

    int64_t duration_us = echo_end - echo_start;
    float distance_cm = (duration_us * 0.0343f) / 2.0f;
    return (distance_cm > 0.0f && distance_cm < 600.0f) ? distance_cm : 0.0f;
}

// ==========================================
// BACA SENSOR TDS (DENGAN KOMPENSASI SUHU)
// ==========================================
static float tds_read_ppm(float water_temp) {
    if (tds_adc_handle == NULL) return 0.0f;

    const int SAMPLES = 10;
    uint32_t total_raw = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int adc_value = 0;
        esp_err_t err = adc_oneshot_read(tds_adc_handle, TDS_ADC_CHANNEL, &adc_value);
        if (err == ESP_OK) {
            total_raw += (uint32_t)adc_value;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    float avg_raw = (float)total_raw / SAMPLES;
    if (avg_raw < 10.0f) return 0.0f; // Tidak terhubung / ground

    float voltage = (avg_raw / 4095.0f) * 3.3f;
    float temp = (water_temp > 0.0f) ? water_temp : 25.0f;
    float compensation_coeff = 1.0f + 0.02f * (temp - 25.0f);
    float comp_voltage = voltage / compensation_coeff;

    float tds = (133.42f * comp_voltage * comp_voltage * comp_voltage 
                 - 255.86f * comp_voltage * comp_voltage 
                 + 857.39f * comp_voltage) * 0.5f;

    return (tds > 0.0f) ? tds : 0.0f;
}

// ==========================================
// INISIALISASI HARDWARE
// ==========================================
void hardware_init() {
    // 1. DS18B20 GPIO
    gpio_config_t io_conf_ds = {
        .pin_bit_mask = 1ULL << DS18B20_PIN,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_ds);
    gpio_set_level(DS18B20_PIN, 1);

    // 2. JSN-SR04T GPIO
    gpio_config_t io_conf_jsn = {
        .pin_bit_mask = (1ULL << JSN_TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_jsn);
    gpio_set_level(JSN_TRIG_PIN, 0);

    gpio_config_t io_conf_echo = {
        .pin_bit_mask = (1ULL << JSN_ECHO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_echo);

    // 3. ADC untuk TDS Sensor
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&adc_cfg, &tds_adc_handle);

    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(tds_adc_handle, TDS_ADC_CHANNEL, &adc_chan_cfg);
}

// ==========================================
// TUGAS BACA SENSOR & PUBLISH MQTT (TAMPILAN BERSIH)
// ==========================================
void sensor_task(void *pvParameters) {
    while (1) {
        // 1. Baca Suhu Air (DS18B20) -> jika tidak terpasang/gagal = 0.00
        float suhu_air = 0.0f;
        if (!ds18b20_read_temperature(&suhu_air)) {
            suhu_air = 0.0f;
        }

        // 2. Baca DHT22/DHT11 -> jika gagal/tidak terpasang = 0.00
        float suhu_lingkungan = 0.0f;
        float kelembaban = 0.0f;
        esp_err_t res = dht_read_float_data(DHT_TYPE, DHT_PIN, &kelembaban, &suhu_lingkungan);
        if (res != ESP_OK) {
            suhu_lingkungan = 0.0f;
            kelembaban = 0.0f;
        }

        // 3. Baca JSN-SR04T (Jarak Air) -> jika tidak terpasang/timeout = 0.00
        float jsn_val = jsn_read_distance();

        // 4. Baca TDS Sensor -> jika tidak terpasang = 0.00
        float tds_val = tds_read_ppm(suhu_air);

        // 5. Sensor Lain (Placeholder 0.00)
        float nitrat_val = 0.0f;
        float do_val = 0.0f;

        // 6. Ambil Waktu Timestamp UTC
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        // 7. Format JSON Payload
        char json_string[256];
        snprintf(json_string, sizeof(json_string),
            "{\"tds\":%.2f,\"jsn\":%.2f,\"nitrat\":%.2f,\"do\":%.2f,\"suhu_air\":%.2f,\"suhu_lingkungan\":%.2f,\"timestamp\":\"%s\"}",
            tds_val, jsn_val, nitrat_val, do_val, suhu_air, suhu_lingkungan, timestamp);

        // 8. Kirim ke MQTT
        bool publish_success = false;
        if (mqtt_client != NULL && is_mqtt_connected) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_string, 0, 1, 0);
            if (msg_id >= 0) {
                publish_success = true;
            }
        }

        // 9. Info RAM (Heap)
        uint32_t free_heap_kb = esp_get_free_heap_size() / 1024;
        uint32_t min_heap_kb  = esp_get_minimum_free_heap_size() / 1024;

        // =======================================================
        // TAMPILAN OUTPUT TERMINAL YANG BERSIH & RAPI
        // =======================================================
        printf("\n=======================================================\n");
        printf(" [MONITORING TAMBAK - ESP32]                      \n");
        printf("=======================================================\n");
        printf(" Waktu NTP      : %s\n", timestamp);
        printf(" WiFi Status    : %s (IP: %s)\n", is_wifi_connected ? "TERHUBUNG" : "TERPUTUS", ip_address_str);
        printf(" MQTT Status    : %s (%s)\n", is_mqtt_connected ? "TERHUBUNG" : "TERPUTUS", MQTT_BROKER);
        printf(" Sisa RAM (Heap): %lu KB (Min: %lu KB)\n", (unsigned long)free_heap_kb, (unsigned long)min_heap_kb);
        printf("-------------------------------------------------------\n");
        printf(" HASIL PEMBACAAN SENSOR:\n");
        printf("  - Suhu Air (DS18B20) : %.2f °C\n", suhu_air);
        printf("  - Suhu Udara (DHT)   : %.2f °C\n", suhu_lingkungan);
        printf("  - Kelembaban (DHT)   : %.2f %%\n", kelembaban);
        printf("  - Level Air (JSN)    : %.2f cm\n", jsn_val);
        printf("  - Kualitas Air (TDS) : %.2f ppm\n", tds_val);
        printf("  - Nitrat (Sensor)    : %.2f mg/L\n", nitrat_val);
        printf("  - DO / Oksigen       : %.2f mg/L\n", do_val);
        printf("-------------------------------------------------------\n");
        printf(" MQTT Payload   : %s\n", json_string);
        printf(" Status Publish : %s\n", publish_success ? "BERHASIL DIKIRIM [OK]" : "GAGAL / MENUNGGU KONEKSI");
        printf("=======================================================\n");

        // Interval 10 Detik
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ==========================================
// MAIN APP ENTRY
// ==========================================
void app_main(void) {
    // Nonaktifkan hardware brownout detector untuk mencegah restart akibat drop tegangan sesaat dari USB
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Redam semua log internal yang berisik, hanya tampilkan Warning & Error
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("TAMBAK_ESP32", ESP_LOG_INFO);

    printf("\n>>> MEMULAI SISTEM MONITORING TAMBAK ESP32 <<<\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    hardware_init();
    wifi_init_sta();

    // Tunggu WiFi terhubung maksimal 10 detik di awal
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    initialize_sntp();

    // Inisialisasi MQTT Client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // Jalankan Task Sensor
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}