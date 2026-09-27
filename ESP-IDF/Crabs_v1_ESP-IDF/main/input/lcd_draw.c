/*
 * lcd_draw.c — DMA-safe string render
 *  Created on: 26 Sept 2026
 *      Author: Rafdi
 */
#include "lcd_draw.h"
#include "lcd_font.h"
#include "modules_config.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include <string.h>
#include <ctype.h>

static esp_lcd_panel_handle_t   s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io   = NULL;
static uint16_t s_fg   = LCD_WHITE;
static uint16_t s_bg   = LCD_BLACK;
static uint8_t  s_size = 1;

/* Buffer fill (multi-row chunk) */
#define FILL_CHUNK_ROWS 16
static uint16_t s_chunk_buf[LCD_H_RES * FILL_CHUNK_ROWS];

/* Buffer string penuh (max size 4) */
#define STR_MAX_W  320
#define STR_MAX_H  28
static uint16_t s_str_buf[STR_MAX_W * STR_MAX_H];

/* ============================================================================
 * DMA SYNC — tunggu transfer sebelumnya selesai sebelum tulis buffer.
 * Mengirim NOP (cmd 0x00) yang antri di belakang transfer aktif.
 * Dengan trans_queue_depth=1, panggilan ini BLOCK sampai transfer selesai.
 * ==========================================================================*/
static void lcd_sync_dma(void) {
    if (s_io == NULL) return;
    esp_lcd_panel_io_tx_param(s_io, 0x00, NULL, 0);
}

void lcd_draw_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io) {
    s_panel = panel;
    s_io    = io;
}

void lcd_draw_set_color(uint16_t fg, uint16_t bg) {
    s_fg = fg;
    s_bg = bg;
}

void lcd_draw_set_size(uint8_t size) {
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    s_size = size;
}

/* ============================================================================
 * fill_rect — pakai sync_dma sebelum menulis s_chunk_buf
 * ==========================================================================*/
void lcd_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (s_panel == NULL || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    static uint16_t s_last_color = 0xDEAD;
    static bool     s_ready      = false;

    if (!s_ready || s_last_color != color) {
        lcd_sync_dma();                    /* tunggu transfer lama sebelum timpa */
        for (int i = 0; i < (LCD_H_RES * FILL_CHUNK_ROWS); i++) {
            s_chunk_buf[i] = color;
        }
        s_last_color = color;
        s_ready = true;
    }

    int16_t rem_h = h;
    int16_t cur_y = y;
    while (rem_h > 0) {
        int16_t batch = (rem_h > FILL_CHUNK_ROWS) ? FILL_CHUNK_ROWS : rem_h;
        esp_lcd_panel_draw_bitmap(s_panel, x, cur_y, x + w, cur_y + batch, s_chunk_buf);
        cur_y += batch;
        rem_h -= batch;
    }
}

void lcd_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    lcd_draw_fill_rect(x, y, w, 1, color);
}

void lcd_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
    lcd_draw_fill_rect(x, y, 1, h, color);
}

void lcd_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w < 2 || h < 2) return;
    lcd_draw_hline(x, y, w, color);
    lcd_draw_hline(x, y + h - 1, w, color);
    lcd_draw_vline(x, y, h, color);
    lcd_draw_vline(x + w - 1, y, h, color);
}

/* ============================================================================
 * String render — sync DMA, isi buffer, kirim sekali. Tidak ada race.
 * ==========================================================================*/
void lcd_draw_string(int16_t x, int16_t y, const char *str) {
    if (str == NULL || s_panel == NULL) return;

    int sz = s_size;
    int cw = 5 * sz;
    int ch = 7 * sz;
    int cs = 6 * sz;

    int len = (int)strlen(str);
    if (len == 0) return;

    int str_w = (len - 1) * cs + cw;
    int str_h = ch;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + str_w > LCD_H_RES) str_w = LCD_H_RES - x;
    if (y + str_h > LCD_V_RES) str_h = LCD_V_RES - y;
    if (str_w <= 0 || str_h <= 0) return;
    if (str_w > STR_MAX_W || str_h > STR_MAX_H) return;

    /* --- KRITIKAL: tunggu DMA transfer sebelumnya selesai --- */
    lcd_sync_dma();

    /* 1. Isi background */
    int total = str_w * str_h;
    for (int i = 0; i < total; i++) {
        s_str_buf[i] = s_bg;
    }

    /* 2. Render glyph ke buffer (hanya pixel ON yang ditulis) */
    int cursor = 0;
    for (int i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 0x20 || c > 0x5F) c = '?';

        const uint8_t *glyph = s_font5x7[c - 0x20];

        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1 << row))) continue;
                for (int dy = 0; dy < sz; dy++) {
                    int py = row * sz + dy;
                    if (py >= str_h) continue;
                    for (int dx = 0; dx < sz; dx++) {
                        int px = cursor + col * sz + dx;
                        if (px >= str_w) continue;
                        s_str_buf[py * str_w + px] = s_fg;
                    }
                }
            }
        }
        cursor += cs;
    }

    /* 3. Kirim SATU transfer per string */
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + str_w, y + str_h, s_str_buf);
}

void lcd_draw_string_bg(int16_t x, int16_t y, const char *str,
                        uint16_t fg, uint16_t bg) {
    uint16_t sf = s_fg, sb = s_bg;
    s_fg = fg; s_bg = bg;
    lcd_draw_string(x, y, str);
    s_fg = sf; s_bg = sb;
}

void lcd_draw_badge(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg) {
    if (text == NULL) return;
    int16_t text_w = strlen(text) * 6 * s_size;
    int16_t pw = 4, ph = 2;
    int16_t bw = text_w + pw * 2;
    int16_t bh = (7 * s_size) + ph * 2;
    lcd_draw_fill_rect(x, y, bw, bh, bg);
    lcd_draw_rect(x, y, bw, bh, fg);
    lcd_draw_string_bg(x + pw, y + ph, text, fg, bg);
}

int16_t lcd_draw_string_width(const char *str) {
    if (str == NULL) return 0;
    return strlen(str) * 6 * s_size - s_size;
}

void lcd_draw_fill_circle_small(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    lcd_draw_fill_rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, color);
}