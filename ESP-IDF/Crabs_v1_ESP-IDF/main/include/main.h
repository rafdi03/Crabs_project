/*
 * main.h
 *
 *  Created on: 7 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_MAIN_H_
#define MAIN_INCLUDE_MAIN_H_

#include <stdint.h>
#include "bsp_pins.h"

#pragma once

/* ===== WiFi Credentials ===== */
#define RAF_COM_WIFI_SSID        "LEPTOPAING"
#define WIFI_PASS_DEFAULT        "BRTMOBILE"

/* ===== WiFi Static IP ===== */
#define WIFI_STATIC_IP_ENABLED   1
#define WIFI_STATIC_IP_ADDR      "192.168.137.100"
#define WIFI_STATIC_IP_GW        "192.168.137.1"
#define WIFI_STATIC_IP_NETMASK   "255.255.255.0"
#define WIFI_STATIC_DNS1         "8.8.8.8"
#define WIFI_STATIC_DNS2         "1.1.1.1"

/* ===== MQTT ===== */
#define MQTT_BROKER_URI_DEFAULT  "mqtt://broker.emqx.io:1883"
#define MQTT_CLIENT_ID_DEFAULT   "ESP32-001"
#define MQTT_DEVICE_ID           "ESP32-001"
#define MQTT_TOPIC_SENSOR        "tambak/" MQTT_DEVICE_ID "/sensor"
#define MQTT_TOPIC_RELAY_SET     "tambak/" MQTT_DEVICE_ID "/relay/+/set"
#define MQTT_TOPIC_RELAY_ALL_SET "tambak/" MQTT_DEVICE_ID "/relay/all/set"
#define MQTT_TOPIC_RELAY_STATUS  "tambak/" MQTT_DEVICE_ID "/relay/status"
#define MQTT_TOPIC_RESP_DEFAULT  "tambak/" MQTT_DEVICE_ID "/resp"

/* ===== MQTT OTA (baru) ===== */
#define MQTT_TOPIC_OTA_URL       "tambak/" MQTT_DEVICE_ID "/ota/url"
#define MQTT_TOPIC_OTA_STATUS    "tambak/" MQTT_DEVICE_ID "/ota/status"

/* ===== GSM ===== */
#define RAF_COM_GSM_APN          "internet"
#define GSM_IMEI_DEFAULT         "864043050823850"
#define RAF_COM_GSM_PIN_TX       27      
#define GSM_PIN_RX               26      
#define GSM_PIN_POWER_ON         23
#define GSM_PIN_RST              5
#define GSM_PIN_PWKEY            4

/* ===== Network Timeouts ===== */
#define CONNECTIVITY_WIFI_TIMEOUT_MS   30000
#define CONNECTIVITY_GSM_TIMEOUT_MS    60000

#endif /* MAIN_INCLUDE_MAIN_H_ */
