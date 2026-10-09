/*
 * com_GSM.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Driver GSM SIM800 (onboard TTGO T-Call) via esp_modem + PPP.
 *
 *  Fitur baru (sync dengan versi Arduino yang terbukti berhasil):
 *    - AT+CBAND="ALL_BAND"    → buka semua band frekuensi
 *    - Set IMEI + restart modem (AT+CFUN=1,1)
 *    - Tunggu REGISTRASI network (AT+CREG?), bukan cuma CSQ
 */

#ifndef MAIN_COMMUNICATION_RAF_COM_GSM_H_
#define MAIN_COMMUNICATION_RAF_COM_GSM_H_

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "com_net_manager.h"
#include "main.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Threshold & Konstanta
 * ============================================================ */
#define GSM_MIN_RSSI_CSQ        0       /* CSQ minimal (0-31)          */
#define GSM_PPP_TIMEOUT_MS      60000U  /* Timeout tunggu IP PPP (60s) */
#define GSM_MIN_REGISTER_CHECK_MS  2000 /* Interval cek CREG           */

/* ============================================================
 * Init / Stop
 * ============================================================ */

/**
 * @brief Inisialisasi SIM800 + PPP netif. Blocking ~3 detik (power-on pulse).
 */
esp_err_t raf_com_gsm_init(const char *apn, const char *user, const char *pass);

/**
 * @brief Set IMEI (opsional, untuk operator yang butuh registrasi IMEI).
 */
esp_err_t raf_com_gsm_set_imei(const char *imei);

/**
 * @brief Persiapan penting SEBELUM tunggu koneksi:
 *          1. Set CBAND (default "ALL_BAND")
 *          2. Set IMEI (jika diberikan)
 *          3. Restart modem via AT+CFUN=1,1
 *          4. Re-sync setelah restart
 *
 *        Wajib dipanggil setelah raf_com_gsm_init() dan sebelum
 *        raf_com_gsm_wait_connected().
 *
 * @param imei  IMEI yang akan di-set (boleh NULL kalau tidak perlu)
 * @param cband Band setting: "ALL_BAND", "GSM900", "DCS1800", dll
 */
esp_err_t raf_com_gsm_prepare(const char *imei, const char *cband);

/**
 * @brief Tunggu registrasi network via AT+CREG? (stat=1 home / 5 roaming).
 */
bool raf_com_gsm_wait_network(uint32_t timeout_ms);

/**
 * @brief Tunggu sinyal + registrasi + PPP IP. Blocking dengan timeout.
 */
bool raf_com_gsm_wait_connected(uint32_t timeout_ms);

bool raf_com_gsm_is_connected(void);
void raf_com_gsm_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_RAF_COM_GSM_H_ */