/*
 * com_GSM.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 *
 *  Driver SIM800 onboard TTGO T-Call via esp_modem + PPP.
 *
 *  Urutan koneksi:
 *    1. Power cycle hardware (OFF dulu → ON, biar cold-boot)
 *    2. Init DTE + DCE + PPP netif
 *    3. Sync modem dengan retry (5x)
 *    4. raf_com_gsm_prepare(): AT+CBAND → IMEI → CFUN=1,1 → Re-sync
 *    5. raf_com_gsm_wait_connected(): CSQ → CREG → PPP IP → DNS
 */

#include "com_GSM.h"

static const char *TAG = "COM_GSM";

static esp_modem_dce_t *s_dce           = NULL;
static esp_netif_t     *s_ppp_netif     = NULL;
static bool             s_gsm_connected = false;

/* ==================================================================
 * Hardware power cycle (TTGO T-Call onboard SIM800)
 *
 * PENTING: Power OFF dulu 1.5 detik untuk clear stale state.
 * Tanpa ini, kalau ESP32 reboot tanpa SIM800 dimatikan,
 * PWKEY pulse bisa gagal (SIM800 stuck di state lama).
 * ================================================================== */
static void gsm_power_on_sequence(void) {
    gpio_set_direction(GSM_PIN_POWER_ON, GPIO_MODE_OUTPUT);
    gpio_set_direction(GSM_PIN_PWKEY,    GPIO_MODE_OUTPUT);
    gpio_set_direction(GSM_PIN_RST,      GPIO_MODE_OUTPUT);

    /* --- STEP 1: Power OFF (clear stale state) --- */
    ESP_LOGI(TAG, "SIM800 power OFF (clearing stale state)...");
    gpio_set_level(GSM_PIN_POWER_ON, 0);
    gpio_set_level(GSM_PIN_PWKEY,    0);
    gpio_set_level(GSM_PIN_RST,      1);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* --- STEP 2: Power ON --- */
    ESP_LOGI(TAG, "SIM800 power ON...");
    gpio_set_level(GSM_PIN_POWER_ON, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* --- STEP 3: PWKEY pulse (turn on modem) --- */
    gpio_set_level(GSM_PIN_PWKEY, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GSM_PIN_PWKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(GSM_PIN_PWKEY, 0);

    ESP_LOGI(TAG, "Power-on pulse selesai, tunggu SIM800 boot...");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

/* ==================================================================
 * DNS manual untuk PPP netif
 * (Mencegah getaddrinfo() returns 202 setelah PPP reconnect)
 * ================================================================== */
static void gsm_set_dns(void) {
    if (s_ppp_netif == NULL) return;

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;

    /* DNS Telkomsel */
    if (esp_netif_str_to_ip4("10.204.20.1", &dns.ip.u_addr.ip4) == ESP_OK) {
        esp_netif_set_dns_info(s_ppp_netif, ESP_NETIF_DNS_MAIN, &dns);
    }

    /* Fallback: Google DNS */
    if (esp_netif_str_to_ip4("8.8.8.8", &dns.ip.u_addr.ip4) == ESP_OK) {
        esp_netif_set_dns_info(s_ppp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    }

    ESP_LOGI(TAG, "DNS PPP diset: 10.204.20.1 (main) / 8.8.8.8 (backup)");
}

/* ==================================================================
 * Init — power cycle + DTE + DCE + PPP netif + sync retry
 * ================================================================== */
esp_err_t raf_com_gsm_init(const char *apn, const char *user, const char *pass) {
    if (s_dce != NULL) return ESP_OK;    /* idempotent */

    const char *target_apn = (apn != NULL) ? apn : RAF_COM_GSM_APN;

    /* --- 1. Hardware power cycle --- */
    gsm_power_on_sequence();

    /* --- 2. DTE (UART) config --- */
    esp_modem_dte_config_t dte_cfg = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_cfg.uart_config.tx_io_num       = RAF_COM_GSM_PIN_TX;
    dte_cfg.uart_config.rx_io_num       = GSM_PIN_RX;
    dte_cfg.uart_config.rts_io_num      = -1;
    dte_cfg.uart_config.cts_io_num      = -1;
    dte_cfg.uart_config.rx_buffer_size  = 512;
    dte_cfg.uart_config.tx_buffer_size  = 512;
    dte_cfg.uart_config.event_queue_size = 30;
    dte_cfg.task_stack_size = 4096;
    dte_cfg.task_priority   = 5;

    ESP_LOGI(TAG, "UART config: TX=%d, RX=%d",
             RAF_COM_GSM_PIN_TX, GSM_PIN_RX);

    /* --- 3. DCE config (APN) --- */
    esp_modem_dce_config_t dce_cfg = ESP_MODEM_DCE_DEFAULT_CONFIG(target_apn);

    /* --- 4. PPP netif --- */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&netif_cfg);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "Gagal buat PPP netif");
        return ESP_FAIL;
    }

    /* --- 5. DCE (SIM800) --- */
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_cfg, &dce_cfg,
                              s_ppp_netif);
    if (s_dce == NULL) {
        ESP_LOGE(TAG, "Gagal buat esp_modem DCE");
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
        return ESP_FAIL;
    }

    /* --- 6. Sync modem dengan RETRY (5x) --- */
    ESP_LOGI(TAG, "Sync modem (AT)...");
    esp_err_t sync_err = ESP_FAIL;
    for (int i = 0; i < 5; i++) {
        sync_err = esp_modem_sync(s_dce);
        if (sync_err == ESP_OK) {
            ESP_LOGI(TAG, "Sync OK (attempt %d)", i + 1);
            break;
        }
        ESP_LOGW(TAG, "Sync attempt %d gagal (%s), retry...",
                 i + 1, esp_err_to_name(sync_err));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /* --- 7. Kalau sync gagal total → cleanup supaya retry bisa fresh --- */
    if (sync_err != ESP_OK) {
        ESP_LOGE(TAG, "Sync modem GAGAL setelah 5x — cek wiring TX/RX & power!");

        esp_modem_destroy(s_dce);
        s_dce = NULL;

        if (s_ppp_netif != NULL) {
            esp_netif_destroy(s_ppp_netif);
            s_ppp_netif = NULL;
        }
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "SIM800 siap. APN: %s", target_apn);
    return ESP_OK;
}

/* ==================================================================
 * Set IMEI
 * ================================================================== */
esp_err_t raf_com_gsm_set_imei(const char *imei) {
    if (s_dce == NULL) return ESP_ERR_INVALID_STATE;
    if (imei == NULL || imei[0] == '\0') return ESP_ERR_INVALID_ARG;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+EGMR=1,7,\"%s\"", imei);
    ESP_LOGI(TAG, "Set IMEI: %s", imei);

    esp_err_t err = esp_modem_at(s_dce, cmd, NULL, 1500);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Set IMEI gagal: %s", esp_err_to_name(err));
    }
    return err;
}

