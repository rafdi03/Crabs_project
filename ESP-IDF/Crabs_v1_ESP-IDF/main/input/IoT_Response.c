/*
 * IoT_Response.c
 *  Created on: 9 Sept 2026
 *      Author: Rafdi
 *
 *  Template output seluruh modul input ke logic system.
 */

#include "IoT_Response.h"

static const char *TAG = "IOT_RESP";

/* ============================================================================
 * Snapshot semua sensor (satu pintu keluar menuju logic system / dashboard)
 * ==========================================================================*/
typedef struct {
    float suhu_air;
    float suhu_udara;
    float lembap_udara;
    float jarak_cm;
    float do_mg;
    int   tds_ppm;
    bool  dht_valid;
} iot_sensor_snapshot_t;

static iot_sensor_snapshot_t iot_snapshot_sensors(void) {
    iot_sensor_snapshot_t s = {0};

    dht22_data_t dht = {0};
    s.dht_valid = dht22_get_data(&dht);

    s.suhu_air      = ds18b20_get_temp();
    s.tds_ppm       = tds_get_ppm();
    s.jarak_cm      = jsn_get_distance_cm();
    s.do_mg         = do_get_value();

    if (s.dht_valid) {
        s.suhu_udara   = dht.temperature;
        s.lembap_udara = dht.humidity;
    }

    // Sanitasi nilai invalid
    if (s.suhu_air == -999.0f || s.suhu_air == -127.0f) s.suhu_air = 0.0f;
    if (s.tds_ppm == -999)                              s.tds_ppm  = 0;
    if (s.jarak_cm < 0)                                 s.jarak_cm = 0.0f;
    if (s.do_mg == -999.0f)                             s.do_mg    = 0.0f;

    return s;
}

/* ============================================================================
 * Binary handler — untuk protokol biner (LoRa / Modbus / UART)
 * ==========================================================================*/
static bool iot_binary_cmd_handler(uint8_t cmd_code,
                                   const uint8_t *in_payload, uint8_t in_len,
                                   uint8_t *out_payload, uint8_t *out_len) {
    (void)in_payload; (void)in_len;

    if (cmd_code == CMD_REQ_ALL_SENSORS || cmd_code == CMD_REQ_IMU) {
        iot_sensor_snapshot_t s = iot_snapshot_sensors();
        float tds_f = (float)s.tds_ppm;

        memcpy(&out_payload[0],  &s.suhu_air,      sizeof(float));
        memcpy(&out_payload[4],  &s.suhu_udara,    sizeof(float));
        memcpy(&out_payload[8],  &s.lembap_udara,  sizeof(float));
        memcpy(&out_payload[12], &s.jarak_cm,      sizeof(float));
        memcpy(&out_payload[16], &s.do_mg,         sizeof(float));
        memcpy(&out_payload[20], &tds_f,           sizeof(float));

        *out_len = sizeof(float) * 6;
        return true;
    }
    return false;
}

/* ============================================================================
 * JSON handler — untuk Website / MQTT / Dashboard
 * ==========================================================================*/
static bool iot_json_request_handler(const char *json_req,
                                     char *json_resp, size_t max_resp_len) {
    iot_sensor_snapshot_t s = iot_snapshot_sensors();

    // 1. Suhu (air + udara)
    if (strstr(json_req, "\"req\":\"temp\"") || strstr(json_req, "\"req\":\"suhu\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"type\":\"temp\","
            "\"suhu_air\":%.1f,\"suhu_udara\":%.1f,\"lembap_udara\":%.1f,"
            "\"unit\":\"C\"}",
            s.suhu_air, s.suhu_udara, s.lembap_udara);
        return true;
    }
    // 2. TDS
    else if (strstr(json_req, "\"req\":\"tds\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"type\":\"tds\",\"tds_ppm\":%d}", s.tds_ppm);
        return true;
    }
    // 3. Jarak (level air)
    else if (strstr(json_req, "\"req\":\"jarak\"") || strstr(json_req, "\"req\":\"level\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"type\":\"jarak\",\"jarak_cm\":%.1f}", s.jarak_cm);
        return true;
    }
    // 4. DO (Dissolved Oxygen)
    else if (strstr(json_req, "\"req\":\"do\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"type\":\"do\",\"do_mg\":%.2f}", s.do_mg);
        return true;
    }
    // 5. Semua sensor (untuk dashboard)
    else if (strstr(json_req, "\"req\":\"all\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"type\":\"all\","
            "\"suhu_air\":%.1f,\"suhu_udara\":%.1f,\"lembap_udara\":%.1f,"
            "\"tds_ppm\":%d,\"jarak_cm\":%.1f,\"do_mg\":%.2f}",
            s.suhu_air, s.suhu_udara, s.lembap_udara,
            s.tds_ppm, s.jarak_cm, s.do_mg);
        return true;
    }
    // 6. Ping / heartbeat
    else if (strstr(json_req, "\"req\":\"ping\"")) {
        snprintf(json_resp, max_resp_len,
            "{\"status\":\"OK\",\"msg\":\"pong\",\"uptime\":%lu}",
            (unsigned long)(esp_timer_get_time() / 1000000ULL));
        return true;
    }

    return false;
}

void iot_response_init(void) {
    com_register_cmd_handler(iot_binary_cmd_handler);
    com_register_json_handler(iot_json_request_handler);
    ESP_LOGI(TAG, "IoT Response Handlers (JSON & Biner) siap. Gerbang output semua sensor aktif.");
}