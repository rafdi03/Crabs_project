#ifndef MAIN_INCLUDE_APPLICATION_H_
#define MAIN_INCLUDE_APPLICATION_H_

#pragma once

#include "Scheduler.h"
#include "app_module.h"
#include "main.h"
#include "COM.h"
#include "ringbuff_com.h"
#include "com_wifi.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

#include "modules_config.h"
#include "DHT22.h"
#include "TempWater.h"
#include "TDS.h"
#include "JSN-SR04T.h"
#include "DO.h"
#include "Relay.h"
#include "com_mqtt.h"            
#include "IoT_Response.h"  
#include "LCD.h"

#include "COM.h"
#include "com_gsm.h"

typedef enum {
    RAF_TASK_IDX_COM_DISPATCH = 0,
    RAF_TASK_IDX_SYS_DIAG,
    RAF_TASK_IDX_DS18B20,
    RAF_TASK_IDX_TDS,
    RAF_TASK_IDX_DHT22,
    RAF_TASK_IDX_JSN_SR04T,
    RAF_TASK_IDX_DO_SENSOR,
    RAF_TASK_IDX_RELAY,
    RAF_TASK_IDX_LCD,
    RAF_TASK_IDX_COUNT
} RAF_TaskIndex_t;

esp_err_t RAF_ApplicationInit(void);
RAF_TaskId_t RAF_TaskRegistryGetId(RAF_TaskIndex_t idx);

#endif
