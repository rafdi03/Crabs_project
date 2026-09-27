/*
 * Com.h
 *
 *  Created on: 7 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_COM_H_
#define MAIN_INCLUDE_COM_H_

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ringbuff_com.h"
#include "esp_log.h"
#include <string.h>

// Definisi 8 Antarmuka Komunikasi Universal
typedef enum {
    COM_IF_UART = 0,    // UART (Serial PC / RS485 / Industrial Sensor)
    COM_IF_MODBUS,      // Modbus RTU (RS485) / Modbus TCP
    COM_IF_LORA,        // LoRa Module (SPI / SX1276 / SX1278 P2P)
    COM_IF_WIFI_HTTP,   // WiFi / WebServer REST API Endpoint
    COM_IF_MQTT,        // MQTT Broker Pub/Sub
    COM_IF_CAN,         // CAN Bus / TWAI Controller (Automotive)
    COM_IF_BLE,         // Bluetooth Low Energy GATT
    COM_IF_ESPNOW,      // ESP-NOW 2.4GHz Ultra-Fast Wireless (Peer-to-Peer / Broadcast)
    COM_IF_MAX
} com_interface_t;

// Definisi Kode Perintah Request Standar (Master -> ESP32)
typedef enum {
    CMD_REQ_PING            = 0x01, // Tes koneksi / Heartbeat
    CMD_REQ_ALL_SENSORS     = 0x02, // Request semua data sensor
    CMD_REQ_IMU             = 0x03, // Request data IMU MPU6050
    CMD_REQ_GPS             = 0x04, // Request data GPS
    CMD_REQ_SYS_STATUS      = 0x05, // Request data kesehatan baterai / status RTOS
    CMD_REQ_CUSTOM          = 0x0F  // Request kustom payload
} com_cmd_code_t;

// Format Header Request Masuk (Inbound Request Packet)
typedef struct {
    uint8_t preamble;           // COMM_PACKET_PREAMBLE (0xAA)
    uint8_t cmd_code;           // com_cmd_code_t (Perintah yang diminta)
    uint8_t payload_len;        // Panjang data tambahan (jika ada)
    uint8_t payload[32];        // Parameter request
    uint16_t crc16;             // CRC Checksum validasi request over-the-air
    uint8_t iface_source;       // Metadata routing internal: Interface penerima lokal
} __attribute__((packed)) com_inbound_req_t;

/* ============================================================
 * Network Orchestrator — auto-switch WiFi ↔ GSM
 * ============================================================ */
typedef enum {
    COM_NET_NONE = 0,
    COM_NET_WIFI,
    COM_NET_GSM,
} com_net_iface_t;

/**
 * @brief Start task async yang akan:
 *        1. Coba WiFi (timeout CONNECTIVITY_WIFI_TIMEOUT_MS)
 *        2. Kalau gagal → fallback GSM (timeout CONNECTIVITY_GSM_TIMEOUT_MS)
 *        3. Setelah online → trigger OTA (WiFi only) + MQTT
 */
esp_err_t       com_network_start(void);

/** @brief Query interface yang aktif saat ini. */
com_net_iface_t com_get_active_net(void);

/** @brief Nama interface aktif: "WiFi", "GSM", atau "NONE". */
const char*     com_get_active_net_name(void);

typedef esp_err_t (*com_tx_handler_t)(const void *data, size_t len);

/**
 * @brief Tipe callback function pointer untuk menyediakan data sensor/kustom ke Com Hub saat ada request dari Master.
 * @param cmd_code Kode perintah yang diminta (misal CMD_REQ_IMU, CMD_REQ_ALL_SENSORS, CMD_REQ_CUSTOM).
 * @param in_payload Pointer ke payload parameter request dari Master (jika ada).
 * @param in_len Panjang payload parameter request.
 * @param out_payload Buffer output yang diisi oleh handler aplikasi (maksimal 64 bytes).
 * @param out_len Pointer penampung jumlah byte yang berhasil diisi ke out_payload.
 * @return true jika perintah berhasil diproses/dikenali, false jika perintah tidak dikenali.
 */
typedef bool (*com_cmd_handler_t)(uint8_t cmd_code, const uint8_t *in_payload, uint8_t in_len, uint8_t *out_payload, uint8_t *out_len);

/**
 * @brief Tipe callback function pointer untuk memproses request berbasis string JSON dari Web / MQTT.
 * @param json_req String JSON masuk dari Master (null-terminated).
 * @param json_resp Buffer output string JSON balasan dari ESP32.
 * @param max_resp_len Kapasitas maksimal buffer output.
 * @return true jika command dikenali dan berhasil diproses, false jika gagal/unknown.
 */
typedef bool (*com_json_handler_t)(const char *json_req, char *json_resp, size_t max_resp_len);

/**
 * @brief Inisialisasi subsistem komunikasi terpusat (Com Hub).
 * @return ESP_OK jika berhasil.
 */
esp_err_t com_init(void);

/**
 * @brief Registrasi custom command handler / data provider dari modul input/sensor aplikasi (Biner).
 * @param handler Function pointer ke callback penyedia data sensor.
 */
void com_register_cmd_handler(com_cmd_handler_t handler);

/**
 * @brief Registrasi JSON command handler untuk request berbasis teks/JSON dari Web & MQTT.
 * @param handler Function pointer ke callback pemroses JSON.
 */
void com_register_json_handler(com_json_handler_t handler);

/**
 * @brief Registrasi fungsi driver TX untuk antarmuka tertentu (LoRa, WiFi, UART, Modbus, dll).
 * @param iface Antarmuka komunikasi target.
 * @param handler Function pointer callback pengiriman data.
 */
void com_register_tx_handler(com_interface_t iface, com_tx_handler_t handler);

/**
 * @brief Masukkan request baru ke antrian Com Hub dari Driver/ISR manapun (ISR & Thread-safe).
 * @param iface Antarmuka pengirim.
 * @param data Pointer ke paket request.
 * @param len Ukuran request dalam bytes.
 * @return true jika berhasil masuk antrian, false jika gagal.
 */
bool com_push_incoming_request(com_interface_t iface, const void *data, size_t len);

/**
 * @brief Eksekusi & Dispatcher request secepat kilat (Non-blocking).
 * @note WAJIB DIPANGGIL DI job_1ms() pada Core 1.
 *       Jika tidak ada request, fungsi selesai dalam < 1 mikrodetik.
 */
void com_update_1ms(void);

#endif /* MAIN_INCLUDE_COM_H_ */
