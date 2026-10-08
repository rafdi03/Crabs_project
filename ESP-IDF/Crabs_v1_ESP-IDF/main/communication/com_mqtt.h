/*
 * com_mqtt.h
 *
 *  Created on: 14 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_COMMUNICATION_COM_MQTT_H_
#define MAIN_COMMUNICATION_COM_MQTT_H_

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "COM.h"
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t com_mqtt_init(const char *broker_uri, const char *client_id);
esp_err_t com_mqtt_publish(const void *data, size_t len);
esp_err_t com_mqtt_publish_raw(const char *topic, const void *data, size_t len);
void      send_mqtt_json(void);
void      publish_relay_status(void);
bool      com_mqtt_is_connected(void);
uint32_t com_mqtt_last_tx_ms(void);
void com_mqtt_stop(void);
#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_COM_MQTT_H_ */