/*
 * relay_ctrl.c - 5-Channel Relay Controller with FreeRTOS Queue & MQTT Commands
 */

#include "relay_ctrl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "RELAY_CTRL";

// Konfigurasi pin relay dalam array
static const gpio_num_t relay_pins[NUM_RELAYS] = {
    RELAY_1_PIN, // D25
    RELAY_2_PIN, // D16
    RELAY_3_PIN, // D17
    RELAY_4_PIN, // D13
    RELAY_5_PIN  // D14
};

// Status aktual masing-masing relay (false = OFF, true = ON)
static bool relay_states[NUM_RELAYS] = {false, false, false, false, false};

// FreeRTOS Queue untuk pemrosesan asinkron
static QueueHandle_t relay_cmd_queue = NULL;

// ==========================================
// TUGAS FREERTOS KHUSUS RELAY (ASINKRON)
// ==========================================
static void relay_task(void *pvParameters) {
    relay_cmd_t cmd;
    while (1) {
        // Tunggu perintah dari Queue tanpa membebani CPU
        if (xQueueReceive(relay_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.relay_num >= 1 && cmd.relay_num <= NUM_RELAYS) {
                relay_set_state(cmd.relay_num, cmd.state);
                ESP_LOGI(TAG, "[RTOS Task] Relay %d berhasil diubah -> %s", 
                         cmd.relay_num, cmd.state ? "ON" : "OFF");
            } else if (cmd.relay_num == 0) { // 0 = Semua Relay Sekaligus
                for (int i = 1; i <= NUM_RELAYS; i++) {
                    relay_set_state(i, cmd.state);
                }
                ESP_LOGI(TAG, "[RTOS Task] Semua Relay (1-5) diubah -> %s", cmd.state ? "ON" : "OFF");
            }
        }
    }
}

// Inisialisasi Hardware Relay & RTOS Queue
void relay_ctrl_init(void) {
    ESP_LOGI(TAG, "Inisialisasi 5-Channel Relay (D25, D16, D17, D13, D14)...");

    uint64_t pin_mask = 0;
    for (int i = 0; i < NUM_RELAYS; i++) {
        pin_mask |= (1ULL << relay_pins[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Set kondisi awal semua relay OFF
    for (int i = 0; i < NUM_RELAYS; i++) {
        gpio_set_level(relay_pins[i], RELAY_OFF_LEVEL);
        relay_states[i] = false;
    }

    // Buat FreeRTOS Queue (Kapasitas 10 antrean perintah)
    relay_cmd_queue = xQueueCreate(10, sizeof(relay_cmd_t));

    // Jalankan task Relay di Core 1 / background priority 6
    xTaskCreate(relay_task, "relay_task", 2048, NULL, 6, NULL);
}

// Mengubah fisik pin relay secara langsung
void relay_set_state(uint8_t relay_num, bool state) {
    if (relay_num < 1 || relay_num > NUM_RELAYS) return;
    int idx = relay_num - 1;
    relay_states[idx] = state;
    gpio_set_level(relay_pins[idx], state ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

// Mendapatkan status relay (true/false)
bool relay_get_state(uint8_t relay_num) {
    if (relay_num < 1 || relay_num > NUM_RELAYS) return false;
    return relay_states[relay_num - 1];
}

// Mengirim perintah ke antrean FreeRTOS Queue (Thread-Safe)
void relay_send_cmd_queue(uint8_t relay_num, bool state) {
    if (relay_cmd_queue == NULL) return;
    relay_cmd_t cmd = {
        .relay_num = relay_num,
        .state = state
    };
    xQueueSend(relay_cmd_queue, &cmd, 0); // Non-blocking send
}

// Helper parser string ON / OFF / 1 / 0
static bool parse_state_string(const char *str) {
    if (strstr(str, "1") || strstr(str, "ON") || strstr(str, "on") || strstr(str, "true") || strstr(str, "TRUE")) {
        return true;
    }
    return false;
}

// Parser Perintah MQTT / Web Request
void relay_parse_mqtt_command(const char *topic, int topic_len, const char *data, int data_len) {
    char topic_buf[128];
    char data_buf[128];

    int t_len = (topic_len < sizeof(topic_buf) - 1) ? topic_len : sizeof(topic_buf) - 1;
    int d_len = (data_len < sizeof(data_buf) - 1) ? data_len : sizeof(data_buf) - 1;

    memcpy(topic_buf, topic, t_len);
    topic_buf[t_len] = '\0';

    memcpy(data_buf, data, d_len);
    data_buf[d_len] = '\0';

    ESP_LOGI(TAG, "Menerima Perintah MQTT -> Topic: %s | Data: %s", topic_buf, data_buf);

    // Format 1: Topic Spesifik per Relay (misal: "tambak/ESP32-001/relay/1/set" s/d "5/set")
    for (int i = 1; i <= NUM_RELAYS; i++) {
        char subtopic[32];
        snprintf(subtopic, sizeof(subtopic), "relay/%d/set", i);
        if (strstr(topic_buf, subtopic)) {
            bool state = parse_state_string(data_buf);
            relay_send_cmd_queue(i, state);
            return;
        }
    }

    // Format 2: Kontrol Semua Relay (misal: "tambak/ESP32-001/relay/all/set")
    if (strstr(topic_buf, "relay/all/set") || strstr(topic_buf, "relay/all")) {
        bool state = parse_state_string(data_buf);
        relay_send_cmd_queue(0, state);
        return;
    }

    // Format 3: Payload JSON (misal: {"relay": 1, "state": 1} atau {"r1":1, "r2":0, ...})
    char *relay_key = strstr(data_buf, "\"relay\"");
    char *state_key = strstr(data_buf, "\"state\"");
    if (relay_key && state_key) {
        int r_num = 0;
        int r_st = 0;
        if (sscanf(relay_key, "\"relay\":%d", &r_num) == 1 || sscanf(relay_key, "\"relay\": %d", &r_num) == 1) {
            if (sscanf(state_key, "\"state\":%d", &r_st) == 1 || sscanf(state_key, "\"state\": %d", &r_st) == 1) {
                relay_send_cmd_queue(r_num, (r_st == 1));
                return;
            }
        }
    }

    // Format 4: Batch JSON Keys (r1 s/d r5)
    for (int i = 1; i <= NUM_RELAYS; i++) {
        char r_key[16];
        snprintf(r_key, sizeof(r_key), "\"r%d\":", i);
        char *p = strstr(data_buf, r_key);
        if (p) {
            int st = 0;
            if (sscanf(p + strlen(r_key), "%d", &st) == 1) {
                relay_send_cmd_queue(i, (st == 1));
            }
        }
    }
}

// Generate JSON status semua relay untuk feedback ke Web/MQTT
char* relay_get_json_status(char *buffer, size_t max_len) {
    snprintf(buffer, max_len,
        "{\"relay1\":%d,\"relay2\":%d,\"relay3\":%d,\"relay4\":%d,\"relay5\":%d}",
        relay_states[0] ? 1 : 0,
        relay_states[1] ? 1 : 0,
        relay_states[2] ? 1 : 0,
        relay_states[3] ? 1 : 0,
        relay_states[4] ? 1 : 0);
    return buffer;
}
