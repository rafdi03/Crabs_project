#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

static QueueHandle_t s_events;
static bool s_ready;

esp_err_t RAF_ExampleGpioInit(void) {
    s_events = xQueueCreate(8, sizeof(uint32_t));
    if (s_events == NULL) return ESP_ERR_NO_MEM;

    s_ready = true;
    return ESP_OK;
}

void IRAM_ATTR RAF_ExampleGpioIsr(void *arg) {
    uint32_t event = (uint32_t)(uintptr_t)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendFromISR(s_events, &event, &higher_priority_task_woken);
    //if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

void RAF_ExampleGpioJob(void *arg) {
    (void)arg;
    if (!s_ready) return;

    uint32_t event;
    if (xQueueReceive(s_events, &event, 0) != pdTRUE) return;

    /* TODO: process the event outside the ISR context. */
}

/* Add this to s_modules[] in application.c:
{
    .name = "MY_GPIO_INPUT",
    .init = RAF_ExampleGpioInit,
    .isr = {
        .pin = GPIO_NUM_27,
        .handler = RAF_ExampleGpioIsr,
        .arg = (void *)(uintptr_t)1,
        .trigger = GPIO_INTR_POSEDGE,
        .enabled = true,
    },
    .task = {
        .name = "MY_GPIO_Task",
        .period_ms = 10,
        .priority = RAF_RT_PRIO_LOW,
        .core = RAF_RT_CORE_0,
        .stack_size = 3072,
        .callback = RAF_ExampleGpioJob,
        .enabled_on_boot = true,
    },
    .required = false,
},
*/
