/*
 * com_mqtt.c
 *
 *  Created on: 14 Sept 2026
 *      Author: Rafdi
 *
 *  Tambahan: tracking waktu disconnect untuk recovery dari net_mgr.
 */

#include "com_mqtt.h"
#include "com_ota.h"
#include "Relay.h"
#include "TempWater.h"
#include "TDS.h"
#include "DHT22.h"
#include "JSN-SR04T.h"
#include "DO.h"
#include <strings.h>

static const char *TAG = "COM_MQTT";
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool     s_mqtt_connected     = false;
static uint32_t s_last_tx_ms         = 0;
static int64_t  s_last_disconnect_us = 0;   /* ⭐ waktu disconnect terakhir */

/* ========================================================================= */
static bool parse_state_payload(const char *payload, size_t len) {
    if (payload == NULL || len == 0) return false;
    if (len == 1 && payload[0] == '1') return true;
    if (strncasecmp(payload, "on", 2) == 0) return true;
    if (strncasecmp(payload, "true", 4) == 0) return true;
    return false;
}

/* ========================================================================= */
void publish_relay_status(void) {
    if (!s_mqtt_connected || s_mqtt_client == NULL) return;

    relay_snapshot_t snap = relay_get_snapshot();

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"relay1\":%d,\"relay2\":%d,\"relay3\":%d,\"relay4\":%d,\"relay5\":%d}",
             snap.state[0] ? 1 : 0,
             snap.state[1] ? 1 : 0,
             snap.state[2] ? 1 : 0,
             snap.state[3] ? 1 : 0,
             snap.state[4] ? 1 : 0);

    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_RELAY_STATUS,
                            payload, strlen(payload), 0, 0);
}

/* =========================================================================
 * Handle incoming MQTT data
 * ========================================================================= */
static void handle_mqtt_data(const char *topic, int topic_len,
                             const char *data, int data_len) {
    char topic_buf[160] = {0};
    if (topic_len >= (int)sizeof(topic_buf)) return;
    memcpy(topic_buf, topic, topic_len);

    char data_buf[512] = {0};
    int copy_len = (data_len < (int)sizeof(data_buf) - 1)
                   ? data_len : (int)sizeof(data_buf) - 1;
    memcpy(data_buf, data, copy_len);

    ESP_LOGI(TAG, "[MQTT RX] Topic: %s | Payload: %s", topic_buf, data_buf);

    /* ---- 1. OTA URL ---- */
    if (strncmp(topic_buf, MQTT_TOPIC_OTA_URL,
                strlen(MQTT_TOPIC_OTA_URL)) == 0) {
        ESP_LOGW(TAG, "[OTA] URL diterima: %s", data_buf);
        com_ota_trigger_url(data_buf);
        return;
    }

    /* ---- 2. Filter topik status ---- */
    if (strstr(topic_buf, "/status") != NULL) return;

    /* ---- 3. Relay all ---- */
    if (strstr(topic_buf, "/relay/all/set") != NULL) {
        bool st = parse_state_payload(data_buf, strlen(data_buf));
        relay_set(0, st);
        publish_relay_status();
        return;
    }

    /* ---- 4. Relay N (1..5) ---- */
    for (int i = 1; i <= 5; i++) {
        char pattern[24];
        snprintf(pattern, sizeof(pattern), "/relay/%d/set", i);
        if (strstr(topic_buf, pattern) != NULL) {
            bool st = parse_state_payload(data_buf, strlen(data_buf));
            relay_set(i, st);
            publish_relay_status();
            return;
        }
    }

    /* ---- 5. Fallback ke Com Hub ---- */
    com_push_incoming_request(COM_IF_MQTT, data, (size_t)data_len);
}

/* =========================================================================
 * MQTT Event Handler
 * ========================================================================= */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected     = true;
        s_last_disconnect_us = 0;   /* ⭐ reset timer disconnect */

        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, ">>> MQTT CONNECTED <<<");
        ESP_LOGI(TAG, ">>> Device ID : %s", MQTT_DEVICE_ID);
        ESP_LOGI(TAG, ">>> Publish   : %s", MQTT_TOPIC_SENSOR);
        ESP_LOGI(TAG, ">>> Subscribe : %s", MQTT_TOPIC_RELAY_SET);
        ESP_LOGI(TAG, ">>> Subscribe : %s", MQTT_TOPIC_RELAY_ALL_SET);
        ESP_LOGI(TAG, ">>> Subscribe : %s", MQTT_TOPIC_OTA_URL);
        ESP_LOGI(TAG, "==================================================");

        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_RELAY_SET,     1);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_RELAY_ALL_SET, 1);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_OTA_URL,       1);

        publish_relay_status();
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        if (s_last_disconnect_us == 0) {
            s_last_disconnect_us = esp_timer_get_time();  /* ⭐ catat waktu */
        }
        ESP_LOGW(TAG, "Terputus dari broker MQTT");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribe OK (msg_id=%d)", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        handle_mqtt_data(event->topic, event->topic_len,
                         event->data, event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        /* ⭐ Kalau belum connected dan belum ada timestamp → catat */
        if (!s_mqtt_connected && s_last_disconnect_us == 0) {
            s_last_disconnect_us = esp_timer_get_time();
        }
        ESP_LOGE(TAG, "Error pada client MQTT");
        break;

    default:
        break;
    }
}

