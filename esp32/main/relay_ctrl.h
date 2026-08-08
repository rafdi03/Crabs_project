#ifndef RELAY_CTRL_H
#define RELAY_CTRL_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// DEFINISI PIN 5-CHANNEL RELAY
// ==========================================
#define RELAY_1_PIN     GPIO_NUM_25  // Relay 1 (D25)
#define RELAY_2_PIN     GPIO_NUM_16  // Relay 2 (D16)
#define RELAY_3_PIN     GPIO_NUM_17  // Relay 3 (D17)
#define RELAY_4_PIN     GPIO_NUM_13  // Relay 4 (D13)
#define RELAY_5_PIN     GPIO_NUM_14  // Relay 5 (D14)

#define NUM_RELAYS      5

// 0 = Active LOW (Modul relay umum), 1 = Active HIGH
#define RELAY_ON_LEVEL  0
#define RELAY_OFF_LEVEL 1

// Struktur Perintah Relay untuk FreeRTOS Queue
typedef struct {
    uint8_t relay_num; // 1 s/d 5 (atau 0 untuk all)
    bool state;        // true = ON, false = OFF
} relay_cmd_t;

// ==========================================
// DEKLARASI FUNGSI MODULAR RELAY
// ==========================================
void relay_ctrl_init(void);
void relay_set_state(uint8_t relay_num, bool state);
bool relay_get_state(uint8_t relay_num);
void relay_send_cmd_queue(uint8_t relay_num, bool state);
void relay_parse_mqtt_command(const char *topic, int topic_len, const char *data, int data_len);
char* relay_get_json_status(char *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // RELAY_CTRL_H
