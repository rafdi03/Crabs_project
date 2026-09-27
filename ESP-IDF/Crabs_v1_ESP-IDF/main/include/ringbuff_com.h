/*
 * ringbuff_com.h
 *
 *  Created on: 7 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_RINGBUFF_COM_H_
#define MAIN_INCLUDE_RINGBUFF_COM_H_

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"

#define RINGBUF_COMM_DEFAULT_SIZE   2048 // 2 KB default ring buffer
#define COMM_PACKET_PREAMBLE        0xAA // Byte penanda awal paket

// Template struktur paket komunikasi standar ber-CRC
typedef struct {
    uint8_t preamble;           // Preamble (0xAA)
    uint8_t msg_type;           // ID jenis data
    uint8_t payload_len;        // Panjang data payload
    uint8_t payload[64];        // Data payload (maks 64 bytes)
    uint16_t crc16;             // Checksum CRC-16 untuk validasi
} __attribute__((packed)) comm_packet_t;

// Struct statistik untuk monitoring & debugging kesehatan buffer
typedef struct {
    uint32_t total_sent;        // Total paket berhasil dikirim ke buffer
    uint32_t total_received;    // Total paket berhasil dibaca dari buffer
    uint32_t dropped_packets;   // Total paket gagal/terbuang karena buffer penuh
    size_t free_bytes;          // Perkiraan sisa ruang buffer dalam bytes
} ringbuf_com_stats_t;

/**
 * @brief Inisialisasi RingBuffer komunikasi.
 * @param buffer_size Ukuran buffer dalam bytes (0 untuk ukuran default 2048 bytes).
 * @return ESP_OK jika berhasil.
 */
esp_err_t ringbuf_com_init(size_t buffer_size);

/**
 * @brief Hapus dan bebaskan alokasi memori RingBuffer.
 */
void ringbuf_com_deinit(void);

/**
 * @brief Mengirim paket data ke RingBuffer dari Task (Thread-Safe).
 * @param data Pointer ke data yang akan dikirim.
 * @param len Panjang data dalam bytes.
 * @param wait_ms Waktu tunggu maksimal dalam milidetik (0 untuk non-blocking).
 * @return true jika berhasil masuk ke antrian, false jika buffer penuh atau timeout.
 */
bool ringbuf_com_send(const void *data, size_t len, uint32_t wait_ms);

/**
 * @brief Mengirim paket data ke RingBuffer dari Hardware Interrupt / ISR (ISR-Safe).
 * @param data Pointer ke data yang akan dikirim.
 * @param len Panjang data dalam bytes.
 * @param pxHigherPriorityTaskWoken Pointer ke flag context switch FreeRTOS.
 * @return true jika berhasil, false jika buffer penuh.
 */
bool ringbuf_com_send_from_isr(const void *data, size_t len, BaseType_t *pxHigherPriorityTaskWoken);

/**
 * @brief Mengambil paket data dari RingBuffer (Consumer).
 * @param item_size Output: pointer penampung ukuran data yang diterima.
 * @param wait_ms Waktu tunggu maksimal dalam milidetik (0 untuk non-blocking).
 * @return Pointer ke item data dalam buffer (NULL jika buffer kosong).
 * @note SETELAH SELESAI MEMBACA, WAJIB PANGGIL ringbuf_com_free(item) untuk membebaskan slot!
 */
void* ringbuf_com_receive(size_t *item_size, uint32_t wait_ms);

/**
 * @brief Membebaskan slot memori item setelah selesai diproses.
 * @param item Pointer data yang didapatkan dari ringbuf_com_receive().
 */
void ringbuf_com_free(void *item);

/**
 * @brief Mengambil informasi statistik buffer saat ini.
 * @return Struct ringbuf_com_stats_t berisi metriks pengiriman dan penerimaan.
 */
ringbuf_com_stats_t ringbuf_com_get_stats(void);

/**
 * @brief Mencetak status kesehatan buffer ke ESP_LOGI (untuk keperluan debug/tracing).
 */
void ringbuf_com_print_stats(void);

// =========================================================================
// UTILITAS CRC (CYCLIC REDUNDANCY CHECK)
// =========================================================================

/**
 * @brief Menghitung nilai Checksum CRC-16 (Standard CCITT/IBM LE).
 * @param data Pointer ke buffer data yang akan dihitung.
 * @param len Panjang data dalam bytes.
 * @return Nilai uint16_t CRC-16.
 */
uint16_t comm_crc16(const void *data, size_t len);

/**
 * @brief Memvalidasi data dengan nilai CRC-16 ekspektasi.
 * @param data Pointer ke buffer data.
 * @param len Panjang data dalam bytes.
 * @param expected_crc Nilai CRC-16 yang diharapkan.
 * @return true jika data valid (cocok), false jika data korup.
 */
bool comm_verify_crc16(const void *data, size_t len, uint16_t expected_crc);

/**
 * @brief Menghitung nilai Checksum CRC-32 (Standard IEEE 802.3 LE).
 * @param data Pointer ke buffer data yang akan dihitung.
 * @param len Panjang data dalam bytes.
 * @return Nilai uint32_t CRC-32.
 */
uint32_t comm_crc32(const void *data, size_t len);

/**
 * @brief Memvalidasi data dengan nilai CRC-32 ekspektasi.
 * @param data Pointer ke buffer data.
 * @param len Panjang data dalam bytes.
 * @param expected_crc Nilai CRC-32 yang diharapkan.
 * @return true jika data valid (cocok), false jika data korup.
 */
bool comm_verify_crc32(const void *data, size_t len, uint32_t expected_crc);

#endif /* MAIN_INCLUDE_RINGBUFF_COM_H_ */