/* =========================================================================
 * Init
 * ========================================================================= */
esp_err_t com_mqtt_init(const char *broker_uri, const char *client_id) {
    if (s_mqtt_client != NULL) return ESP_OK;

    const char *uri = (broker_uri != NULL) ? broker_uri : MQTT_BROKER_URI_DEFAULT;
    const char *cid = (client_id != NULL)  ? client_id  : MQTT_DEVICE_ID;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri    = uri,
        .credentials.client_id = cid,
        .session.keepalive     = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Gagal membuat instance MQTT client!");
        return ESP_FAIL;
    }

    com_register_tx_handler(COM_IF_MQTT, com_mqtt_publish);

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Client started. Connecting ke %s (Client: %s)...", uri, cid);
    return ESP_OK;
}

/* =========================================================================
 * Publish
 * ========================================================================= */
esp_err_t com_mqtt_publish(const void *data, size_t len) {
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_mqtt_client,
                                         MQTT_TOPIC_RESP_DEFAULT,
                                         (const char *)data, len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Gagal publish (%u bytes)", (unsigned)len);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Publish OK (%u bytes) -> %s",
             (unsigned)len, MQTT_TOPIC_RESP_DEFAULT);
    return ESP_OK;
}

esp_err_t com_mqtt_publish_raw(const char *topic, const void *data, size_t len) {
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (topic == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic,
                                         (const char *)data, (int)len, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

void send_mqtt_json(void) {
    if (!s_mqtt_connected || s_mqtt_client == NULL) return;

    dht22_data_t dht = {0};
    bool dht_valid = dht22_get_data(&dht);

    float suhu_air  = ds18b20_get_temp();
    int   tds_ppm   = tds_get_ppm();
    float jarak_cm  = jsn_get_distance_cm();
    float do_mg     = do_get_value();

    char payload[384];
    int written = snprintf(payload, sizeof(payload),
        "{\"device_id\":\"%s\","
        "\"suhu_air\":%.1f,"
        "\"tds_ppm\":%d,"
        "\"jarak_cm\":%.1f,"
        "\"suhu_udara\":%.1f,"
        "\"lembap_udr\":%.1f,"
        "\"do_mg\":%.2f}",
        MQTT_DEVICE_ID,
        (suhu_air != -999.0f && suhu_air != -127.0f) ? suhu_air : 0.0f,
        (tds_ppm != -999) ? tds_ppm : 0,
        (jarak_cm > 0) ? jarak_cm : 0.0f,
        dht_valid ? dht.temperature : 0.0f,
        dht_valid ? dht.humidity    : 0.0f,
        (do_mg != -999.0f) ? do_mg : 0.0f);

    if (written <= 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "JSON buffer overflow!");
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_SENSOR,
                                          payload, written, 0, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Gagal publish sensor");
    } else {
        ESP_LOGI(TAG, "Sensor publish OK (%d bytes)", written);
        s_last_tx_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
}

void com_mqtt_stop(void) {
    if (s_mqtt_client == NULL) return;
    ESP_LOGI(TAG, "MQTT stopping...");
    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
    s_mqtt_connected = false;
    s_last_disconnect_us = 0;
    ESP_LOGI(TAG, "MQTT stopped");
}

uint32_t com_mqtt_last_tx_ms(void) {
    return s_last_tx_ms;
}

bool com_mqtt_is_connected(void) {
    return s_mqtt_connected;
}

/* ⭐ Fungsi baru untuk net_mgr monitoring */
int64_t com_mqtt_last_disconnect_us(void) {
    return s_last_disconnect_us;
}

bool com_mqtt_is_reconnecting(void) {
    return (s_mqtt_client != NULL && !s_mqtt_connected);
}