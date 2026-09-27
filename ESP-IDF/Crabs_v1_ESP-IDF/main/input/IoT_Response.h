#ifndef MAIN_INPUT_IOT_RESPONSE_H_
#define MAIN_INPUT_IOT_RESPONSE_H_

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "Com.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "main.h"

// --- Include sensor riil yang dipakai (pengganti IMU_MPU.h contoh) ---
#include "DHT22.h"
#include "TempWater.h"
#include "TDS.h"
#include "JSN-SR04T.h"
#include "DO.h"

#ifdef __cplusplus
extern "C" {
#endif

void iot_response_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_INPUT_IOT_RESPONSE_H_ */