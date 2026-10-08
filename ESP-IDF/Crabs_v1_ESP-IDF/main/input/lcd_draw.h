/*
 * lcd_draw.h
 *
 *  Created on: 26 Sept 2026
 *      Author: Rafdi
 *
 * HIGH-CONTRAST THEME
 *   - Background screen : hitam
 *   - Card              : PUTIH + border hitam (kontras maksimal)
 *   - Nilai sensor      : hitam besar
 *   - Label card        : versi gelap (terbaca di atas putih)
 *   - Header & status   : putih/cyan di atas hitam
 */

#ifndef MAIN_INCLUDE_LCD_DRAW_H_
#define MAIN_INCLUDE_LCD_DRAW_H_

#include <stdint.h>
#include "esp_lcd_panel_ops.h"

/* ============================================================
 * BASE
 * ============================================================ */
#define LCD_BLACK        0x0000
#define LCD_WHITE        0xFFFF
#define LCD_DARK_BG      0x0000

/* ============================================================
 * CARD — putih dengan border hitam
 * ============================================================ */
#define LCD_CARD_BG      0xFFFF   /* PUTIH */
#define LCD_CARD_BORDER  0x0000   /* HITAM */

/* ============================================================
 * TEXT DI DALAM CARD (di atas putih)
 * ============================================================ */
#define LCD_VALUE_COLOR  0x0000   /* HITAM — angka besar */
#define LCD_UNIT_COLOR   0x4208   /* abu gelap — satuan */

/* Label judul card — versi gelap supaya kebaca di atas putih */
#define LCD_LABEL_ORANGE 0xC300
#define LCD_LABEL_AMBER  0x8300
#define LCD_LABEL_BLUE   0x0010
#define LCD_LABEL_GREEN  0x0400
#define LCD_LABEL_NAVY   0x0008

/* ============================================================
 * ACCENTS (header / status bar / heartbeat)
 * ============================================================ */
#define LCD_CYAN         0x07FF
#define LCD_AQUA         0x3DDF
#define LCD_GREEN        0x07E0
#define LCD_MINT         0x3FE0
#define LCD_YELLOW       0xFFE0
#define LCD_ORANGE       0xFD20
#define LCD_RED          0xF800
#define LCD_MUTED        0x7BEF
#define LCD_DARK_GRAY    0x3186
#define LCD_BLUE         0x001F

/* ============================================================
 * API
 * ============================================================ */
void lcd_draw_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io);
void lcd_draw_set_color(uint16_t fg, uint16_t bg);
void lcd_draw_set_size(uint8_t size);
void lcd_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void lcd_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void lcd_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void lcd_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void lcd_draw_string(int16_t x, int16_t y, const char *str);
void lcd_draw_string_bg(int16_t x, int16_t y, const char *str, uint16_t fg, uint16_t bg);
void lcd_draw_badge(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg);
void lcd_draw_fill_circle_small(int16_t cx, int16_t cy, int16_t r, uint16_t color);
int16_t lcd_draw_string_width(const char *str);

#endif