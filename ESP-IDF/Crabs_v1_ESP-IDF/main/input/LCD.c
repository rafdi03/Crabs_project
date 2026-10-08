/*
 * LCD.c — Full System Dashboard 320×240 (High-Contrast Theme)
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#include "LCD.h"
#include "COM.h"
#include "esp_timer.h"
#include "esp_lcd_ili9341.h"
#include "TempWater.h"
#include "TDS.h"
#include "JSN-SR04T.h"
#include "DO.h"
#include "DHT22.h"

static const char *TAG = "LCD";
static bool s_ready = false;
static bool s_static_rendered = false;
static esp_lcd_panel_handle_t s_panel = NULL;
static lcd_display_data_t s_data = {0};
static bool s_hb_state = false;

static lcd_sys_status_t s_sys_status = {0};
static bool s_status_rendered = false;

static void lcd_draw_static_dashboard(void);
static void lcd_draw_status_bar(void);

/* ============================================================================
 * Rotasi & init
 * ==========================================================================*/
void lcd_set_rotation(uint8_t rotation) {
    if (s_panel == NULL) return;
    switch (rotation) {
    case 0:
        esp_lcd_panel_swap_xy(s_panel, false);
        esp_lcd_panel_mirror(s_panel, true, true);
        esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y);
        break;
    case 1:
        esp_lcd_panel_swap_xy(s_panel, true);
        esp_lcd_panel_mirror(s_panel, false, true);
        esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_Y, LCD_OFFSET_X);
        break;
    case 2:
        esp_lcd_panel_swap_xy(s_panel, false);
        esp_lcd_panel_mirror(s_panel, false, false);
        esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y);
        break;
    case 3:
        esp_lcd_panel_swap_xy(s_panel, true);
        esp_lcd_panel_mirror(s_panel, true, false);
        esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_Y, LCD_OFFSET_X);
        break;
    }
    s_static_rendered = false;
}

void lcd_force_full_redraw(void) { s_static_rendered = false; }

esp_err_t lcd_init(void) {
    spi_bus_config_t buscfg = {
        .sclk_io_num     = LCD_PIN_SCLK,
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST_SPI, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = LCD_PIN_DC,
        .cs_gpio_num       = LCD_PIN_CS,
        .pclk_hz           = LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = LCD_SPI_MODE,
        .trans_queue_depth = LCD_TRANS_QUEUE_DEPTH,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST_SPI,
                                              &io_config, &io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR));
    lcd_set_rotation(LCD_ROTATION);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    vTaskDelay(pdMS_TO_TICKS(50));

    lcd_draw_init(s_panel, io);
    lcd_draw_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, LCD_BLACK);

    s_ready = true;
    s_static_rendered = false;
    ESP_LOGI(TAG, "LCD ILI9341 320x240 ready (high-contrast theme)");
    return ESP_OK;
}

void lcd_set_display_data(const lcd_display_data_t *d) { if (d) s_data = *d; }

/* ============================================================================
 * Helper — draw value + unit di dalam card (PUTIH)
 * ==========================================================================*/
static void lcd_draw_value_unit(int16_t x, int16_t y,
                                const char *value, const char *unit,
                                uint16_t value_color) {
    lcd_draw_set_size(3);
    lcd_draw_set_color(value_color, LCD_CARD_BG);
    lcd_draw_string(x, y, value);

    int16_t vw = strlen(value) * 6 * 3 - 3;

    lcd_draw_set_size(2);
    lcd_draw_set_color(LCD_UNIT_COLOR, LCD_CARD_BG);
    lcd_draw_string(x + vw + 6, y + 7, unit);
}

static void lcd_draw_waiting(int16_t x, int16_t y) {
    lcd_draw_set_size(2);
    lcd_draw_set_color(LCD_UNIT_COLOR, LCD_CARD_BG);
    lcd_draw_string(x, y + 7, "WAITING...");
}

/* ============================================================================
 * Layout dashboard 320×240 (dipanggil sekali, lalu di-cache)
 * ==========================================================================*/
