/*
 * lcd_draw.h
 *
 *  Created on: 26 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_LCD_DRAW_H_
#define MAIN_INCLUDE_LCD_DRAW_H_

#include <stdint.h>
#include "esp_lcd_panel_ops.h"

// Modern Industrial High-Contrast Theme Palette (RGB565)
#define LCD_BLACK        0x0000  // Hitam Pekat
#define LCD_WHITE        0xFFFF  // Putih Bersih
#define LCD_DARK_BG      0x0000  // Background Utama
#define LCD_CARD_BG      0x0841  // Background Kotak Sensor (Dark Slate)
#define LCD_CARD_BORDER  0x2965  // Border Kotak (Slate Gray)
#define LCD_CYAN         0x07FF  // Aksen Cyan/Biru Muda
#define LCD_AQUA         0x3DDF  // Aqua
#define LCD_GREEN        0x07E0  // Hijau Cerah
#define LCD_MINT         0x3FE0  // Mint Green
#define LCD_YELLOW       0xFFE0  // Kuning Cerah
#define LCD_ORANGE       0xFD20  // Amber / Oranye
#define LCD_RED          0xF800  // Merah Bahaya
#define LCD_MUTED        0x7BEF  // Abu-abu Label
#define LCD_DARK_GRAY    0x3186  // Abu-abu Gelap
#define LCD_BLUE         0x001F

void lcd_draw_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io);

// Set warna (fg = teks, bg = latar). Panggil sebelum draw_string.
void lcd_draw_set_color(uint16_t fg, uint16_t bg);

// Ukuran font pengali: 1 = 5x7 px, 2 = 10x14, 3 = 15x21, dst.
void lcd_draw_set_size(uint8_t size);

// Fill area dengan warna solid (menggunakan buffer chunk cepat)
void lcd_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// Kotak border (garis tepi saja)
void lcd_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// Garis horizontal dan vertikal
void lcd_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void lcd_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);

// Tulis string. Otomatis advance. Font hanya handle 0x20-0x5F (uppercase).
// Huruf kecil otomatis di-convert ke uppercase.
void lcd_draw_string(int16_t x, int16_t y, const char *str);

// Tulis string dengan background fill (menghapus area sebelumnya)
void lcd_draw_string_bg(int16_t x, int16_t y, const char *str, uint16_t fg, uint16_t bg);

// Badge status kecil dengan background (contoh: [NORMAL], [WIFI])
void lcd_draw_badge(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg);

// Kotak kecil / indikator bulat
void lcd_draw_fill_circle_small(int16_t cx, int16_t cy, int16_t r, uint16_t color);

// Ukuran string dalam pixel, untuk alignment
int16_t lcd_draw_string_width(const char *str);

#endif