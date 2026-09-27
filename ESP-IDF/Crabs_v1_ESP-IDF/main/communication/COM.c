/*
 * Com.c
 *
 *  Created on: 7 Sept 2026
 *      Author: Rafdi
 */

#include "COM.h"
#include "com_wifi.h"
#include "com_GSM.h"
#include "com_mqtt.h"
#include "com_ota.h"
#include "main.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "COM_HUB";

static com_tx_handler_t  s_tx_handlers[COM_IF_MAX] = {NULL};
static com_cmd_handler_t s_cmd_handler = NULL;
static com_json_handler_t s_json_handler = NULL;
static com_net_iface_t   s_active_net = COM_NET_NONE;

/* ============================================================
 * Init — NVS + event loop + netif (dipakai WiFi & GSM)
 * ============================================================ */
esp_err_t com_init(void) {
    ESP_LOGI(TAG, "Central Communication Hub siap (Dual Mode: JSON Text & Binary 1ms Dispatcher).");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init gagal: %s", esp_err_to_name(ret));
        return ret;
    }

    // Netif + default event loop (shared WiFi & GSM)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    return ESP_OK;
}

void com_register_cmd_handler(com_cmd_handler_t handler) {
    s_cmd_handler = handler;
}

void com_register_json_handler(com_json_handler_t handler) {
    s_json_handler = handler;
}

void com_register_tx_handler(com_interface_t iface, com_tx_handler_t handler) {
    if (iface < COM_IF_MAX) {
        s_tx_handlers[iface] = handler;
    }
}

bool com_push_incoming_request(com_interface_t iface, const void *data, size_t len) {
    if (data == NULL || len == 0 || len > sizeof(com_inbound_req_t)) {
        return false;
    }

    com_inbound_req_t req;
    memset(&req, 0, sizeof(req));
    memcpy(&req, data, len);
    req.iface_source = (uint8_t)iface;

    return ringbuf_com_send(&req, sizeof(req), 0);
}

static void com_dispatch_response(const com_inbound_req_t *req) {
    if (req == NULL) return;

    comm_packet_t resp = {
        .preamble = COMM_PACKET_PREAMBLE,
        .msg_type = req->cmd_code | 0x80,
        .payload_len = 0
    };

    if (req->cmd_code == CMD_REQ_PING) {
        const char pong[] = "PONG";
        resp.payload_len = sizeof(pong) - 1;
        memcpy(resp.payload, pong, resp.payload_len);
    } else if (s_cmd_handler != NULL) {
        uint8_t out_len = 0;
        if (s_cmd_handler(req->cmd_code, req->payload, req->payload_len,
                          resp.payload, &out_len)) {
            resp.payload_len = (out_len > sizeof(resp.payload))
                               ? sizeof(resp.payload)
                               : out_len;
        } else {
            resp.msg_type = 0xFF;
        }
    } else {
        resp.msg_type = 0xFF;
    }

    resp.crc16 = comm_crc16(&resp, offsetof(comm_packet_t, crc16));

    com_interface_t src = (com_interface_t)req->iface_source;
    if (src < COM_IF_MAX && s_tx_handlers[src] != NULL) {
        s_tx_handlers[src](&resp, sizeof(resp));
    }
}