/* ==================================================================
 * Prepare — CBAND + IMEI + CFUN=1,1 restart
 * ================================================================== */
esp_err_t raf_com_gsm_prepare(const char *imei, const char *cband) {
    if (s_dce == NULL) return ESP_ERR_INVALID_STATE;

    const char *band = (cband != NULL) ? cband : "ALL_BAND";

    /* --- 1. Set CBAND --- */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CBAND=\"%s\"", band);
    ESP_LOGI(TAG, "Set CBAND: %s", band);
    esp_err_t err = esp_modem_at(s_dce, cmd, NULL, 2000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Set CBAND gagal (lanjut): %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    /* --- 2. Set IMEI --- */
    if (imei != NULL && imei[0] != '\0') {
        raf_com_gsm_set_imei(imei);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* --- 3. RESTART MODEM (AT+CFUN=1,1) --- */
    ESP_LOGI(TAG, "Restart modem (AT+CFUN=1,1)...");
    esp_modem_at(s_dce, "AT+CFUN=1,1", NULL, 3000);
    ESP_LOGI(TAG, "Tunggu SIM800 reboot (5s)...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* --- 4. Re-sync setelah restart (retry 10x) --- */
    ESP_LOGI(TAG, "Re-sync setelah restart...");
    bool synced = false;
    for (int i = 0; i < 10; i++) {
        if (esp_modem_sync(s_dce) == ESP_OK) {
            synced = true;
            ESP_LOGI(TAG, "Re-sync OK (attempt %d)", i + 1);
            break;
        }
        ESP_LOGW(TAG, "Re-sync attempt %d gagal, coba lagi...", i + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!synced) {
        ESP_LOGE(TAG, "Re-sync GAGAL setelah restart");
        return ESP_FAIL;
    }

    /* --- 5. Verifikasi --- */
    char resp[128] = {0};
    if (esp_modem_at(s_dce, "AT+CBAND?", resp, 1500) == ESP_OK) {
        ESP_LOGI(TAG, "CBAND aktif: %s", resp);
    }
    memset(resp, 0, sizeof(resp));
    if (esp_modem_at(s_dce, "AT+GSN", resp, 1500) == ESP_OK) {
        ESP_LOGI(TAG, "IMEI aktif: %s", resp);
    }

    ESP_LOGI(TAG, "Modem SIAP dengan CBAND='%s' + IMEI", band);
    return ESP_OK;
}

/* ==================================================================
 * Tunggu REGISTRASI NETWORK via AT+CREG?
 * ================================================================== */
bool raf_com_gsm_wait_network(uint32_t timeout_ms) {
    if (s_dce == NULL) return false;

    ESP_LOGI(TAG, "Menunggu registrasi network (CREG, max %us)...",
             timeout_ms / 1000);

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        char resp[128] = {0};
        esp_err_t err = esp_modem_at(s_dce, "AT+CREG?", resp, 2000);

        int stat = -1;
        if (err == ESP_OK) {
            char *p = strstr(resp, "+CREG:");
            if (p != NULL) {
                char *comma = strchr(p, ',');
                if (comma != NULL) {
                    stat = atoi(comma + 1);
                }
            }
        }

        int rssi = 0, ber = 0;
        esp_modem_get_signal_quality(s_dce, &rssi, &ber);

        ESP_LOGI(TAG, "  [%us] CREG=%d %s | RSSI=%d",
                 waited / 1000,
                 stat,
                 (stat == 1) ? "(home)" :
                 (stat == 5) ? "(roaming)" :
                 (stat == 2) ? "(searching)" :
                 (stat == 3) ? "(DENIED)" : "(unknown)",
                 rssi);

        if (stat == 1 || stat == 5) {
            ESP_LOGI(TAG, ">>> NETWORK REGISTERED (stat=%d, rssi=%d) <<<",
                     stat, rssi);
            return true;
        }

        if (stat == 3) {
            ESP_LOGE(TAG, "Registrasi DITOLAK operator (IMEI/SIM issue?)");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(GSM_MIN_REGISTER_CHECK_MS));
        waited += GSM_MIN_REGISTER_CHECK_MS;
    }

    ESP_LOGE(TAG, "Timeout menunggu registrasi network");
    return false;
}

/* ==================================================================
 * Tunggu koneksi lengkap: CSQ → CREG → PPP IP → DNS
 * ================================================================== */
bool raf_com_gsm_wait_connected(uint32_t timeout_ms) {
    if (s_dce == NULL) return false;

    /* ============================================================
     * STEP 1: Tunggu sinyal RF (CSQ)
     * ============================================================ */
    ESP_LOGI(TAG, "Menunggu sinyal GSM (CSQ >= %d, max %us)...",
             GSM_MIN_RSSI_CSQ, (timeout_ms / 2) / 1000);

    int rssi = 0, ber = 0;
    uint32_t waited = 0;
    uint32_t signal_timeout = timeout_ms / 2;
    if (signal_timeout < 30000) signal_timeout = 30000;

    while (waited < signal_timeout) {
        if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK
            && rssi >= GSM_MIN_RSSI_CSQ) {
            break;
        }
        if ((waited % 5000) == 0) {
            ESP_LOGI(TAG, "  Menunggu sinyal... rssi=%d (min %d)",
                     rssi, GSM_MIN_RSSI_CSQ);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited += 1000;
    }

    if (rssi < GSM_MIN_RSSI_CSQ) {
        ESP_LOGW(TAG, "Sinyal GSM lemah (rssi=%d, min=%d)",
                 rssi, GSM_MIN_RSSI_CSQ);
        return false;
    }
    ESP_LOGI(TAG, "Sinyal GSM OK (rssi=%d)", rssi);

    /* ============================================================
     * STEP 2: Tunggu REGISTRASI network (CREG)
     * ============================================================ */
    if (!raf_com_gsm_wait_network(timeout_ms)) {
        ESP_LOGE(TAG, "Gagal registrasi network");
        return false;
    }

    /* ============================================================
     * STEP 3: Masuk mode DATA (PPP)
     * ============================================================ */
    ESP_LOGI(TAG, "Beralih ke mode DATA (PPP)...");
    if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(DATA) gagal");
        return false;
    }

    /* ============================================================
     * STEP 4: Tunggu IP PPP
     * ============================================================ */
    ESP_LOGI(TAG, "Menunggu IP PPP (max %us)...", GSM_PPP_TIMEOUT_MS / 1000);

    esp_netif_ip_info_t ip = {0};
    waited = 0;
    while (waited < GSM_PPP_TIMEOUT_MS) {
        if (esp_netif_get_ip_info(s_ppp_netif, &ip) == ESP_OK
            && ip.ip.addr != 0) {
            s_gsm_connected = true;
            raf_com_net_mgr_notify_gsm(true);

            /* Set DNS manual */
            gsm_set_dns();

            ESP_LOGI(TAG, "==================================================");
            ESP_LOGI(TAG, ">>> SUKSES TERHUBUNG VIA GSM (PPP) <<<");
            ESP_LOGI(TAG, ">>> IP Address : " IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, ">>> Gateway    : " IPSTR, IP2STR(&ip.gw));
            ESP_LOGI(TAG, "==================================================");
            return true;
        }
        if ((waited % 5000) == 0 && waited > 0) {
            ESP_LOGI(TAG, "  Menunggu IP PPP... %us", waited / 1000);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited += 1000;
    }

    ESP_LOGE(TAG, "Timeout menunggu IP PPP (%us)", GSM_PPP_TIMEOUT_MS / 1000);
    return false;
}

/* ==================================================================
 * Query status
 * ================================================================== */
bool raf_com_gsm_is_connected(void) {
    return s_gsm_connected;
}

/* ==================================================================
 * Stop — lepas DCE + netif
 * ================================================================== */
void raf_com_gsm_stop(void) {
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