/*
 * log_system.c
 *
 *  Created on: 8 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Mesin logika pemrosesan log murni (Pure Engine Logic),
 *             100% Non-blocking Ping-Pong Buffer dengan RTOS Task Notifier pada Core 0,
 *             Chunk Builder dengan Hardware ROM CRC32, dan Boot Recovery scanner.
 */

#include "log_system.h"
#include "data_logger.h"
#include "app_log.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "LOG_SYSTEM";

/* =========================================================================
 * VARIABEL & STATE INTERNAL ENGINE LOGGING
 * ========================================================================= */

// Alokasi Ping-Pong (Double) Buffer RAM (2 x 4096 bytes)
static uint8_t s_buffer_a[LOG_SECTOR_SIZE];
static uint8_t s_buffer_b[LOG_SECTOR_SIZE];
static uint8_t *s_active_buf = s_buffer_a;
static uint8_t *s_flush_buf  = s_buffer_b;

static size_t   s_active_offset          = sizeof(chunk_header_t);
static uint16_t s_active_record_count    = 0;
static uint32_t s_active_timestamp_start = 0;

static uint64_t s_current_write_address  = 0;
static uint32_t s_current_sequence       = 1;
static uint64_t s_total_storage_size     = 0;
static storage_backend_t *s_backend      = NULL;

// RTOS Sync & Concurrency Handlers
static SemaphoreHandle_t s_log_mutex            = NULL;
static TaskHandle_t      s_log_writer_task_handle = NULL;

/* =========================================================================
 * BACKGROUND WORKER TASK (RUNS ON CORE 0)
 * ========================================================================= */

/**
 * @brief  Task pekerja latar belakang (Core 0) yang bertugas mengeksekusi penulisan fisik
 *         ke SPI Flash / SD Card dan Pre-Erase tanpa memblokir pemanggilan push di Core 1.
 */
static void log_writer_task(void *pvParameters) {
    ESP_LOGI(TAG, "Log Background Writer Task aktif pada Core %d.", xPortGetCoreID());

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (s_backend && s_backend->write && s_flush_buf) {
            esp_err_t err = s_backend->write(s_current_write_address, s_flush_buf, LOG_SECTOR_SIZE);
            if (err == ESP_OK) {
                ESP_LOGD(TAG, "Background Write sukses @ 0x%08llX", s_current_write_address);

                s_current_write_address += LOG_SECTOR_SIZE;
                if (s_current_write_address >= s_total_storage_size) {
                    s_current_write_address = 0; // Wrap around jika melingkar
                }

                app_log_pre_erase_check(s_current_write_address);
            } else {
                ESP_LOGE(TAG, "Gagal menulis sektor Flash @ 0x%08llX (Error: %d)", s_current_write_address, err);
            }
        }
    } 
}

/**
 * @brief  Menjalankan pemulihan sektor saat sistem booting (Boot Recovery).
 *         Memindai seluruh sektor Flash untuk menemukan blok data valid terakhir.
 */
static esp_err_t log_system_boot_recovery(void) {
    if (s_backend == NULL || s_backend->read == NULL || s_total_storage_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Menjalankan Boot Recovery Scan pada media penyimpanan (%llu KB)...", 
             s_total_storage_size / 1024);

    uint64_t last_valid_addr = 0;
    uint32_t max_seq_found = 0;
    bool found_any_valid = false;
    uint8_t temp_sector[LOG_SECTOR_SIZE];

    for (uint64_t addr = 0; addr + LOG_SECTOR_SIZE <= s_total_storage_size; addr += LOG_SECTOR_SIZE) {
        chunk_header_t header;
        chunk_footer_t footer;

        if (s_backend->read(addr, &header, sizeof(header)) != ESP_OK) continue;
        if (s_backend->read(addr + LOG_SECTOR_SIZE - sizeof(footer), &footer, sizeof(footer)) != ESP_OK) continue;

        if (header.magic_start == DATALOG_CHUNK_MAGIC && footer.magic_end == DATALOG_CHUNK_END_MAGIC) {

            if (s_backend->read(addr, temp_sector, LOG_SECTOR_SIZE) == ESP_OK) {
                uint32_t data_len = LOG_SECTOR_SIZE - sizeof(chunk_footer_t);
                uint32_t calc_crc = esp_rom_crc32_le(0, temp_sector, data_len);

                if (calc_crc == footer.crc32) {
                    if (header.sequence_num >= max_seq_found) {
                        max_seq_found = header.sequence_num;
                        last_valid_addr = addr;
                        found_any_valid = true;
                    }
                } else {
                    ESP_LOGW(TAG, "Sektor 0x%08llX terkorupsi (CRC mismatch sisa blackout). Diabaikan.", addr);
                }
            }
        }
    }

    if (found_any_valid) {
        s_current_sequence = max_seq_found + 1;
        s_current_write_address = last_valid_addr + LOG_SECTOR_SIZE;
        if (s_current_write_address >= s_total_storage_size) {
            s_current_write_address = 0; // Wrap around
        }
        ESP_LOGI(TAG, "Recovery sukses: Sequence terakhir = %lu, Write Head berikutnya @ 0x%08llX", 
                 (unsigned long)max_seq_found, s_current_write_address);
    } else {
        s_current_sequence = 1;
        s_current_write_address = 0;
        ESP_LOGI(TAG, "Tidak ada data log lama (Storage baru/bersih). Memulai dari Sequence 1 @ 0x00000000.");

        if (s_backend->erase) {
            s_backend->erase(0, LOG_SECTOR_SIZE);
        }
    }

    app_log_pre_erase_check(s_current_write_address);
    return ESP_OK;
}

