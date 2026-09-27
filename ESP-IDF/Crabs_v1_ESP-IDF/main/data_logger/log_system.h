/*
 * log_system.h
 *
 *  Created on: 8 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Mesin logika pemrosesan log murni (Pure Engine Logic),
 *             Ping-Pong Buffer, Chunk Builder, dan Boot Recovery scanner.
 */

#ifndef MAIN_SYSTEM_DATA_LOGGER_LOG_SYSTEM_H_
#define MAIN_SYSTEM_DATA_LOGGER_LOG_SYSTEM_H_

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_SECTOR_SIZE         4096    /*!< Ukuran 1 Sektor Flash NOR (4KB) */

/**
 * @brief  Inisialisasi engine pemroses log, alokasi Ping-Pong buffer, dan jalankan Boot Recovery.
 * @param  None
 * @retval ESP_OK jika inisialisasi berhasil, atau esp_err_t jika gagal.
 */
esp_err_t log_system_init(void);

/**
 * @brief  Mendorong 1 data record ke dalam Active Ping-Pong Buffer (Non-blocking / Fast).
 * @param  record Pointer ke data record byte.
 * @param  record_len Panjang ukuran 1 record dalam byte.
 * @retval ESP_OK jika berhasil disimpan ke buffer, atau kode error esp_err_t jika gagal.
 */
esp_err_t log_system_push_raw_record(const void *record, size_t record_len);

/**
 * @brief  Memaksa penulisan sisa data di active buffer ke penyimpanan fisik (Flush/Sync).
 * @param  None
 * @retval ESP_OK jika berhasil diflush ke media penyimpanan.
 */
esp_err_t log_system_flush(void);

/**
 * @brief  Mendapatkan alamat offset tulis aktif saat ini pada storage.
 * @param  None
 * @retval Alamat byte offset (uint64_t).
 */
uint64_t log_system_get_write_offset(void);

/**
 * @brief  Mendapatkan nomor urut sequence blok aktif saat ini.
 * @param  None
 * @retval Sequence number (uint32_t).
 */
uint32_t log_system_get_sequence(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_SYSTEM_DATA_LOGGER_LOG_SYSTEM_H_ */