static void lcd_draw_static_dashboard(void) {
    /* Screen background: HITAM */
    lcd_draw_fill_rect(0, 0, 320, 240, LCD_BLACK);

    /* ---- Header (y 0–22) : hitam + cyan/putih ---- */
    lcd_draw_set_size(2);
    lcd_draw_set_color(LCD_CYAN, LCD_BLACK);
    lcd_draw_string(8, 3, "IOT");
    lcd_draw_set_color(LCD_WHITE, LCD_BLACK);
    lcd_draw_string(150, 3, "TAMBAK MONITOR");
    lcd_draw_hline(0, 22, 320, LCD_CYAN);

    /* ---- Status bar (y 24–40) : background hitam ---- */
    lcd_draw_fill_rect(0, 24, 320, 16, LCD_BLACK);
    lcd_draw_hline(0, 41, 320, LCD_CARD_BORDER);

    /* ---- Card 1: SUHU AIR ---- */
    lcd_draw_fill_rect(4, 42, 154, 68, LCD_CARD_BG);
    lcd_draw_rect(4, 42, 154, 68, LCD_CARD_BORDER);
    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_LABEL_ORANGE, LCD_CARD_BG);
    lcd_draw_string(10, 48, "SUHU AIR (DS18B20)");

    /* ---- Card 2: TDS ---- */
    lcd_draw_fill_rect(162, 42, 154, 68, LCD_CARD_BG);
    lcd_draw_rect(162, 42, 154, 68, LCD_CARD_BORDER);
    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_LABEL_AMBER, LCD_CARD_BG);
    lcd_draw_string(168, 48, "TDS (PPM)");

    /* ---- Card 3: LEVEL AIR ---- */
    lcd_draw_fill_rect(4, 114, 154, 68, LCD_CARD_BG);
    lcd_draw_rect(4, 114, 154, 68, LCD_CARD_BORDER);
    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_LABEL_BLUE, LCD_CARD_BG);
    lcd_draw_string(10, 120, "LEVEL AIR (JSN)");

    /* ---- Card 4: DISSOLVED OXYGEN ---- */
    lcd_draw_fill_rect(162, 114, 154, 68, LCD_CARD_BG);
    lcd_draw_rect(162, 114, 154, 68, LCD_CARD_BORDER);
    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_LABEL_GREEN, LCD_CARD_BG);
    lcd_draw_string(168, 120, "DISSOLVED OXYGEN");

    /* ---- Card 5: UDARA ---- */
    lcd_draw_fill_rect(4, 186, 312, 50, LCD_CARD_BG);
    lcd_draw_rect(4, 186, 312, 50, LCD_CARD_BORDER);
    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_LABEL_NAVY, LCD_CARD_BG);
    lcd_draw_string(10, 191, "UDARA (DHT22)");

    lcd_draw_set_size(1);
    lcd_draw_set_color(LCD_UNIT_COLOR, LCD_CARD_BG);
    lcd_draw_string(10, 205, "SUHU UDARA");
    lcd_draw_string(10, 219, "KELEMBAPAN");

    s_static_rendered = true;
}

/* ============================================================================
 * Update per-sensor
 * ==========================================================================*/
static void lcd_update_suhu_air(float suhu) {
    lcd_draw_fill_rect(8, 66, 148, 42, LCD_CARD_BG);
    char buf[16];
    if (suhu == -999.0f || suhu == -127.0f) {
        lcd_draw_waiting(10, 66);
        return;
    }
    snprintf(buf, sizeof(buf), "%.1f", suhu);
    lcd_draw_value_unit(10, 68, buf, "C", LCD_VALUE_COLOR);
}

