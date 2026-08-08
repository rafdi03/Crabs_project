/*
 * lcd_tft.c - Modular LCD TFT ILI9341 / ILI9342 SPI Driver & UI Layout
 */

#include "lcd_tft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "LCD_TFT";
static spi_device_handle_t tft_spi = NULL;

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
// DRIVER SPI LOW-LEVEL
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

// ==========================================
// FUNGSI PUBLIK MODULAR LCD
// ==========================================
void lcd_tft_init(void) {
    ESP_LOGI(TAG, "Inisialisasi LCD TFT ILI9342 SPI...");

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

    // Initial commands ILI9341 / ILI9342
    tft_send_cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_send_cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    tft_send_cmd(0x36); // MADCTL Rotation Mode 2 (0xC8 / 320x240)
    tft_send_data_byte(0xC8);

    tft_send_cmd(0x3A); // PIXFMT 16-bit
    tft_send_data_byte(0x55);

    tft_send_cmd(0x21); // Display Inversion ON

    tft_send_cmd(0x29); // DISPON
    vTaskDelay(pdMS_TO_TICKS(50));
}

void lcd_tft_draw_layout(void) {
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

void lcd_tft_update(float suhu_air, float suhu_lingkungan, float humidity, float tds_val, float jsn_val, float do_val) {
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
