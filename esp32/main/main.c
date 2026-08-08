/*
 * ESP-IDF Main C File: monitoring_tambak
 * Integrasi Lengkap: DS18B20, DHT22, TDS, JSN-SR04T, 5-Channel Relay (FreeRTOS), MQTT & LCD TFT
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

// Modul Modular DHT, LCD TFT & FreeRTOS Relay Controller
#include "dht.h"
#include "lcd_tft.h"
#include "relay_ctrl.h"

static const char *TAG = "TAMBAK_ESP32";

// ==========================================
// KONFIGURASI WIFI & MQTT
// ==========================================
#define WIFI_SSID         "Bayu"
#define WIFI_PASS         "12345678"
#define MQTT_BROKER       "mqtt://broker.emqx.io:1883"
#define DEVICE_ID         "ESP32-001"

#define MQTT_TOPIC_SENSOR "tambak/ESP32-001/sensor"
#define MQTT_TOPIC_RELAY_SUB "tambak/ESP32-001/relay/#"
#define MQTT_TOPIC_RELAY_STATE "tambak/ESP32-001/relay/state"

// ==========================================
// DEFINISI PIN SENSOR
// ==========================================
#define DS18B20_PIN       GPIO_NUM_33   // Suhu Air
#define DHT_PIN           GPIO_NUM_32   // Suhu & Kelembaban Lingkungan
#define DHT_TYPE          DHT_TYPE_DHT22 // Ubah ke DHT_TYPE_DHT11 jika pakai DHT11

#define TDS_ADC_CHANNEL   ADC_CHANNEL_6 // GPIO 34 (Sensor_VN / Analog In)
#define TRIG_PIN          GPIO_NUM_15   // JSN-SR04T Trig (Dipindah ke D15 agar D14 bebas untuk Relay 5)
#define ECHO_PIN          GPIO_NUM_27   // JSN-SR04T Echo

// Kalibrasi ADC TDS / Modul Hujan
const int ADC_AIR_MURNI  = 3800; 
const int PPM_AIR_MURNI  = 10;   
const int ADC_AIR_KERAN  = 1500; 
const int PPM_AIR_KERAN  = 150;  

#define SCOUNT 20
static int analogBuffer[SCOUNT];

// Variabel Global Status
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static bool is_wifi_connected = false;
static bool is_mqtt_connected = false;
static char ip_address_str[32] = "Belum Terhubung";

static adc_oneshot_unit_handle_t tds_adc_handle = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// KALIBRASI & FILTER MEDIAN TDS SENSOR
// ==========================================
static int getMedianNum(int bArray[], int iFilterLen) {
    int bTab[SCOUNT];
    for (int i = 0; i < iFilterLen; i++) bTab[i] = bArray[i];
    int bTemp;
    for (int j = 0; j < iFilterLen - 1; j++) {
        for (int i = 0; i < iFilterLen - j - 1; i++) {
            if (bTab[i] > bTab[i + 1]) {
                bTemp = bTab[i];
                bTab[i] = bTab[i + 1];
                bTab[i + 1] = bTemp;
            }
        }
    }
    if ((iFilterLen & 1) > 0) bTemp = bTab[(iFilterLen - 1) / 2];
    else bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
    return bTemp;
}

static int hitungPPMdarimodulHujan(int adcRaw) {
    long ppm = (long)(adcRaw - ADC_AIR_MURNI) * (PPM_AIR_KERAN - PPM_AIR_MURNI) / (ADC_AIR_KERAN - ADC_AIR_MURNI) + PPM_AIR_MURNI;
    if (ppm < 0) ppm = 0;
    return (int)ppm;
}

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
// MQTT EVENT HANDLER & RELAY RECV COMMAND
// ==========================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        is_mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT Terhubung ke broker!");
        // Berlangganan (Subscribe) ke topik kontrol relay
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_RELAY_SUB, 1);
        
        // Kirim status awal relay ke broker
        char relay_json[128];
        relay_get_json_status(relay_json, sizeof(relay_json));
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_RELAY_STATE, relay_json, 0, 1, 0);

    } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        is_mqtt_connected = false;
    } else if (event->event_id == MQTT_EVENT_DATA) {
        // Menerima perintah kontrol relay secara asinkron (FreeRTOS)
        relay_parse_mqtt_command(event->topic, event->topic_len, event->data, event->data_len);
        
        // Kirim feedback state relay terbaru
        char relay_json[128];
        relay_get_json_status(relay_json, sizeof(relay_json));
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_RELAY_STATE, relay_json, 0, 1, 0);
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
    esp_wifi_set_max_tx_power(50);
}

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
    ds18b20_write_byte(0xCC);
    ds18b20_write_byte(0x44);

    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC);
    ds18b20_write_byte(0xBE);

    uint8_t temp_lsb = ds18b20_read_byte();
    uint8_t temp_msb = ds18b20_read_byte();
    for (int i = 0; i < 7; i++) ds18b20_read_byte();

    int16_t raw = (int16_t)((temp_msb << 8) | temp_lsb);
    *temperature = raw * 0.0625f;
    return (*temperature > -55.0f && *temperature < 125.0f);
}

// ==========================================
// BACA JSN-SR04T (LEVEL / JARAK AIR)
// ==========================================
static float bacaJarakJSN(void) {
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 0) {
        if (esp_timer_get_time() - start_time > 30000) {
            return 0.0f;
        }
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) {
            return 0.0f;
        }
    }
    int64_t echo_end = esp_timer_get_time();

    int64_t duration_us = echo_end - echo_start;
    float distance_cm = (duration_us * 0.0343f) / 2.0f;
    return (distance_cm > 0.0f && distance_cm < 600.0f) ? distance_cm : 0.0f;
}

// ==========================================
// INISIALISASI HARDWARE SENSOR
// ==========================================
void hardware_init(void) {
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

    // 2. JSN-SR04T GPIO (Trig: D15, Echo: D27)
    gpio_config_t io_conf_jsn = {
        .pin_bit_mask = (1ULL << TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_jsn);
    gpio_set_level(TRIG_PIN, 0);

    gpio_config_t io_conf_echo = {
        .pin_bit_mask = (1ULL << ECHO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_echo);

    // 3. ADC untuk TDS Sensor (GPIO 34 / ADC1_CHANNEL_6)
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
// TUGAS BACA SENSOR & UPDATE LCD & PUBLISH MQTT
// ==========================================
void sensor_task(void *pvParameters) {
    while (1) {
        // Sample ADC TDS (20 samples for median)
        for (int i = 0; i < SCOUNT; i++) {
            int adc_val = 0;
            if (tds_adc_handle != NULL) {
                adc_oneshot_read(tds_adc_handle, TDS_ADC_CHANNEL, &adc_val);
            }
            analogBuffer[i] = adc_val;
            vTaskDelay(pdMS_TO_TICKS(15));
        }

        // 1. Baca Suhu Air (DS18B20)
        float suhu_air = 0.0f;
        if (!ds18b20_read_temperature(&suhu_air)) {
            suhu_air = 0.0f;
        }

        // 2. Baca DHT22/DHT11
        float suhu_lingkungan = 0.0f;
        float kelembaban = 0.0f;
        esp_err_t res = dht_read_float_data(DHT_TYPE, DHT_PIN, &kelembaban, &suhu_lingkungan);
        if (res != ESP_OK) {
            suhu_lingkungan = 0.0f;
            kelembaban = 0.0f;
        }

        // 3. Baca JSN-SR04T (Jarak Air)
        float jsn_val = bacaJarakJSN();

        // 4. Baca TDS dengan Median Filter & Kalibrasi Modul Hujan
        int medianADC = getMedianNum(analogBuffer, SCOUNT);
        float tds_val = (float)hitungPPMdarimodulHujan(medianADC);

        // 5. Sensor Lain
        float nitrat_val = 0.0f;
        float do_val = 6.8f; // Dummy / Dissolved Oxygen

        // 6. Update Layar LCD TFT ILI9342 (Modular)
        lcd_tft_update(suhu_air, suhu_lingkungan, kelembaban, tds_val, jsn_val, do_val);

        // 7. Ambil Timestamp UTC
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        // 8. Format JSON Payload Sensor
        char json_string[256];
        snprintf(json_string, sizeof(json_string),
            "{\"tds\":%.2f,\"jsn\":%.2f,\"nitrat\":%.2f,\"do\":%.2f,\"suhu_air\":%.2f,\"suhu_lingkungan\":%.2f,\"timestamp\":\"%s\"}",
            tds_val, jsn_val, nitrat_val, do_val, suhu_air, suhu_lingkungan, timestamp);

        // 9. Kirim ke MQTT
        bool publish_success = false;
        if (mqtt_client != NULL && is_mqtt_connected) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_SENSOR, json_string, 0, 1, 0);
            if (msg_id >= 0) {
                publish_success = true;
            }
        }

        // 10. Info RAM & Status 5 Relay
        uint32_t free_heap_kb = esp_get_free_heap_size() / 1024;
        printf("\n=======================================================\n");
        printf(" [MONITORING TAMBAK - ESP32 & 5 RELAY RTOS & LCD]      \n");
        printf("=======================================================\n");
        printf(" Waktu NTP      : %s\n", timestamp);
        printf(" WiFi Status    : %s (IP: %s)\n", is_wifi_connected ? "TERHUBUNG" : "TERPUTUS", ip_address_str);
        printf(" MQTT Status    : %s (%s)\n", is_mqtt_connected ? "TERHUBUNG" : "TERPUTUS", MQTT_BROKER);
        printf(" Sisa RAM (Heap): %lu KB\n", (unsigned long)free_heap_kb);
        printf("-------------------------------------------------------\n");
        printf(" STATUS 5 RELAY (FreeRTOS):\n");
        printf("  - Relay 1 (D25) : %s\n", relay_get_state(1) ? "ON  [AKTIF]" : "OFF [MATI]");
        printf("  - Relay 2 (D16) : %s\n", relay_get_state(2) ? "ON  [AKTIF]" : "OFF [MATI]");
        printf("  - Relay 3 (D17) : %s\n", relay_get_state(3) ? "ON  [AKTIF]" : "OFF [MATI]");
        printf("  - Relay 4 (D13) : %s\n", relay_get_state(4) ? "ON  [AKTIF]" : "OFF [MATI]");
        printf("  - Relay 5 (D14) : %s\n", relay_get_state(5) ? "ON  [AKTIF]" : "OFF [MATI]");
        printf("-------------------------------------------------------\n");
        printf(" HASIL SENSOR & UPDATE LCD:\n");
        printf("  - Suhu Air (DS18B20) : %.2f °C\n", suhu_air);
        printf("  - Suhu Udara (DHT)   : %.2f °C\n", suhu_lingkungan);
        printf("  - Kelembaban (DHT)   : %.2f %%\n", kelembaban);
        printf("  - Level Air (JSN)    : %.2f cm\n", jsn_val);
        printf("  - Kualitas Air (TDS) : %.2f PPM (ADC: %d)\n", tds_val, medianADC);
        printf("  - Oksigen Terlarut   : %.2f mg/L\n", do_val);
        printf("-------------------------------------------------------\n");
        printf(" MQTT Sensor    : %s\n", json_string);
        printf(" Status Publish : %s\n", publish_success ? "BERHASIL [OK]" : "GAGAL / MENUNGGU KONEKSI");
        printf("=======================================================\n");

        // Interval 10 Detik
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ==========================================
// MAIN APP ENTRY
// ==========================================
void app_main(void) {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("TAMBAK_ESP32", ESP_LOG_INFO);
    esp_log_level_set("RELAY_CTRL", ESP_LOG_INFO);

    printf("\n>>> MEMULAI MONITORING TAMBAK ESP32 (RTOS MULTI-TASKING) <<<\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Inisialisasi Hardware, Relay Task (FreeRTOS) & LCD TFT
    relay_ctrl_init();
    hardware_init();
    lcd_tft_init();
    lcd_tft_draw_layout();

    wifi_init_sta();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    initialize_sntp();

    // Inisialisasi MQTT Client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // Jalankan Task Sensor & Update LCD (Priority 5)
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}