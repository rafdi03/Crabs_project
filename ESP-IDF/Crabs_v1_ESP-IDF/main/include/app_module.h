/*
 * raf_config.h
 *
 *  Created on: 19 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_APP_MODULE_H_
#define MAIN_INCLUDE_APP_MODULE_H_

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "Scheduler.h"

/**
 * GPIO interrupt registration for an application module.
 *
 * ISR handlers must be declared with IRAM_ATTR, remain short and
 * non-blocking, and use ISR-safe queues or ring buffers for handoff.
 */
typedef struct {
    gpio_num_t pin;
    gpio_isr_t handler;
    void *arg;
    gpio_int_type_t trigger;
    bool enabled;
} RAF_AppIsrConfig_t;

typedef struct {
    const char *name;
    uint32_t period_ms;
    uint32_t phase_ms;
    uint32_t deadline_ms;
    RAF_TaskPriority_t priority;
    RAF_TaskCore_t core;
    uint32_t stack_size;
    RAF_TaskCallback_t callback;
    void *arg;
    bool enabled_on_boot;
} RAF_AppTaskConfig_t;

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    void (*deinit)(void);
    RAF_AppIsrConfig_t isr;
    RAF_AppTaskConfig_t task;
    bool required;
} RAF_AppModule_t;

#endif /* MAIN_INCLUDE_APP_MODULE_H_ */
