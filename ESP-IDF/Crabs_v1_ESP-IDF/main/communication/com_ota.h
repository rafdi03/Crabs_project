/*
 * com_ota.h
 *
 *  Created on: 14 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_COMMUNICATION_COM_OTA_H_
#define MAIN_COMMUNICATION_COM_OTA_H_

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP server lokal (WiFi only) — upload .bin via browser */
esp_err_t com_ota_init(void);
void      com_ota_stop(void);

/* OTA via URL download (works on WiFi & GSM, MQTT-triggered) */
void com_ota_trigger_url(const char *url);
bool com_ota_is_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_COM_OTA_H_ */