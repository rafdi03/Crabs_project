/*
 * com_GSM.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Driver GSM SIM800 (onboard TTGO T-Call) via esp_modem + PPP.
 */

#ifndef MAIN_COMMUNICATION_raf_com_gsm_H_
#define MAIN_COMMUNICATION_raf_com_gsm_H_

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


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inisialisasi SIM800 + PPP netif. Blocking ~3 detik (power-on pulse).
 */
esp_err_t raf_com_gsm_init(const char *apn, const char *user, const char *pass);

/**
 * @brief Set IMEI (opsional, untuk operator yang butuh registrasi IMEI).
 */
esp_err_t raf_com_gsm_set_imei(const char *imei);

/**
 * @brief Tunggu sinyal + registrasi + PPP IP. Blocking dengan timeout.
 */
bool raf_com_gsm_wait_connected(uint32_t timeout_ms);

bool raf_com_gsm_is_connected(void);
void raf_com_gsm_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_raf_com_gsm_H_ */