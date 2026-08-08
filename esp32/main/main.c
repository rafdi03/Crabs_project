/*
 * ESP-IDF Main C File: monitoring_tambak
 * Integrasi Lengkap: DS18B20, DHT22, TDS, JSN-SR04T, MQTT & LCD TFT ILI9342 / ILI9341 SPI
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
#include "driver/spi_master.h"
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
// DEFINISI PIN LCD TFT ILI9342 / ILI9341 (SPI)
// ==========================================
#define TFT_CS_PIN     GPIO_NUM_5
#define TFT_DC_PIN     GPIO_NUM_17
#define TFT_RST_PIN    GPIO_NUM_16
#define TFT_MOSI_PIN   GPIO_NUM_23
#define TFT_SCLK_PIN   GPIO_NUM_18

#define TFT_WIDTH      320
#define TFT_HEIGHT     240

// ==========================================
// DEFINISI PIN SENSOR
// ==========================================
#define DS18B20_PIN    GPIO_NUM_33   // Suhu Air
#define DHT_PIN        GPIO_NUM_32   // Suhu & Kelembaban Lingkungan
#define DHT_TYPE       DHT_TYPE_DHT22 // Ubah ke DHT_TYPE_DHT11 jika pakai DHT11

#define TDS_ADC_CHANNEL ADC_CHANNEL_6 // GPIO 34 (Sensor_VN / Analog In)
#define TRIG_PIN       GPIO_NUM_14   // JSN-SR04T Trig
#define ECHO_PIN       GPIO_NUM_27   // JSN-SR04T Echo

// ==========================================
// WARNA RGB565 (ILI9341 / Adafruit GFX)
// ==========================================
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_CYAN     0x07FF
#define COLOR_MAGENTA  0xF81F
#define COLOR_YELLOW   0xFFE0
#define COLOR_ORANGE   0xFD20
#define COLOR_HEADER   0x0015 // Dark Blue Header

// Kalibrasi ADC TDS / Modul Hujan
const int ADC_AIR_MURNI  = 3800; 
const int PPM_AIR_MURNI  = 10;   
const int ADC_AIR_KERAN  = 1500; 
const int PPM_AIR_KERAN  = 150;  

#define SCOUNT 20
static int analogBuffer[SCOUNT];
static int analogBufferIndex = 0;

// Variabel Global
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static bool is_wifi_connected = false;
static bool is_mqtt_connected = false;
static char ip_address_str[32] = "Belum Terhubung";

static adc_oneshot_unit_handle_t tds_adc_handle = NULL;
static spi_device_handle_t tft_spi = NULL;
esp_mqtt_client_handle_t mqtt_client = NULL;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// FONT 5x7 ASCII TABLE (ADAFRUIT GFX COMPATIBLE)
// ==========================================
static const uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space (32)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0 (48)
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x00, 0x41, 0x22, 0x14, 0x08, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A (65)
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x07, 0x08, 0x70, 0x08, 0x07, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x7F, 0x41, 0x41, 0x00, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // backslash
    0x00, 0x41, 0x41, 0x7F, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a (97)
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x0C, 0x52, 0x52, 0x52, 0x3E, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x7F, 0x10, 0x28, 0x44, 0x00, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08  // ~
};

// ==========================================
// DRIVER SPI LCD ILI9341 / ILI9342
// ==========================================
static void tft_send_cmd(uint8_t cmd) {
    gpio_set_level(TFT_DC_PIN, 0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(tft_spi, &t);
}

static void tft_send_data(const uint8_t *data, int len) {
    if (len <= 0) return;
    gpio_set_level(TFT_DC_PIN, 1);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(tft_spi, &t);
}

static void tft_send_data_byte(uint8_t data) {
    tft_send_data(&data, 1);
}

static void tft_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    tft_send_cmd(0x2A); // CASET
    uint8_t data_x[] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF), (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    tft_send_data(data_x, 4);

    tft_send_cmd(0x2B); // PASET
    uint8_t data_y[] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };
    tft_send_data(data_y, 4);

    tft_send_cmd(0x2C); // RAMWR
}

static void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    tft_set_addr_window(x, y, x + w - 1, y + h - 1);

    int total_pixels = w * h;
    int chunk_size = 256;
    if (chunk_size > total_pixels) chunk_size = total_pixels;

    uint16_t *buf = malloc(chunk_size * sizeof(uint16_t));
    if (!buf) return;
    for (int i = 0; i < chunk_size; i++) buf[i] = (color >> 8) | (color << 8);

    gpio_set_level(TFT_DC_PIN, 1);
    while (total_pixels > 0) {
        int current_chunk = (total_pixels > chunk_size) ? chunk_size : total_pixels;
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = current_chunk * 16;
        t.tx_buffer = buf;
        spi_device_polling_transmit(tft_spi, &t);
        total_pixels -= current_chunk;
    }
    free(buf);
}

static void tft_draw_fast_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft_fill_rect(x, y, w, 1, color);
}

static void tft_draw_fast_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
    tft_fill_rect(x, y, 1, h, color);
}

static void tft_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    tft_draw_fast_hline(x + r, y, w - 2 * r, color);
    tft_draw_fast_hline(x + r, y + h - 1, w - 2 * r, color);
    tft_draw_fast_vline(x, y + r, h - 2 * r, color);
    tft_draw_fast_vline(x + w - 1, y + r, h - 2 * r, color);

    // Corner lines
    for (int i = 0; i < r; i++) {
        tft_fill_rect(x + r - i, y + i, 1, 1, color);
        tft_fill_rect(x + w - 1 - r + i, y + i, 1, 1, color);
        tft_fill_rect(x + r - i, y + h - 1 - i, 1, 1, color);
        tft_fill_rect(x + w - 1 - r + i, y + h - 1 - i, 1, 1, color);
    }
}

static void tft_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (c < 32 || c > 126) c = ' ';
    int char_idx = c - 32;

    for (int8_t i = 0; i < 5; i++) {
        uint8_t line = font5x7[char_idx * 5 + i];
        for (int8_t j = 0; j < 8; j++) {
            if (line & 0x1) {
                if (size == 1) {
                    tft_fill_rect(x + i, y + j, 1, 1, color);
                } else {
                    tft_fill_rect(x + (i * size), y + (j * size), size, size, color);
                }
            } else if (bg != color && bg != 0) {
                if (size == 1) {
                    tft_fill_rect(x + i, y + j, 1, 1, bg);
                } else {
                    tft_fill_rect(x + (i * size), y + (j * size), size, size, bg);
                }
            }
            line >>= 1;
        }
    }
}

static void tft_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size) {
    int16_t cursor_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 8 * size;
            cursor_x = x;
        } else {
            tft_draw_char(cursor_x, y, *str, color, bg, size);
            cursor_x += 6 * size;
        }
        str++;
    }
}

// Inisialisasi LCD ILI9341 / ILI9342
static void tft_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TFT_DC_PIN) | (1ULL << TFT_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // Reset Hardware LCD
    gpio_set_level(TFT_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TFT_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(TFT_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Setup Bus SPI Master
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = TFT_MOSI_PIN,
        .sclk_io_num = TFT_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10MHz SPI
        .mode = 0,
        .spics_io_num = TFT_CS_PIN,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &tft_spi);

    // Initial sequence ILI9341 / ILI9342
    tft_send_cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_send_cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    tft_send_cmd(0x36); // MADCTL Rotation Mode 2 (0xC8 / 320x240)
    tft_send_data_byte(0xC8);

    tft_send_cmd(0x3A); // PIXFMT 16-bit
    tft_send_data_byte(0x55);

    tft_send_cmd(0x21); // Display Inversion ON (sesuai tft.invertDisplay(true))

    tft_send_cmd(0x29); // DISPON
    vTaskDelay(pdMS_TO_TICKS(50));
}

// Gambar Kerangka Tampilan UI di Layar TFT
static void gambarLayoutUI(void) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);

    // Header 320px
    tft_fill_rect(0, 0, 320, 28, COLOR_HEADER);
    tft_draw_fast_hline(0, 28, 320, COLOR_CYAN);
    tft_draw_string(50, 6, "MONITORING TAMBAK", COLOR_WHITE, COLOR_HEADER, 2);

    // Grid 1: SUHU AIR & DHT (Atas Kiri)
    tft_draw_round_rect(4, 32, 154, 98, 5, COLOR_YELLOW);
    tft_draw_string(10, 38, "SUHU AIR & DHT", COLOR_YELLOW, COLOR_BLACK, 1);

    // Grid 2: TDS (Atas Kanan)
    tft_draw_round_rect(162, 32, 154, 98, 5, COLOR_GREEN);
    tft_draw_string(170, 38, "TDS (ESTIMASI PPM)", COLOR_GREEN, COLOR_BLACK, 1);

    // Grid 3: JARAK JSN (Bawah Kiri)
    tft_draw_round_rect(4, 134, 154, 102, 5, COLOR_CYAN);
    tft_draw_string(10, 140, "KETINGGIAN AIR", COLOR_CYAN, COLOR_BLACK, 1);

    // Grid 4: OKSIGEN / DO (Bawah Kanan)
    tft_draw_round_rect(162, 134, 154, 102, 5, COLOR_MAGENTA);
    tft_draw_string(170, 140, "OKSIGEN (DO)", COLOR_MAGENTA, COLOR_BLACK, 1);
}

// Update Nilai Realtime di Layar TFT
static void update_tft_display(float suhu_air, float suhu_lingkungan, float humidity, float tds_val, float jsn_val, float do_val) {
    char buf[32];

    // A. SUHU AIR & DHT
    tft_fill_rect(8, 52, 146, 74, COLOR_BLACK);
    if (suhu_air <= 0.0f) {
        tft_draw_string(12, 54, "DS : ERR", COLOR_WHITE, COLOR_BLACK, 2);
    } else {
        snprintf(buf, sizeof(buf), "DS :%.1f C", suhu_air);
        tft_draw_string(12, 54, buf, COLOR_WHITE, COLOR_BLACK, 2);
    }

    snprintf(buf, sizeof(buf), "DHT :%.1f C", suhu_lingkungan);
    tft_draw_string(12, 76, buf, COLOR_ORANGE, COLOR_BLACK, 1);
    snprintf(buf, sizeof(buf), "Hum :%.1f %%", humidity);
    tft_draw_string(12, 92, buf, COLOR_ORANGE, COLOR_BLACK, 1);

    // B. TDS
    tft_fill_rect(166, 52, 146, 74, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "%d", (int)tds_val);
    tft_draw_string(170, 65, buf, COLOR_WHITE, COLOR_BLACK, 3);
    tft_draw_string(240, 72, " PPM", COLOR_GREEN, COLOR_BLACK, 2);

    // C. JARAK JSN
    tft_fill_rect(8, 155, 146, 75, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "%.1f", jsn_val);
    tft_draw_string(12, 168, buf, COLOR_WHITE, COLOR_BLACK, 3);
    tft_draw_string(90, 175, " cm", COLOR_CYAN, COLOR_BLACK, 2);

    // D. OKSIGEN DO
    tft_fill_rect(166, 155, 146, 75, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "%.1f", do_val);
    tft_draw_string(170, 168, buf, COLOR_WHITE, COLOR_BLACK, 3);
    tft_draw_string(225, 175, " mg/L", COLOR_MAGENTA, COLOR_BLACK, 2);
}

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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        is_mqtt_connected = true;
    } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        is_mqtt_connected = false;
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
    esp_wifi_set_max_tx_power(50); // Mencegah lonjakan arus berlebih
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
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0x44); // Start conversion

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
// INISIALISASI HARDWARE
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

    // 2. JSN-SR04T GPIO (Trig: 14, Echo: 27)
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

        // 6. Update Tampilan Layar LCD TFT ILI9341 / ILI9342
        update_tft_display(suhu_air, suhu_lingkungan, kelembaban, tds_val, jsn_val, do_val);

        // 7. Ambil Timestamp UTC
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        // 8. Format JSON Payload
        char json_string[256];
        snprintf(json_string, sizeof(json_string),
            "{\"tds\":%.2f,\"jsn\":%.2f,\"nitrat\":%.2f,\"do\":%.2f,\"suhu_air\":%.2f,\"suhu_lingkungan\":%.2f,\"timestamp\":\"%s\"}",
            tds_val, jsn_val, nitrat_val, do_val, suhu_air, suhu_lingkungan, timestamp);

        // 9. Kirim ke MQTT
        bool publish_success = false;
        if (mqtt_client != NULL && is_mqtt_connected) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_string, 0, 1, 0);
            if (msg_id >= 0) {
                publish_success = true;
            }
        }

        // 10. Info RAM & Terminal Output
        uint32_t free_heap_kb = esp_get_free_heap_size() / 1024;
        printf("\n=======================================================\n");
        printf(" [MONITORING TAMBAK - ESP32 & LCD TFT ILI9342]   \n");
        printf("=======================================================\n");
        printf(" Waktu NTP      : %s\n", timestamp);
        printf(" WiFi Status    : %s (IP: %s)\n", is_wifi_connected ? "TERHUBUNG" : "TERPUTUS", ip_address_str);
        printf(" MQTT Status    : %s (%s)\n", is_mqtt_connected ? "TERHUBUNG" : "TERPUTUS", MQTT_BROKER);
        printf(" Sisa RAM (Heap): %lu KB\n", (unsigned long)free_heap_kb);
        printf("-------------------------------------------------------\n");
        printf(" PEMBACAAN SENSOR & LAYAR LCD:\n");
        printf("  - Suhu Air (DS18B20) : %.2f °C\n", suhu_air);
        printf("  - Suhu Udara (DHT)   : %.2f °C\n", suhu_lingkungan);
        printf("  - Kelembaban (DHT)   : %.2f %%\n", kelembaban);
        printf("  - Level Air (JSN)    : %.2f cm\n", jsn_val);
        printf("  - Kualitas Air (TDS) : %.2f PPM (ADC: %d)\n", tds_val, medianADC);
        printf("  - Oksigen Terlarut   : %.2f mg/L\n", do_val);
        printf("-------------------------------------------------------\n");
        printf(" MQTT Payload   : %s\n", json_string);
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

    printf("\n>>> MEMULAI SISTEM MONITORING TAMBAK ESP32 DENGAN LCD TFT <<<\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Inisialisasi Hardware & LCD
    hardware_init();
    tft_init();
    gambarLayoutUI();

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

    // Jalankan Task Sensor & LCD
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}