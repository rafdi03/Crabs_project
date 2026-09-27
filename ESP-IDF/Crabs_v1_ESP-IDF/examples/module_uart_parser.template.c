#include "esp_err.h"
#include "driver/uart.h"
#include <stdbool.h>

static bool s_ready;

esp_err_t RAF_ExampleUartInit(void) {
    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(UART_NUM_1, &config);
    if (err != ESP_OK) return err;
    err = uart_set_pin(UART_NUM_1, 17, 16, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    s_ready = true;
    return ESP_OK;
}

void RAF_ExampleUartJob(void *arg) {
    (void)arg;
    if (!s_ready) return;

    uint8_t buffer[128];
    int length = uart_read_bytes(UART_NUM_1, buffer, sizeof(buffer), 0);
    if (length <= 0) return;

    /* TODO: parse the received frame and update your module state. */
}
