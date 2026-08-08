#ifndef LCD_TFT_H
#define LCD_TFT_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

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
// DEFINISI WARNA RGB565
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
#define COLOR_HEADER   0x0015 // Dark Blue

// ==========================================
// FUNGSI MODULAR LCD TFT
// ==========================================
void lcd_tft_init(void);
void lcd_tft_draw_layout(void);
void lcd_tft_update(float suhu_air, float suhu_lingkungan, float humidity, float tds_val, float jsn_val, float do_val);

#ifdef __cplusplus
}
#endif

#endif // LCD_TFT_H