/**
 * @brief  Menutup blok sektor aktif, memasang header/CRC32 footer, menukar buffer,
 *         dan mengirim notifikasi asinkron ke task pekerja (100% Non-blocking).
 */
static void log_system_swap_and_notify_worker_locked(size_t record_len) {
    if (s_active_record_count == 0) return;

    chunk_header_t *header = (chunk_header_t *)s_active_buf;
    header->magic_start      = DATALOG_CHUNK_MAGIC;
    header->sequence_num     = s_current_sequence++;
    header->record_count     = s_active_record_count;
    header->record_size      = record_len;
    header->timestamp_start  = s_active_timestamp_start;

    size_t data_payload_limit = LOG_SECTOR_SIZE - sizeof(chunk_footer_t);
    if (s_active_offset < data_payload_limit) {
        memset(s_active_buf + s_active_offset, 0xFF, data_payload_limit - s_active_offset);
    }

    uint32_t calc_crc = esp_rom_crc32_le(0, s_active_buf, data_payload_limit);

    chunk_footer_t *footer = (chunk_footer_t *)(s_active_buf + data_payload_limit);
    footer->crc32     = calc_crc;
    footer->magic_end = DATALOG_CHUNK_END_MAGIC;

    s_flush_buf  = s_active_buf;
    s_active_buf = (s_active_buf == s_buffer_a) ? s_buffer_b : s_buffer_a;
    s_active_offset          = sizeof(chunk_header_t);
    s_active_record_count    = 0;
    s_active_timestamp_start = 0;

    if (s_log_writer_task_handle != NULL) {
        xTaskNotifyGive(s_log_writer_task_handle);
    }
}

/* =========================================================================
 * PUBLIC ENGINE API
 * ========================================================================= */

esp_err_t log_system_init(void) {
    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
        if (s_log_mutex == NULL) return ESP_ERR_NO_MEM;
    }

    s_backend = app_log_get_backend();
    if (s_backend == NULL) {
        ESP_LOGE(TAG, "Storage backend tidak terdaftar!");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_backend->init) {
        esp_err_t err = s_backend->init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Storage backend init gagal atau partisi belum siap.");
            return err;
        }
    }

    storage_info_t info = {0};
    if (s_backend->get_info && s_backend->get_info(&info) == ESP_OK) {
        s_total_storage_size = info.total_size;
    }

    s_active_buf             = s_buffer_a;
    s_flush_buf              = s_buffer_b;
    s_active_offset          = sizeof(chunk_header_t);
    s_active_record_count    = 0;
    s_active_timestamp_start = 0;

    log_system_boot_recovery();

    if (s_log_writer_task_handle == NULL) {
        BaseType_t res = xTaskCreatePinnedToCore(
            log_writer_task,
            "log_writer",
            4096,
            NULL,
            2,
            &s_log_writer_task_handle,
            0 // Pin ke Core 0
        );
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Gagal membuat Log Writer Task di Core 0!");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Log System Engine siap (100%% Non-blocking Ping-Pong Buffer di Core 0).");
    return ESP_OK;
}

esp_err_t log_system_push_raw_record(const void *record, size_t record_len) {
    if (record == NULL || record_len == 0) return ESP_ERR_INVALID_ARG;
    if (s_log_mutex == NULL) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t data_payload_limit = LOG_SECTOR_SIZE - sizeof(chunk_footer_t);

    if (s_active_offset + record_len > data_payload_limit) {
        log_system_swap_and_notify_worker_locked(record_len);
    }

    if (s_active_record_count == 0) {
        const device_datalog_t *sample = (const device_datalog_t *)record;
        s_active_timestamp_start = (uint32_t)(sample->timestamp * 1000.0f);
    }

    memcpy(s_active_buf + s_active_offset, record, record_len);
    s_active_offset += record_len;
    s_active_record_count++;

    xSemaphoreGive(s_log_mutex);
    return ESP_OK;
}

esp_err_t log_system_flush(void) {
    if (s_log_mutex == NULL) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_active_record_count > 0) {
        log_system_swap_and_notify_worker_locked(sizeof(device_datalog_t));
    }

    xSemaphoreGive(s_log_mutex);
    return ESP_OK;
}

uint64_t log_system_get_write_offset(void) {
    return s_current_write_address;
}

uint32_t log_system_get_sequence(void) {
    return s_current_sequence;
}