static void lcd_update_tds(int ppm) {
    lcd_draw_fill_rect(166, 66, 148, 42, LCD_CARD_BG);
    if (ppm == -999 || ppm < 0) {
        lcd_draw_waiting(168, 66);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", ppm);
    lcd_draw_value_unit(168, 68, buf, "PPM", LCD_VALUE_COLOR);
}

static void lcd_update_level(float jarak) {
    lcd_draw_fill_rect(8, 138, 148, 42, LCD_CARD_BG);
    if (jarak < 0 || jarak == -999.0f) {
        lcd_draw_waiting(10, 138);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", jarak);
    lcd_draw_value_unit(10, 140, buf, "cm", LCD_VALUE_COLOR);
}

static void lcd_update_do(float do_val) {
    lcd_draw_fill_rect(166, 138, 148, 42, LCD_CARD_BG);
    if (do_val == -999.0f) {
        lcd_draw_waiting(168, 138);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", do_val);
    lcd_draw_value_unit(168, 140, buf, "mg/L", LCD_VALUE_COLOR);
}

static void lcd_update_udara(float suhu_udara, float lembap, bool valid) {
    lcd_draw_fill_rect(110, 200, 200, 34, LCD_CARD_BG);

    char buf[24];

    if (!valid || suhu_udara == -999.0f) {
        lcd_draw_set_size(2);
        lcd_draw_set_color(LCD_UNIT_COLOR, LCD_CARD_BG);
        lcd_draw_string(120, 202, "--.-- C");
    } else {
        lcd_draw_set_size(2);
        lcd_draw_set_color(LCD_VALUE_COLOR, LCD_CARD_BG);
        snprintf(buf, sizeof(buf), "%.1f C", suhu_udara);
        lcd_draw_string(120, 202, buf);
    }

    if (!valid || lembap == -999.0f) {
        lcd_draw_set_size(2);
        lcd_draw_set_color(LCD_UNIT_COLOR, LCD_CARD_BG);
        lcd_draw_string(120, 220, "--.- %");
    } else {
        lcd_draw_set_size(2);
        lcd_draw_set_color(LCD_VALUE_COLOR, LCD_CARD_BG);
        snprintf(buf, sizeof(buf), "%.1f %%", lembap);
        lcd_draw_string(120, 220, buf);
    }
}

static void lcd_update_heartbeat(bool state) {
    lcd_draw_fill_circle_small(306, 13, 4,
                                state ? LCD_GREEN : LCD_DARK_GRAY);
}

/* ============================================================================
 * lcd_update
 * ==========================================================================*/
void lcd_update(void) {
    if (!s_ready) return;

    if (!s_static_rendered) {
        lcd_draw_static_dashboard();
    }

    if (!s_status_rendered) {
        lcd_draw_status_bar();
        s_status_rendered = true;
    }

    /* Baca semua sensor */
    float suhu_air = ds18b20_get_temp();
    int   tds      = tds_get_ppm();
    float jarak    = jsn_get_distance_cm();
    float do_val   = do_get_value();

    dht22_data_t dht = {0};
    bool dht_ok = dht22_get_data(&dht);

    /* Update tiap kartu */
    lcd_update_suhu_air(suhu_air);
    lcd_update_tds(tds);
    lcd_update_level(jarak);
    lcd_update_do(do_val);
    lcd_update_udara(dht_ok ? dht.temperature : -999.0f,
                     dht_ok ? dht.humidity    : -999.0f,
                     dht_ok);
}

void lcd_toggle_heartbeat(void) {
    if (!s_ready) return;
    s_hb_state = !s_hb_state;
    lcd_update_heartbeat(s_hb_state);
}

bool lcd_is_ready(void) { return s_ready; }

void lcd_set_sys_status(const lcd_sys_status_t *st) {
    if (st) {
        s_sys_status = *st;
        s_status_rendered = false;
    }
}

static void lcd_draw_status_bar(void) {
    /* Background status bar: HITAM */
    lcd_draw_fill_rect(0, 24, 320, 16, LCD_BLACK);

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    bool tx_recent = (now_ms - s_sys_status.last_tx_ms) < 10000;

    struct {
        int16_t dot_x, text_x;
        const char *label;
        uint16_t color;
    } items[] = {
        { 14,  22, "WIFI", s_sys_status.wifi_connected ? LCD_GREEN : LCD_DARK_GRAY },
        { 94, 102, "GSM",  s_sys_status.gsm_connected  ? LCD_GREEN : LCD_DARK_GRAY },
        {174, 182, "MQTT", s_sys_status.mqtt_connected ? LCD_GREEN : LCD_DARK_GRAY },
        {254, 262, "TX",   tx_recent                   ? LCD_GREEN : LCD_DARK_GRAY },
    };

    for (int i = 0; i < 4; i++) {
        lcd_draw_fill_circle_small(items[i].dot_x, 32, 3, items[i].color);
        lcd_draw_set_size(1);
        lcd_draw_set_color(items[i].color, LCD_BLACK);
        lcd_draw_string(items[i].text_x, 27, items[i].label);
    }
}