/*
 * com_wifi.c
 *
 *  Created on: 14 Sept 2026
 *      Author: Rafdi
 *
 *  WiFi Station mode dengan idempotent init — aman untuk stop/start berulang.
 */

#include "com_wifi.h"

static const char *TAG = "COM_WIFI";

static bool         s_wifi_connected = false;
static bool         s_stack_inited   = false;   /* WiFi stack + netif init sekali */
static esp_netif_t *s_sta_netif      = NULL;

/* ------------------------------------------------------------------ */
static esp_err_t wifi_apply_static_ip(esp_netif_t *netif) {
#if WIFI_STATIC_IP_ENABLED
    if (netif == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc_stop: %s (lanjut)", esp_err_to_name(err));
    }

    esp_netif_ip_info_t ip = {0};
    esp_netif_str_to_ip4(WIFI_STATIC_IP_ADDR,    &ip.ip);
    esp_netif_str_to_ip4(WIFI_STATIC_IP_GW,      &ip.gw);
    esp_netif_str_to_ip4(WIFI_STATIC_IP_NETMASK, &ip.netmask);

    err = esp_netif_set_ip_info(netif, &ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_ip_info gagal: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4(WIFI_STATIC_DNS1, &dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_str_to_ip4(WIFI_STATIC_DNS2, &dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);

    ESP_LOGI(TAG, "Static IP: %s / GW %s / DNS %s",
             WIFI_STATIC_IP_ADDR, WIFI_STATIC_IP_GW, WIFI_STATIC_DNS1);
#else
    (void)netif;
    ESP_LOGI(TAG, "DHCP dynamic IP aktif");
#endif
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Driver siap, menghubungkan ke AP...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        raf_com_net_mgr_notify_wifi(false);

        wifi_event_sta_disconnected_t *d =
            (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Terputus (Reason: %d, RSSI: %d). Reconnect...",
                 d->reason, d->rssi);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        raf_com_net_mgr_notify_wifi(true);

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, ">>> WIFI CONNECTED <<<");
        ESP_LOGI(TAG, ">>> IP      : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, ">>> Netmask : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, ">>> Gateway : " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "==================================================");
    }
}

/* ------------------------------------------------------------------ */
esp_err_t raf_com_wifi_init(const char *ssid, const char *pass) {
    /* --- 1. Netif + WiFi stack: HANYA SEKALI --- */
    if (!s_stack_inited) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "Gagal buat STA netif");
            return ESP_FAIL;
        }

        wifi_apply_static_ip(s_sta_netif);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

        s_stack_inited = true;
        ESP_LOGI(TAG, "WiFi stack initialized (first time)");
    }

    /* --- 2. Config + start (bisa berulang) --- */
    const char *target_ssid = (ssid != NULL) ? ssid : RAF_COM_WIFI_SSID;
    const char *target_pass = (pass != NULL) ? pass : WIFI_PASS_DEFAULT;

    wifi_config_t wifi_config = {
        .sta = {
            .scan_method        = WIFI_ALL_CHANNEL_SCAN,
            .sort_method        = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    strncpy((char *)wifi_config.sta.ssid,
            target_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password,
            target_pass, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "WiFi dimulai untuk SSID: '%s'", target_ssid);
    return ESP_OK;
}

bool com_wifi_is_connected(void) { return s_wifi_connected; }

bool com_wifi_wait_connected(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    while (!s_wifi_connected && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return s_wifi_connected;
}

void com_wifi_stop(void) {
    /* Cukup stop radio. JANGAN deinit — supaya bisa start ulang. */
    esp_wifi_stop();
    s_wifi_connected = false;
    ESP_LOGI(TAG, "WiFi stopped (stack tetap ter-init)");
}