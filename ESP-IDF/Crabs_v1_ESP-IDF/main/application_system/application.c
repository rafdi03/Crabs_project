/*
 * application.c
 *
 *  Created on: 18 Sept 2026
 *      Author: Rafdi
 */

#include "application.h"

static const char *TAG = "RAF_APPLICATION";
typedef void (*RAF_ModuleUpdate_t)(void *context);

typedef struct {
    RAF_ModuleUpdate_t update;
    void *context;
} RAF_ModuleCallbackContext_t;

static void RAF_ApplicationJobModule(void *arg) {
    RAF_ModuleCallbackContext_t *module = (RAF_ModuleCallbackContext_t *)arg;
    if (module == NULL || module->update == NULL) return;
    module->update(module->context);
}

static void update_com_dispatch(void *ctx) {
    (void)ctx;
    com_update_1ms();
}

static void update_diagnostics(void *ctx) {
    (void)ctx;
    //ringbuf_com_print_stats();
    //RAF_SchedulerPrintStats();

    static uint16_t cpu_tick = 0;
    if (++cpu_tick >= 5) {
        cpu_tick = 0;
        static char cpu_buf[1024] __attribute__((unused));
        //vTaskGetRunTimeStats(cpu_buf);
        //ESP_LOGI("CPU", "\n%s", cpu_buf);
    }

    static uint16_t mqtt_tick = 0;
    if (++mqtt_tick >= 10) {
        mqtt_tick = 0;
        send_mqtt_json();
    }
}

static void update_ds18b20(void *ctx)   { (void)ctx; ds18b20_update(); }
static void update_tds(void *ctx)       { (void)ctx; tds_update(); }
static void update_dht22(void *ctx)     { (void)ctx; dht22_update(); }
static void update_jsn(void *ctx)       { (void)ctx; jsn_update(); }
static void update_do_sensor(void *ctx) { (void)ctx; do_sensor_update(); }
static void update_relay(void *ctx)     { (void)ctx; /* event-driven */ }
//static void update_lcd(void *ctx)       { (void)ctx; lcd_update(); }

static void update_lcd(void *ctx) {
    (void)ctx;

    lcd_display_data_t d = {0};
    dht22_data_t dht = {0};

    d.suhu_air  = ds18b20_get_temp();
    d.tds_ppm   = tds_get_ppm();
    d.jarak_cm  = jsn_get_distance_cm();
    d.do_mg     = do_get_value();

    if (dht22_get_data(&dht)) {
        d.suhu_udara   = dht.temperature;
        d.lembap_udara = dht.humidity;
    }

    lcd_set_display_data(&d);
    lcd_update();
}

// ============================================================================
// Context Instances
// ============================================================================
static RAF_ModuleCallbackContext_t s_com_dispatch_context = { .update = update_com_dispatch, .context = NULL };
static RAF_ModuleCallbackContext_t s_diagnostics_context  = { .update = update_diagnostics,  .context = NULL };
static RAF_ModuleCallbackContext_t s_ds18b20_context      = { .update = update_ds18b20,      .context = NULL };
static RAF_ModuleCallbackContext_t s_tds_context          = { .update = update_tds,          .context = NULL };
static RAF_ModuleCallbackContext_t s_dht22_context        = { .update = update_dht22,        .context = NULL };
static RAF_ModuleCallbackContext_t s_jsn_context          = { .update = update_jsn,          .context = NULL };
static RAF_ModuleCallbackContext_t s_do_context           = { .update = update_do_sensor,    .context = NULL };
static RAF_ModuleCallbackContext_t s_relay_context        = { .update = update_relay,        .context = NULL };
static RAF_ModuleCallbackContext_t s_lcd_context          = { .update = update_lcd,          .context = NULL };

static esp_err_t RAF_ApplicationInitDs18b20(void) { return ds18b20_init(); }
static esp_err_t RAF_ApplicationInitTds(void)     { return tds_init(); }
static esp_err_t RAF_ApplicationInitDht22(void)   { return dht22_init(); }
static esp_err_t RAF_ApplicationInitJsn(void)     { return jsn_init(); }
static esp_err_t RAF_ApplicationInitDo(void)      { return do_sensor_init(); }
static esp_err_t RAF_ApplicationInitRelay(void)   { return relay_init(); }
static esp_err_t RAF_ApplicationInitLcd(void)     { return lcd_init(); }

