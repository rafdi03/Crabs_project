/*
 * com_GSM.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#include "com_GSM.h"
#include "main.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "COM_GSM";

static esp_modem_dce_t *s_dce = NULL;
static esp_netif_t     *s_ppp_netif = NULL;
static bool             s_gsm_connected = false;

/* ------------------------------------------------------------------ */
static void gsm_power_on_sequence(void) {
    gpio_set_direction(GSM_PIN_POWER_ON, GPIO_MODE_OUTPUT);
    gpio_set_level(GSM_PIN_POWER_ON, 1);

    gpio_set_direction(GSM_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(GSM_PIN_RST, 1);

    gpio_set_direction(GSM_PIN_PWKEY, GPIO_MODE_OUTPUT);
    gpio_set_level(GSM_PIN_PWKEY, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GSM_PIN_PWKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(GSM_PIN_PWKEY, 0);

    ESP_LOGI(TAG, "Power-on pulse selesai, tunggu SIM800 boot...");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

/* ------------------------------------------------------------------ */
esp_err_t com_gsm_init(const char *apn, const char *user, const char *pass) {
    if (s_dce != NULL) return ESP_OK;    // idempotent

    const char *target_apn  = (apn  != NULL) ? apn  : GSM_APN_DEFAULT;

    // 1. Hardware power-on
    gsm_power_on_sequence();

    // 2. DTE (UART) config
    esp_modem_dte_config_t dte_cfg = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_cfg.uart_config.tx_io_num = GSM_PIN_TX;
    dte_cfg.uart_config.rx_io_num = GSM_PIN_RX;
    dte_cfg.uart_config.rts_io_num = -1;
    dte_cfg.uart_config.cts_io_num = -1;
    dte_cfg.uart_config.rx_buffer_size = 512;
    dte_cfg.uart_config.tx_buffer_size = 512;
    dte_cfg.uart_config.event_queue_size = 30;
    dte_cfg.task_stack_size = 4096;
    dte_cfg.task_priority = 5;

    // 3. DCE config (APN)
    esp_modem_dce_config_t dce_cfg = ESP_MODEM_DCE_DEFAULT_CONFIG(target_apn);

    // 4. PPP netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&netif_cfg);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "Gagal buat PPP netif");
        return ESP_FAIL;
    }

    // 5. DCE (SIM800)
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_cfg, &dce_cfg, s_ppp_netif);
    if (s_dce == NULL) {
        ESP_LOGE(TAG, "Gagal buat esp_modem DCE");
        return ESP_FAIL;
    }

    // 6. Sync modem (AT OK)
    esp_modem_sync(s_dce);              // <-- tanpa prefix dce_
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "SIM800 siap. APN: %s", target_apn);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
esp_err_t com_gsm_set_imei(const char *imei) {
    if (s_dce == NULL) return ESP_ERR_INVALID_STATE;
    if (imei == NULL || imei[0] == '\0') return ESP_ERR_INVALID_ARG;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+EGMR=1,7,\"%s\"", imei);
    ESP_LOGI(TAG, "Set IMEI: %s", imei);
    return esp_modem_at(s_dce, cmd, NULL, 1000);
}

/* ------------------------------------------------------------------ */
bool com_gsm_wait_connected(uint32_t timeout_ms) {
    if (s_dce == NULL) return false;

    ESP_LOGI(TAG, "Menunggu sinyal GSM...");

    int rssi = 0, ber = 0;
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK && rssi > 5) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited += 1000;
    }
    if (rssi <= 5) {
        ESP_LOGW(TAG, "Sinyal GSM lemah (rssi=%d)", rssi);
        return false;
    }
    ESP_LOGI(TAG, "Sinyal GSM OK (rssi=%d)", rssi);

    ESP_LOGI(TAG, "Menghubungkan data (PPP)...");
    if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(DATA) gagal");
        return false;
    }

    esp_netif_ip_info_t ip = {0};
    waited = 0;
    while (waited < timeout_ms) {
        if (esp_netif_get_ip_info(s_ppp_netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            s_gsm_connected = true;
            ESP_LOGI(TAG, "==================================================");
            ESP_LOGI(TAG, ">>> SUKSES TERHUBUNG VIA GSM (PPP) <<<");
            ESP_LOGI(TAG, ">>> IP Address : " IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "==================================================");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited += 1000;
    }

    ESP_LOGE(TAG, "Timeout menunggu IP PPP");
    return false;
}

bool com_gsm_is_connected(void) { return s_gsm_connected; }

void com_gsm_stop(void) {
    if (s_dce != NULL) {
        esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
        esp_modem_destroy(s_dce);
        s_dce = NULL;
    }
    if (s_ppp_netif != NULL) {
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
    }
    s_gsm_connected = false;
    ESP_LOGI(TAG, "GSM dihentikan");
}