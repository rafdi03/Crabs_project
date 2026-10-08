/*
 * Scheduler.c
 *
 *  Created on: 4 Sept 2026
 *      Author: Rafdi
 */

#include "Scheduler.h"


static const char *TAG = "SCHEDULER";

typedef struct {
    RAF_TaskId_t id;
    RAF_TaskConfig_t config;
    TaskHandle_t free_rtos_handle;

    // Internal counters & Profiling
    volatile bool enabled;
    uint32_t execution_count;
    uint32_t execution_overruns;
    uint32_t deadline_misses;
	
	uint32_t missed_periods;

    uint32_t min_exec_us;
    uint32_t max_exec_us;
    uint64_t total_exec_us;
    uint32_t last_exec_us;
    uint32_t max_jitter_us;

    SemaphoreHandle_t lock; // Thread-safe snapshot isolation
    bool is_used;
} RAF_TaskSlot_t;

static RAF_TaskSlot_t s_task_slots[RAF_RT_MAX_TASKS];
static size_t s_task_count = 0;
static RAF_SchedulerState_t s_scheduler_state = RAF_RT_STATE_UNINITIALIZED;
static SemaphoreHandle_t s_registry_lock = NULL;

static esp_err_t init_task_watchdog(uint32_t timeout_ms) {
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = 0,
        .trigger_panic = true
    };

    esp_err_t err = esp_task_wdt_reconfigure(&twdt_config);
    if (err != ESP_OK) {
        err = esp_task_wdt_init(&twdt_config);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WDT dikonfigurasi: timeout %lu ms", (unsigned long)timeout_ms);
    } else {
        ESP_LOGW(TAG, "WDT gagal dikonfigurasi: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t RAF_SchedulerValidateConfig(const RAF_TaskConfig_t *config) {
    if (config == NULL || config->callback == NULL) {
        ESP_LOGE(TAG, "Config Invalid: config atau callback NULL");
        return ESP_ERR_INVALID_ARG;
    }
	if (config->name[0] == '\0') {
	    ESP_LOGE(TAG, "Config Invalid: name kosong");
	    return ESP_ERR_INVALID_ARG;
	}
    if (config->period_ms == 0) {
        ESP_LOGE(TAG, "Config Invalid: period_ms tidak boleh 0");
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t period_ticks = pdMS_TO_TICKS(config->period_ms);
    if (period_ticks == 0) {
        ESP_LOGE(TAG, "Config Invalid: period_ms %lu terlalu kecil untuk tick HZ %d",
                 (unsigned long)config->period_ms, configTICK_RATE_HZ);
        return ESP_ERR_INVALID_ARG;
    }

    // Peringatan jika period tidak presisi di tick rate saat ini
    uint32_t actual_ms = (uint32_t)((period_ticks * 1000U) / configTICK_RATE_HZ);
    if (actual_ms != config->period_ms) {
        ESP_LOGW(TAG, "period_ms %lu tidak presisi, aktual %lu ms (tick %d Hz)",
                 (unsigned long)config->period_ms,
                 (unsigned long)actual_ms,
                 configTICK_RATE_HZ);
    }

    if (config->deadline_ms > 0 && config->deadline_ms > config->period_ms) {
        ESP_LOGE(TAG, "Config Invalid: deadline_ms (%lu) > period_ms (%lu)",
                 (unsigned long)config->deadline_ms,
                 (unsigned long)config->period_ms);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->priority < RAF_RT_PRIO_BACKGROUND || config->priority > RAF_RT_PRIO_REALTIME) {
        ESP_LOGE(TAG, "Config Invalid: priority %d di luar range", config->priority);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->stack_size > 0 && config->stack_size < 2048) {
        ESP_LOGE(TAG, "Config Invalid: stack_size %lu terlalu kecil",
                 (unsigned long)config->stack_size);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->core != RAF_RT_CORE_ANY &&
        config->core != RAF_RT_CORE_0 &&
        config->core != RAF_RT_CORE_1) {
        ESP_LOGE(TAG, "Config Invalid: core %d tidak valid", config->core);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void RAF_SchedulerTaskWrapper(void *pvParameters) {
    RAF_TaskSlot_t *slot = (RAF_TaskSlot_t *)pvParameters;

    if (slot->config.phase_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(slot->config.phase_ms));
    }

	#if RAF_CONFIG_WDT_ENABLED
	    esp_task_wdt_add(NULL);
	#endif

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(slot->config.period_ms);
    const int64_t period_us = (int64_t)slot->config.period_ms * 1000LL;

    int64_t expected_us = esp_timer_get_time();
    bool was_enabled = false;

    while (1) {
        bool en;
        xSemaphoreTake(slot->lock, portMAX_DELAY);
        en = slot->enabled;
        xSemaphoreGive(slot->lock);

        if (en && !was_enabled) {
            expected_us = esp_timer_get_time();
            xLastWakeTime = xTaskGetTickCount();
            was_enabled = true;
        } else if (!en) {
            expected_us = esp_timer_get_time();
            was_enabled = false;
        }

        if (en) {
            int64_t start_time_us = esp_timer_get_time();

            slot->config.callback(slot->config.arg);

            int64_t end_time_us = esp_timer_get_time();
            uint32_t exec_duration_us = (uint32_t)(end_time_us - start_time_us);

            // Jitter: start aktual vs expected (keduanya dari esp_timer)
            int64_t jitter_us = start_time_us - expected_us;
            if (jitter_us < 0) jitter_us = -jitter_us;
            if (jitter_us > (int64_t)slot->max_jitter_us) {
                slot->max_jitter_us = (uint32_t)jitter_us;
            }

            xSemaphoreTake(slot->lock, portMAX_DELAY);

            slot->execution_count++;
            slot->last_exec_us = exec_duration_us;
            slot->total_exec_us += exec_duration_us;

            if (exec_duration_us < slot->min_exec_us) slot->min_exec_us = exec_duration_us;
            if (exec_duration_us > slot->max_exec_us) slot->max_exec_us = exec_duration_us;

            if (slot->config.deadline_ms > 0) {
                uint64_t deadline_us = (uint64_t)slot->config.deadline_ms * 1000ULL;
                if (exec_duration_us > deadline_us) {
                    slot->execution_overruns++;
                    slot->deadline_misses++;
                }
            }

            xSemaphoreGive(slot->lock);
        }

		#if RAF_CONFIG_WDT_ENABLED
		        esp_task_wdt_reset();
		#endif

        TickType_t tick_before = xLastWakeTime;
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        if (en) {
            expected_us += period_us;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - tick_before;
        if (elapsed > (xFrequency + (xFrequency / 2))) {
            uint32_t missed = (elapsed - xFrequency) / xFrequency;
            if (missed > 0) {
                xSemaphoreTake(slot->lock, portMAX_DELAY);
                slot->missed_periods += missed;
                xSemaphoreGive(slot->lock);
            }
        }
    }
}

esp_err_t RAF_SchedulerInit(void) {
    if (s_scheduler_state != RAF_RT_STATE_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_task_slots, 0, sizeof(s_task_slots));
    s_task_count = 0;

    s_registry_lock = xSemaphoreCreateMutex();
    if (s_registry_lock == NULL) {
        s_scheduler_state = RAF_RT_STATE_ERROR;
        ESP_LOGE(TAG, "Gagal membuat registry mutex");
        return ESP_ERR_NO_MEM;
    }

	#if RAF_CONFIG_WDT_ENABLED
	    esp_err_t err = init_task_watchdog(RAF_CONFIG_WDT_TIMEOUT_MS);   // ← CONFIG
	    if (err != ESP_OK) {
	        vSemaphoreDelete(s_registry_lock);
	        s_registry_lock = NULL;
	        s_scheduler_state = RAF_RT_STATE_ERROR;
	        ESP_LOGE(TAG, "Gagal init WDT: %s", esp_err_to_name(err));
	        return err;
	    }
	#else
	    ESP_LOGI(TAG, "WDT dinonaktifkan (RAF_CONFIG_WDT_ENABLED=0)");
	#endif

    s_scheduler_state = RAF_RT_STATE_INITIALIZED;
    ESP_LOGI(TAG, "RTOS Scheduler Engine initialized.");
    return ESP_OK;
}

esp_err_t RAF_SchedulerRegisterTask(const RAF_TaskConfig_t *config, RAF_TaskId_t *out_task_id) {
    if (s_scheduler_state != RAF_RT_STATE_INITIALIZED &&
        s_scheduler_state != RAF_RT_STATE_REGISTERING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = RAF_SchedulerValidateConfig(config);
    if (err != ESP_OK) return err;

    xSemaphoreTake(s_registry_lock, portMAX_DELAY);

    if (s_task_count >= RAF_RT_MAX_TASKS) {
        xSemaphoreGive(s_registry_lock);
        return ESP_ERR_NO_MEM;
    }

    RAF_TaskSlot_t *slot = &s_task_slots[s_task_count];
    memset(slot, 0, sizeof(RAF_TaskSlot_t));

    memcpy(&slot->config, config, sizeof(RAF_TaskConfig_t));

    slot->config.name[RAF_RT_TASK_NAME_MAX_LEN - 1] = '\0';

    slot->id = (RAF_TaskId_t)(s_task_count + 1);
    slot->enabled = config->enabled_on_boot;
    slot->min_exec_us = UINT32_MAX;

    slot->lock = xSemaphoreCreateMutex();
    if (slot->lock == NULL) {
        memset(slot, 0, sizeof(RAF_TaskSlot_t));
        xSemaphoreGive(s_registry_lock);
        return ESP_ERR_NO_MEM;
    }

    slot->is_used = true;
    if (out_task_id) *out_task_id = slot->id;

    s_task_count++;
    s_scheduler_state = RAF_RT_STATE_REGISTERING;

    xSemaphoreGive(s_registry_lock);

    ESP_LOGI(TAG, "Task Registered -> ID: %u | Name: %s | Period: %lu ms | Phase: %lu ms",
             slot->id, slot->config.name,
             (unsigned long)slot->config.period_ms,
             (unsigned long)slot->config.phase_ms);

    return ESP_OK;
}

esp_err_t RAF_SchedulerStart(void) {
    if (s_scheduler_state != RAF_RT_STATE_REGISTERING &&
        s_scheduler_state != RAF_RT_STATE_INITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
	
	#if RAF_CONFIG_WDT_ENABLED
	    uint32_t max_period = 0;
	    for (size_t i = 0; i < s_task_count; i++) {
	        if (s_task_slots[i].config.period_ms > max_period) {
	            max_period = s_task_slots[i].config.period_ms;
	        }
	    }

	    uint32_t wdt_timeout = RAF_CONFIG_WDT_TIMEOUT_MS;

	#if RAF_CONFIG_WDT_AUTO_EXPAND
	    uint32_t min_required = max_period + RAF_CONFIG_WDT_GRACE_MARGIN_MS;
	    if (wdt_timeout < min_required) {
	        ESP_LOGW(TAG, "WDT auto-expand: %lu -> %lu ms (max task period %lu ms)",
	                 (unsigned long)wdt_timeout,
	                 (unsigned long)min_required,
	                 (unsigned long)max_period);
	        wdt_timeout = min_required;
	    }
	#else
	    if (max_period + RAF_CONFIG_WDT_GRACE_MARGIN_MS > wdt_timeout) {
	        ESP_LOGW(TAG, "PERINGATAN: max task period %lu ms akan trigger WDT timeout "
	                 "%lu ms. Kurangi period atau aktifkan RAF_CONFIG_WDT_AUTO_EXPAND.",
	                 (unsigned long)max_period,
	                 (unsigned long)wdt_timeout);
	    }
	#endif

	    esp_err_t wdt_err = init_task_watchdog(wdt_timeout);
	    if (wdt_err != ESP_OK) {
	        s_scheduler_state = RAF_RT_STATE_ERROR;
	        ESP_LOGE(TAG, "Gagal menerapkan WDT timeout %lu ms: %s",
	                 (unsigned long)wdt_timeout, esp_err_to_name(wdt_err));
	        return wdt_err;
	    }
	#endif

    xSemaphoreTake(s_registry_lock, portMAX_DELAY);

    size_t created = 0;
    for (size_t i = 0; i < s_task_count; i++) {
        RAF_TaskSlot_t *slot = &s_task_slots[i];
        uint32_t stack = (slot->config.stack_size > 0) ? slot->config.stack_size : 3072;
        BaseType_t core_affinity = (slot->config.core == RAF_RT_CORE_ANY)
                                   ? tskNO_AFFINITY
                                   : (BaseType_t)slot->config.core;

        BaseType_t res = xTaskCreatePinnedToCore(
            RAF_SchedulerTaskWrapper,
            slot->config.name,
            stack,
            (void *)slot,
            slot->config.priority,
            &slot->free_rtos_handle,
            core_affinity);

        if (res != pdPASS) {
            ESP_LOGE(TAG, "Gagal create task: %s", slot->config.name);
            // Rollback
            for (size_t j = 0; j < created; j++) {
                if (s_task_slots[j].free_rtos_handle) {
                    vTaskDelete(s_task_slots[j].free_rtos_handle);
                    s_task_slots[j].free_rtos_handle = NULL;
                }
            }
            s_scheduler_state = RAF_RT_STATE_ERROR;
            xSemaphoreGive(s_registry_lock);
            return ESP_FAIL;
        }
        created++;
    }

    s_scheduler_state = RAF_RT_STATE_RUNNING;
    xSemaphoreGive(s_registry_lock);

    ESP_LOGI(TAG, "Scheduler RUNNING dengan %zu task aktif.", s_task_count);
    return ESP_OK;
}

RAF_SchedulerState_t RAF_SchedulerGetState(void) {
    return s_scheduler_state;
}

static RAF_TaskId_t RAF_SchedulerFindTaskIdByName(const char *task_name) {
    if (task_name == NULL || task_name[0] == '\0') {
        return RAF_RT_INVALID_TASK_ID;
    }

    for (size_t i = 0; i < s_task_count; i++) {
        if (strncmp(s_task_slots[i].config.name, task_name,
                    RAF_RT_TASK_NAME_MAX_LEN) == 0) {
            return s_task_slots[i].id;
        }
    }

    return RAF_RT_INVALID_TASK_ID;
}

esp_err_t RAF_TaskEnable(RAF_TaskId_t task_id) {
    if (task_id == 0 || task_id > s_task_count) return ESP_ERR_INVALID_ARG;
    RAF_TaskSlot_t *slot = &s_task_slots[task_id - 1];
    if (slot->lock == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(slot->lock, portMAX_DELAY);
    slot->enabled = true;
    xSemaphoreGive(slot->lock);
    return ESP_OK;
}

esp_err_t RAF_TaskDisable(RAF_TaskId_t task_id) {
    if (task_id == 0 || task_id > s_task_count) return ESP_ERR_INVALID_ARG;
    RAF_TaskSlot_t *slot = &s_task_slots[task_id - 1];
    if (slot->lock == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(slot->lock, portMAX_DELAY);
    slot->enabled = false;
    xSemaphoreGive(slot->lock);
    return ESP_OK;
}

esp_err_t RAF_TaskEnableByName(const char *task_name) {
    RAF_TaskId_t task_id = RAF_SchedulerFindTaskIdByName(task_name);
    if (task_id == RAF_RT_INVALID_TASK_ID) return ESP_ERR_NOT_FOUND;
    return RAF_TaskEnable(task_id);
}

esp_err_t RAF_TaskDisableByName(const char *task_name) {
    RAF_TaskId_t task_id = RAF_SchedulerFindTaskIdByName(task_name);
    if (task_id == RAF_RT_INVALID_TASK_ID) return ESP_ERR_NOT_FOUND;
    return RAF_TaskDisable(task_id);
}

esp_err_t RAF_TaskGetStats(RAF_TaskId_t task_id, RAF_TaskStats_t *out_stats) {
    if (task_id == 0 || task_id > s_task_count || out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    RAF_TaskSlot_t *slot = &s_task_slots[task_id - 1];
    if (slot->lock == NULL) return ESP_ERR_INVALID_STATE;

    memset(out_stats, 0, sizeof(*out_stats));

    xSemaphoreTake(slot->lock, portMAX_DELAY);

    out_stats->id = slot->id;
    strncpy(out_stats->name, slot->config.name, RAF_RT_TASK_NAME_MAX_LEN - 1);
    out_stats->name[RAF_RT_TASK_NAME_MAX_LEN - 1] = '\0';
    out_stats->enabled = slot->enabled;
    out_stats->execution_count = slot->execution_count;
    out_stats->execution_overruns = slot->execution_overruns;
    out_stats->deadline_misses = slot->deadline_misses;
    out_stats->missed_periods = slot->missed_periods;
    out_stats->min_exec_us = (slot->min_exec_us == UINT32_MAX) ? 0 : slot->min_exec_us;
    out_stats->max_exec_us = slot->max_exec_us;
    out_stats->last_exec_us = slot->last_exec_us;
    out_stats->avg_exec_us = (slot->execution_count > 0)
        ? (uint32_t)(slot->total_exec_us / slot->execution_count) : 0;
    out_stats->max_jitter_us = slot->max_jitter_us;
    out_stats->stack_high_water = (slot->free_rtos_handle != NULL)
        ? (uint32_t)uxTaskGetStackHighWaterMark(slot->free_rtos_handle) : 0;

    xSemaphoreGive(slot->lock);
    return ESP_OK;
}

esp_err_t RAF_TaskGetStatsByName(const char *task_name, RAF_TaskStats_t *out_stats) {
    RAF_TaskId_t task_id = RAF_SchedulerFindTaskIdByName(task_name);
    if (task_id == RAF_RT_INVALID_TASK_ID) return ESP_ERR_NOT_FOUND;
    return RAF_TaskGetStats(task_id, out_stats);
}

esp_err_t RAF_SchedulerResetMetrics(RAF_TaskId_t task_id) {
    if (task_id == 0 || task_id > s_task_count) return ESP_ERR_INVALID_ARG;

    RAF_TaskSlot_t *slot = &s_task_slots[task_id - 1];
    if (slot->lock == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(slot->lock, portMAX_DELAY);
    slot->execution_count = 0;
    slot->execution_overruns = 0;
    slot->deadline_misses = 0;
    slot->missed_periods = 0;
    slot->min_exec_us = UINT32_MAX;
    slot->max_exec_us = 0;
    slot->total_exec_us = 0;
    slot->last_exec_us = 0;
    slot->max_jitter_us = 0;
    xSemaphoreGive(slot->lock);

    return ESP_OK;
}

esp_err_t RAF_SchedulerResetAllMetrics(void) {
    for (size_t i = 0; i < s_task_count; i++) {
        esp_err_t err = RAF_SchedulerResetMetrics((RAF_TaskId_t)(i + 1));
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

void RAF_SchedulerPrintStats(void) {
    ESP_LOGI(TAG, "=================================== INDUSTRIAL RTOS METRICS SNAPSHOT ===================================");
    ESP_LOGI(TAG, "%-3s | %-10s | %-3s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s",
             "ID", "Name", "En", "Min(us)", "Avg(us)", "Max(us)",
             "Jitter", "Overrun", "Deadline", "Missed", "StackHW");
    ESP_LOGI(TAG, "--------------------------------------------------------------------------------------------------------");

    for (size_t i = 0; i < s_task_count; i++) {
        RAF_TaskStats_t st;
        if (RAF_TaskGetStats((RAF_TaskId_t)(i + 1), &st) == ESP_OK) {
            ESP_LOGI(TAG, "%-3" PRIu32 " | %-10s | %-3s | %-8" PRIu32
                          " | %-8" PRIu32 " | %-8" PRIu32
                          " | %-8" PRIu32 " | %-8" PRIu32 " | %-8" PRIu32
                         " | %-8" PRIu32 " | %-8" PRIu32,
			         st.id, st.name, st.enabled ? "Y" : "N",
			         st.min_exec_us, st.avg_exec_us, st.max_exec_us,
                     st.max_jitter_us, st.execution_overruns,
                         st.deadline_misses, st.missed_periods,
                         st.stack_high_water);
        }
    }
    ESP_LOGI(TAG, "================================================================================------------------------");
}