void com_update_1ms(void) {
    size_t item_size = 0;
    com_inbound_req_t *req = (com_inbound_req_t *)ringbuf_com_receive(&item_size, 0);

    if (req == NULL || item_size == 0) {
        return;
    }

    uint8_t first_byte = *(uint8_t *)req;
    com_interface_t src = (com_interface_t)req->iface_source;

    if (first_byte == '{') {
        char json_in[64] = {0};
        size_t copy_len = (item_size < sizeof(json_in) - 1)
                          ? item_size
                          : sizeof(json_in) - 1;
        memcpy(json_in, req, copy_len);
        json_in[copy_len] = '\0';

        size_t real_len = strnlen(json_in, copy_len);
        json_in[real_len] = '\0';

        char json_out[256] = {0};
        if (s_json_handler != NULL &&
            s_json_handler(json_in, json_out, sizeof(json_out))) {
            if (src < COM_IF_MAX && s_tx_handlers[src] != NULL) {
                s_tx_handlers[src](json_out, strlen(json_out));
            }
        } else {
            const char err_json[] = "{\"status\":\"ERROR\",\"msg\":\"unknown_req\"}";
            if (src < COM_IF_MAX && s_tx_handlers[src] != NULL) {
                s_tx_handlers[src](err_json, strlen(err_json));
            }
        }
    }
    else if (first_byte == COMM_PACKET_PREAMBLE) {
        if (comm_verify_crc16(req, offsetof(com_inbound_req_t, crc16), req->crc16)) {
            com_dispatch_response(req);
        } else {
            ESP_LOGW(TAG, "Request dari IF %u korup / CRC Mismatch!",
                     req->iface_source);
        }
    }

    ringbuf_com_free(req);
}

/* ============================================================
 * Network Orchestrator — WiFi dulu, fallback GSM
 * ============================================================ */
static void com_network_task(void *arg) {
    (void)arg;

    /* ---------- 1. WiFi ---------- */
    ESP_LOGI(TAG, ">>> [NET] Mencoba koneksi WiFi...");
    if (com_wifi_init(NULL, NULL) == ESP_OK) {
        if (com_wifi_wait_connected(CONNECTIVITY_WIFI_TIMEOUT_MS)) {
            s_active_net = COM_NET_WIFI;
            ESP_LOGI(TAG, ">>> [NET] AKTIF: WiFi");

            // OTA hanya di WiFi (inbound IP tersedia)
            com_ota_init();

            // MQTT di semua interface
            com_mqtt_init(MQTT_BROKER_URI_DEFAULT, MQTT_CLIENT_ID_DEFAULT);

            vTaskDelete(NULL);
            return;
        }
        ESP_LOGW(TAG, "[NET] WiFi timeout, matikan & coba GSM...");
        com_wifi_stop();
    } else {
        ESP_LOGW(TAG, "[NET] WiFi init gagal, langsung ke GSM");
    }

    /* ---------- 2. GSM fallback ---------- */
    ESP_LOGI(TAG, ">>> [NET] Mencoba koneksi GSM (SIM800)...");
    if (com_gsm_init(NULL, NULL, NULL) == ESP_OK) {

        // IMEI (opsional — khusus operator yang butuh registrasi)
        com_gsm_set_imei(GSM_IMEI_DEFAULT);

        if (com_gsm_wait_connected(CONNECTIVITY_GSM_TIMEOUT_MS)) {
            s_active_net = COM_NET_GSM;
            ESP_LOGI(TAG, ">>> [NET] AKTIF: GSM");

            // OTA TIDAK diaktifkan di GSM (tidak ada inbound IP)
            com_mqtt_init(MQTT_BROKER_URI_DEFAULT, MQTT_CLIENT_ID_DEFAULT);

            vTaskDelete(NULL);
            return;
        }
        ESP_LOGE(TAG, "[NET] GSM gagal connect");
    } else {
        ESP_LOGE(TAG, "[NET] GSM init gagal");
    }

    /* ---------- 3. Tidak ada konektivitas ---------- */
    ESP_LOGE(TAG, ">>> [NET] Tidak ada konektivitas. Mode lokal saja.");
    s_active_net = COM_NET_NONE;
    vTaskDelete(NULL);
}

esp_err_t com_network_start(void) {
    BaseType_t res = xTaskCreate(com_network_task,
                                 "com_net",
                                 6144,       // stack size
                                 NULL,
                                 5,          // priority
                                 NULL);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

com_net_iface_t com_get_active_net(void) {
    return s_active_net;
}

const char* com_get_active_net_name(void) {
    switch (s_active_net) {
    case COM_NET_WIFI: return "WiFi";
    case COM_NET_GSM:  return "GSM";
    default:           return "NONE";
    }
}