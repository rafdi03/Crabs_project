/*
 * ESP-IDF Main C File: monitoring_tambak
 * 
 * FOKUS: DS18B20 (Pin 33) & DHT (Pin 32) Bekerja Bersamaan.
 * Sensor lainnya diset 0.0 karena belum terpasang.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "esp_rom_sys.h"

// Library DHT
#include "dht.h"

static const char *TAG = "TAMBAK_ESP32";

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
#define DS18B20_PIN GPIO_NUM_33
#define DHT_PIN     GPIO_NUM_32
#define DHT_TYPE    DHT_TYPE_DHT22  // Ubah ke DHT_TYPE_DHT11 jika Anda pakai DHT11

// Variabel Global
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

esp_mqtt_client_handle_t mqtt_client = NULL;
static int wifi_retry_count = 0;
static bool ds18b20_present = false;

// Kunci Interupsi untuk Bit-Banging DS18B20
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// WIFI & EVENT HANDLER
// ==========================================
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "WiFi terputus, alasan=%d. Mencoba koneksi ulang...", disconnected->reason);
        esp_wifi_connect();
        wifi_retry_count++;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Terhubung! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

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
}

// ==========================================
// SNTP (WAKTU INTERNET)
// ==========================================
void initialize_sntp(void) {
    ESP_LOGI(TAG, "Inisialisasi SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Menunggu sinkronisasi waktu... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    
    setenv("TZ", "UTC0", 1);
    tzset();
}

// ==========================================
// BITBANGING DS18B20
// ==========================================
static bool ds18b20_reset(void) {
    taskENTER_CRITICAL(&mux);
    gpio_set_level(DS18B20_PIN, 0); // Tarik LOW
    esp_rom_delay_us(480);
    gpio_set_level(DS18B20_PIN, 1); // Lepas HIGH
    esp_rom_delay_us(70);
    int presence = gpio_get_level(DS18B20_PIN);
    esp_rom_delay_us(410);
    taskEXIT_CRITICAL(&mux);
    return presence == 0;
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

static bool ds18b20_start_conversion(void) {
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0x44); // Convert T
    return true;
}

static bool ds18b20_read_temperature(float *temperature) {
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0xBE); // Read Scratchpad

    uint8_t temp_lsb = ds18b20_read_byte();
    uint8_t temp_msb = ds18b20_read_byte();
    for (int i = 0; i < 7; i++) {
        ds18b20_read_byte(); // Habiskan sisa 7 byte
    }

    int16_t raw = (int16_t)((temp_msb << 8) | temp_lsb);
    *temperature = raw * 0.0625f;
    return true;
}

void hardware_init() {
    ESP_LOGI(TAG, "Inisialisasi Hardware...");

    // Setup pin DS18B20 sebagai Open-Drain
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DS18B20_PIN,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(DS18B20_PIN, 1); // Set HIGH (Idle)

    // Cek awal DS18B20
    ds18b20_present = ds18b20_reset();
    if (ds18b20_present) {
        ESP_LOGI(TAG, "DS18B20 terdeteksi di pin GPIO%d", DS18B20_PIN);
    } else {
        ESP_LOGE(TAG, "DS18B20 tidak terdeteksi. Pastikan kabel dan resistor pull-up 4.7k terpasang.");
    }
}

// ==========================================
// TUGAS BACA SENSOR & PUBLISH MQTT
// ==========================================
void sensor_task(void *pvParameters) {
    while (1) {
        // --- 1. Membaca Suhu Air (DS18B20) ---
        ds18b20_present = ds18b20_reset();
        float suhu_air = 0.0f;
        if (ds18b20_present) {
            if (ds18b20_start_conversion()) {
                vTaskDelay(800 / portTICK_PERIOD_MS); // DS18B20 butuh ~750ms
                if (!ds18b20_read_temperature(&suhu_air)) {
                    ESP_LOGE(TAG, "Gagal membaca suhu dari DS18B20.");
                    suhu_air = 0.0f;
                } else {
                    ESP_LOGI(TAG, "Suhu Air Terbaca: %.2f Celsius", suhu_air);
                }
            } else {
                ESP_LOGE(TAG, "Gagal memulai konversi DS18B20.");
            }
        }

        // --- 2. Membaca Suhu Lingkungan (DHT) ---
        float suhu_lingkungan = 0.0f;
        float humidity = 0.0f;
        esp_err_t res = dht_read_float_data(DHT_TYPE, DHT_PIN, &humidity, &suhu_lingkungan);

        if (res == ESP_OK) {
            ESP_LOGI(TAG, "Suhu Lingkungan: %.2f Celsius | Kelembaban: %.2f %%", suhu_lingkungan, humidity);
        } else {
            ESP_LOGE(TAG, "Gagal membaca suhu dari DHT.");
            suhu_lingkungan = 0.0f;
        }

        // --- Sensor Lain Di-Set 0.0 ---
        float tds_val = 0.0f;
        float jsn_val = 0.0f;
        float nitrat_val = 0.0f;
        float do_val = 0.0f;

        // --- Generate Timestamp ---
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        // --- Build JSON Payload ---
        char json_string[256];
        int len = snprintf(json_string, sizeof(json_string),
            "{\"tds\":%.2f,\"jsn\":%.2f,\"nitrat\":%.2f,\"do\":%.2f,\"suhu_air\":%.2f,\"suhu_lingkungan\":%.2f,\"timestamp\":\"%s\"}",
            tds_val, jsn_val, nitrat_val, do_val, suhu_air, suhu_lingkungan, timestamp);

        if (len >= 0 && len < sizeof(json_string)) {
            ESP_LOGI(TAG, "Payload: %s", json_string);
            if (mqtt_client != NULL) {
                int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_string, 0, 1, 0);
            }
        }

        // Tunggu 10 detik
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// MAIN APP
// ==========================================
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 TAMBAK (DS18B20 & DHT) START ===");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    hardware_init();
    wifi_init_sta();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    initialize_sntp();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}