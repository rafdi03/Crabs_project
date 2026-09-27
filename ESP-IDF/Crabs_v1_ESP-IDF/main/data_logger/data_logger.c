/*
 * data_logger.c
 *
 *  Created on: 8 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Implementasi API publik Data Logger, definisi kamus skema metadata
 *             parameter (datalog_param_desc_t), buffer runtime, dan CSV Header Exporter.
 */

#include "data_logger.h"
#include "log_system.h"
#include "app_log.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "DATA_LOGGER";

/* =========================================================================
 * VARIABEL RUNTIME & STATUS LOGGER
 * ========================================================================= */

static bool s_is_reading_log = false;
static uint32_t s_runtime_counter = 0;

// Buffer penampung sample data aktif
static device_datalog_t Device_Datalog_buffer;

/* =========================================================================
 * KAMUS METADATA PARAMETER LOGGER (Self-Describing Schema)
 * Catatan: Engineer / Pengembang dapat mengedit nama dan satuan di sini.
 * ========================================================================= */

static const datalog_param_desc_t Logger_data[NUMBER_OF_LOGDATA] = {

    {
        .index       = 0,
        .name        = "bias_dc_acc_x",
        .name_length = sizeof("bias_dc_acc_x") - 1,
        .data        = &Device_Datalog_buffer.log_param1,
        .unit        = "m/s2",
        .unit_length = sizeof("m/s2") - 1,
    },
    {
        .index       = 1,
        .name        = "rms_noise_acc_x",
        .name_length = sizeof("rms_noise_acc_x") - 1,
        .data        = &Device_Datalog_buffer.log_param2,
        .unit        = "m/s2",
        .unit_length = sizeof("m/s2") - 1,
    },
    {
        .index       = 2,
        .name        = "vrw_acc_x",
        .name_length = sizeof("vrw_acc_x") - 1,
        .data        = &Device_Datalog_buffer.log_param3,
        .unit        = "m/s",
        .unit_length = sizeof("m/s") - 1,
    },

    // =====================================================
    // GYROSCOPE METRICS
    // =====================================================
    {
        .index       = 3,
        .name        = "bias_dc_gyro_x",
        .name_length = sizeof("bias_dc_gyro_x") - 1,
        .data        = &Device_Datalog_buffer.log_param4,
        .unit        = "rad/s",
        .unit_length = sizeof("rad/s") - 1,
    },
    {
        .index       = 4,
        .name        = "rms_noise_gyro_x",
        .name_length = sizeof("rms_noise_gyro_x") - 1,
        .data        = &Device_Datalog_buffer.log_param5,
        .unit        = "rad/s",
        .unit_length = sizeof("rad/s") - 1,
    },

    // =====================================================
    // REALTIME HARDWARE TIMESTAMP (Seconds - Float FPU)
    // =====================================================
    {
        .index       = 5,
        .name        = "timestamp",
        .name_length = sizeof("timestamp") - 1,
        .data        = &Device_Datalog_buffer.timestamp,
        .unit        = "s",
        .unit_length = sizeof("s") - 1,
    },

    // =====================================================
    // SAMPLE SEQUENCE COUNTER
    // =====================================================
    {
        .index       = 6,
        .name        = "countTIMER",
        .name_length = sizeof("countTIMER") - 1,
        .data        = (float *)&Device_Datalog_buffer.countTIMER,
        .unit        = "count",
        .unit_length = sizeof("count") - 1,
    },
};

/* =========================================================================
 * IMPLEMENTASI PUBLIC API
 * ========================================================================= */

esp_err_t data_logger_init(void) {
    ESP_LOGI(TAG, "Menginisialisasi Data Logger System...");
    return log_system_init();
}

esp_err_t data_logger_write_record(const device_datalog_t *data) {
    if (s_is_reading_log) return ESP_ERR_INVALID_STATE;
    if (data == NULL) return ESP_ERR_INVALID_ARG;

    return log_system_push_raw_record(data, sizeof(device_datalog_t));
}

void Write_Datalog(void) {
    // 1. Cek proteksi pembacaan memori
    if (s_is_reading_log) return;

    s_runtime_counter++;

    /*
     * Catatan untuk Engineer:
     * Isi variabel Device_Datalog_buffer sesuai data sensor / metrik aktif sistem Anda.
     * Contoh:
     * Device_Datalog_buffer.log_param1  = imu->metrics.bias_dc_acc_x;
     * ...
     */
    Device_Datalog_buffer.countTIMER = s_runtime_counter;
    Device_Datalog_buffer.timestamp  = (float)esp_timer_get_time() / 1000000.0f;

    data_logger_write_record(&Device_Datalog_buffer);
}

esp_err_t data_logger_sync(void) {
    return log_system_flush();
}

void data_logger_set_reading_state(bool is_reading) {
    s_is_reading_log = is_reading;
    ESP_LOGI(TAG, "Status pembacaan log diubah: %s", is_reading ? "READING (Write diproteksi)" : "IDLE (Write aktif)");
}

bool data_logger_is_reading(void) {
    return s_is_reading_log;
}

esp_err_t data_logger_export_csv_header(char *out_buf, size_t max_len) {
    if (out_buf == NULL || max_len == 0) return ESP_ERR_INVALID_ARG;

    size_t current_len = 0;
    out_buf[0] = '\0';

    for (int i = 0; i < NUMBER_OF_LOGDATA; i++) {
        char column[64];
        int written = snprintf(column, sizeof(column), "%s [%s]%s",
                               Logger_data[i].name,
                               Logger_data[i].unit,
                               (i == NUMBER_OF_LOGDATA - 1) ? "\r\n" : ",");

        if (written > 0 && (current_len + (size_t)written < max_len)) {
            strcat(out_buf, column);
            current_len += (size_t)written;
        } else {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

const datalog_param_desc_t* data_logger_get_schema(size_t *num_entries) {
    if (num_entries) {
        *num_entries = NUMBER_OF_LOGDATA;
    }
    return Logger_data;
}