static const RAF_AppModule_t s_modules[] = {

    {
        .name = "COM_Dispatch",
        .task = {
            .name            = "COM_Dispatch",
            .period_ms       = 5,
            .phase_ms        = 0,
            .deadline_ms     = 5,                 
            .priority        = RAF_RT_PRIO_HIGH,  
            .core            = RAF_RT_CORE_0,     
            .stack_size      = 5120,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_com_dispatch_context,
            .enabled_on_boot = true,
        },
        .required = true,
    },

    {
        .name = "SYS_Diag",
        .task = {
            .name            = "SYS_Diag",
            .period_ms       = 1000,
            .phase_ms        = 100,
            .deadline_ms     = 0,
            .priority        = RAF_RT_PRIO_BACKGROUND,
            .core            = RAF_RT_CORE_0,
            .stack_size      = 3072,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_diagnostics_context,
            .enabled_on_boot = true,
        },
        .required = true,
    },

    {
        .name = "DS18B20",
        .init = RAF_ApplicationInitDs18b20,
        .task = {
            .name            = "DS18B20_Poll",
            .period_ms       = 1000,
            .phase_ms        = 0,
            .deadline_ms     = 500,
            .priority        = RAF_RT_PRIO_MID,
            .core            = RAF_RT_CORE_1,
            .stack_size      = 3584,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_ds18b20_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "TDS",
        .init = RAF_ApplicationInitTds,
        .task = {
            .name            = "TDS_Poll",
            .period_ms       = 400,
            .phase_ms        = 0,
            .deadline_ms     = 200,
            .priority        = RAF_RT_PRIO_MID,
            .core            = RAF_RT_CORE_1,
            .stack_size      = 3072,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_tds_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "DHT22",
        .init = RAF_ApplicationInitDht22,
        .task = {
            .name            = "DHT22_Poll",
            .period_ms       = 2000,
            .phase_ms        = 0,
            .deadline_ms     = 0,
            .priority        = RAF_RT_PRIO_LOW,
            .core            = RAF_RT_CORE_0,    
            .stack_size      = 3072,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_dht22_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "JSN_SR04T",
        .init = RAF_ApplicationInitJsn,
        .task = {
            .name            = "JSN_Poll",
            .period_ms       = 100,
            .phase_ms        = 0,
            .deadline_ms     = 50,
            .priority        = RAF_RT_PRIO_HIGH,
            .core            = RAF_RT_CORE_1,
            .stack_size      = 3072,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_jsn_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "DO_Sensor",
        .init = RAF_ApplicationInitDo,
        .task = {
            .name            = "DO_Poll",
            .period_ms       = 400,
            .phase_ms        = 0,
            .deadline_ms     = 0,
            .priority        = RAF_RT_PRIO_LOW,
            .core            = RAF_RT_CORE_0,
            .stack_size      = 2048,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_do_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "Relay",
        .init = RAF_ApplicationInitRelay,
        .task = {
            .name            = "Relay_Poll",
            .period_ms       = 1000,
            .phase_ms        = 0,
            .deadline_ms     = 0,
            .priority        = RAF_RT_PRIO_LOW,
            .core            = RAF_RT_CORE_0,
            .stack_size      = 2048,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_relay_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },

    {
        .name = "LCD",
        .init = RAF_ApplicationInitLcd,
        .task = {
            .name            = "LCD_Refresh",
            .period_ms       = 200,
            .phase_ms        = 0,
            .deadline_ms     = 0,
            .priority        = RAF_RT_PRIO_LOW,
            .core            = RAF_RT_CORE_0,
            .stack_size      = 5120,
            .callback        = RAF_ApplicationJobModule,
            .arg             = &s_lcd_context,
            .enabled_on_boot = true,
        },
        .required = false,
    },
};

#define RAF_APP_MODULE_COUNT (sizeof(s_modules) / sizeof(s_modules[0]))

/*_Static_assert(
    RAF_APP_MODULE_COUNT == RAF_TASK_IDX_COUNT,
    "Jumlah module descriptor harus sama dengan RAF_TASK_IDX_COUNT"
);*/

static RAF_TaskId_t s_task_ids[RAF_TASK_IDX_COUNT];
static bool s_initialized = false;

static esp_err_t RAF_ApplicationInitIrqService(void) {
    static bool installed = false;
    if (installed) return ESP_OK;

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service gagal: %s", esp_err_to_name(err));
        return err;
    }

    installed = true;
    return ESP_OK;
}

static esp_err_t RAF_ApplicationRegisterModule(const RAF_AppModule_t *module,
                                               RAF_TaskId_t *out_task_id) {
    if (module == NULL || module->name == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    if (module->init != NULL) {
        err = module->init();
        if (err != ESP_OK) {
            if (module->required) {
                ESP_LOGE(TAG, "[%s] init fatal: %s", module->name, esp_err_to_name(err));
                return err;
            }
            ESP_LOGW(TAG, "[%s] init gagal, dilewati: %s", module->name, esp_err_to_name(err));
            return ESP_OK;
        }
        ESP_LOGI(TAG, "[%s] init OK", module->name);
    }

    if (module->isr.enabled && module->isr.handler != NULL) {
        err = RAF_ApplicationInitIrqService();
        if (err != ESP_OK) return err;

        gpio_config_t gpio_cfg = {
            .pin_bit_mask = (1ULL << module->isr.pin),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = module->isr.trigger,
        };
        err = gpio_config(&gpio_cfg);
        if (err != ESP_OK) return err;

        err = gpio_isr_handler_add(module->isr.pin, module->isr.handler, module->isr.arg);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "[%s] ISR terpasang di GPIO %d", module->name, module->isr.pin);
    }

    if (module->task.callback != NULL) {
        if (module->task.name == NULL || module->task.name[0] == '\0') {
            ESP_LOGE(TAG, "[%s] task.name kosong", module->name);
            return ESP_ERR_INVALID_ARG;
        }

        RAF_TaskConfig_t task_config = {
            .period_ms       = module->task.period_ms,
            .phase_ms        = module->task.phase_ms,
            .deadline_ms     = module->task.deadline_ms,
            .priority        = module->task.priority,
            .core            = module->task.core,
            .stack_size      = module->task.stack_size,
            .callback        = module->task.callback,
            .arg             = module->task.arg,
            .enabled_on_boot = module->task.enabled_on_boot,
        };

        strncpy(task_config.name, module->task.name, RAF_RT_TASK_NAME_MAX_LEN - 1);
        task_config.name[RAF_RT_TASK_NAME_MAX_LEN - 1] = '\0';

        err = RAF_SchedulerRegisterTask(&task_config, out_task_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[%s] register task gagal: %s", module->name, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "[%s] task '%s' terdaftar", module->name, module->task.name);
    }

    return ESP_OK;
}

esp_err_t RAF_ApplicationInit(void) {
    if (s_initialized) return ESP_OK;

    esp_err_t err = ringbuf_com_init(RINGBUF_COMM_DEFAULT_SIZE);
    if (err != ESP_OK) return err;

    err = com_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "COM Hub gagal init: %s", esp_err_to_name(err));
    }

    iot_response_init();

	err = com_network_start();
	if (err != ESP_OK) {
	    ESP_LOGW(TAG, "Network start gagal: %s", esp_err_to_name(err));
	}

    for (size_t i = 0; i < RAF_TASK_IDX_COUNT; i++) {
        s_task_ids[i] = RAF_RT_INVALID_TASK_ID;
    }

    ESP_LOGI(TAG, "Registering %zu module(s)...", RAF_APP_MODULE_COUNT);
    for (size_t i = 0; i < RAF_APP_MODULE_COUNT; i++) {
        err = RAF_ApplicationRegisterModule(&s_modules[i], &s_task_ids[i]);
        if (err != ESP_OK) {
            if (s_modules[i].required) return err;
            ESP_LOGW(TAG, "Modul opsional [%s] tidak aktif", s_modules[i].name);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Application siap. %zu module diproses.", RAF_APP_MODULE_COUNT);
    return ESP_OK;
}

RAF_TaskId_t RAF_TaskRegistryGetId(RAF_TaskIndex_t idx) {
    if (idx >= RAF_TASK_IDX_COUNT) return RAF_RT_INVALID_TASK_ID;
    return s_task_ids[idx];
}
