/*
 * Scheduler.h
 *
 *  Created on: 4 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_SCHEDULER_H_
#define MAIN_SCHEDULER_H_

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h> 
#include "raf_config.h"  

#define RAF_RT_MAX_TASKS          RAF_CONFIG_MAX_TASKS
#define RAF_RT_TASK_NAME_MAX_LEN  RAF_CONFIG_TASK_NAME_MAX_LEN

typedef uint16_t RAF_TaskId_t;
#define RAF_RT_INVALID_TASK_ID    0xFFFF

typedef enum {
    RAF_RT_STATE_UNINITIALIZED = 0,
    RAF_RT_STATE_INITIALIZED,
    RAF_RT_STATE_REGISTERING,
    RAF_RT_STATE_READY,
    RAF_RT_STATE_RUNNING,
    RAF_RT_STATE_STOPPED,
    RAF_RT_STATE_ERROR
} RAF_SchedulerState_t;

typedef enum {
    RAF_RT_PRIO_BACKGROUND = 1, // Core 0 - Low Priority
    RAF_RT_PRIO_LOW        = 2, // Core 0
    RAF_RT_PRIO_MID        = 3, // Core 1 - General Task
    RAF_RT_PRIO_HIGH       = 4, // Core 1 - Fast Sensor / Control
    RAF_RT_PRIO_REALTIME   = 5  // Core 1 - Time Critical
} RAF_TaskPriority_t;

typedef enum {
    RAF_RT_CORE_0   = 0,
    RAF_RT_CORE_1   = 1,
    RAF_RT_CORE_ANY = -1
} RAF_TaskCore_t;

typedef void (*RAF_TaskCallback_t)(void *arg);

// Task Descriptor Configuration
typedef struct {
    char name[RAF_RT_TASK_NAME_MAX_LEN];

    uint32_t period_ms;         // Periode eksekusi (ms)
    uint32_t phase_ms;          // Offset eksekusi awal (ms) untuk mencegah CPU burst
    uint32_t deadline_ms;       // Batas toleransi durasi eksekusi (ms)

    RAF_TaskPriority_t priority; // FreeRTOS Priority (1-5)
    RAF_TaskCore_t core;         // Core Affinity (0, 1, atau ANY)
    uint32_t stack_size;        // Ukuran stack (Bytes)

    RAF_TaskCallback_t callback; // Function pointer logika aplikasi
    void *arg;                  // Parameter callback
    bool enabled_on_boot;       // Status aktif saat scheduler start
} RAF_TaskConfig_t;

// Real-Time Metrics & Profiler
typedef struct {
    RAF_TaskId_t id;
    char name[RAF_RT_TASK_NAME_MAX_LEN];
    bool enabled;

    uint32_t execution_count;
    uint32_t execution_overruns;
    uint32_t deadline_misses;    
    uint32_t missed_periods;     

    uint32_t min_exec_us;
    uint32_t max_exec_us;
    uint32_t avg_exec_us;
    uint32_t last_exec_us;
    uint32_t max_jitter_us;
    uint32_t stack_high_water; 
} RAF_TaskStats_t;

// API Lifecycle Engine
esp_err_t RAF_SchedulerInit(void);
esp_err_t RAF_SchedulerRegisterTask(const RAF_TaskConfig_t *config, RAF_TaskId_t *out_task_id);
esp_err_t RAF_SchedulerStart(void);
RAF_SchedulerState_t RAF_SchedulerGetState(void);

// Runtime Control & Profiler API
esp_err_t RAF_TaskEnable(RAF_TaskId_t task_id);
esp_err_t RAF_TaskDisable(RAF_TaskId_t task_id);
esp_err_t RAF_TaskGetStats(RAF_TaskId_t task_id, RAF_TaskStats_t *out_stats);
esp_err_t RAF_TaskEnableByName(const char *task_name);
esp_err_t RAF_TaskDisableByName(const char *task_name);
esp_err_t RAF_TaskGetStatsByName(const char *task_name, RAF_TaskStats_t *out_stats);
esp_err_t RAF_SchedulerResetMetrics(RAF_TaskId_t task_id);
esp_err_t RAF_SchedulerResetAllMetrics(void);
void RAF_SchedulerPrintStats(void);

#endif /* MAIN_SCHEDULER_H_ */
