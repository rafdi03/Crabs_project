/*
 * app_log.h
 *
 *  Created on: 8 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Eksekutor fisik penyimpanan (Storage Driver) & manajemen sektor
 *             dengan dukungan SPI Flash & SD Card serta strategi Pre-Erase.
 */

#ifndef MAIN_SYSTEM_DATA_LOGGER_APP_LOG_H_
#define MAIN_SYSTEM_DATA_LOGGER_APP_LOG_H_

#pragma once
#include "data_logger.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Mendapatkan pointer ke struktur backend penyimpanan aktif (SPI Flash / SD Card).
 * @param  None
 * @retval Pointer ke storage_backend_t.
 */
storage_backend_t* app_log_get_backend(void);

/**
 * @brief  Memeriksa dan mengeksekusi penghapusan sektor berikutnya (Pre-Erase N+1) di background.
 * @param  current_address Alamat tulis aktif saat ini dalam byte.
 * @retval ESP_OK jika sektor berhasil disiapkan/dihapus, atau esp_err_t jika gagal.
 */
esp_err_t app_log_pre_erase_check(uint64_t current_address);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_SYSTEM_DATA_LOGGER_APP_LOG_H_ */

