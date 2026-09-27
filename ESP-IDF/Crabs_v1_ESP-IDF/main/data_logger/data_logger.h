#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUMBER_OF_LOGDATA       7
#define DATALOG_CHUNK_MAGIC     0x55AA55AA
#define DATALOG_CHUNK_END_MAGIC 0xAA55AA55

typedef enum {
    STORAGE_MEDIA_NONE = 0,
    STORAGE_MEDIA_SPI_FLASH,
    STORAGE_MEDIA_SD_CARD
} storage_media_t;

typedef struct {
    uint64_t total_size;
    uint32_t write_block_size;
    uint32_t erase_block_size;
    bool requires_erase;
    storage_media_t type;
} storage_info_t;

typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*write)(uint64_t address, const void *data, size_t length);
    esp_err_t (*read)(uint64_t address, void *data, size_t length);
    esp_err_t (*erase)(uint64_t address, size_t length);
    esp_err_t (*sync)(void);
    esp_err_t (*get_info)(storage_info_t *info);
} storage_backend_t;

#pragma pack(push, 1)

typedef struct {
    float log_param1;
    float log_param2;
    float log_param3;
    float log_param4;
    float log_param5;
    float timestamp;
    uint32_t countTIMER;
} device_datalog_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint16_t imu_odr;
    uint16_t imu_lpf;
    float accel_scale;
    uint32_t firmware_version;
    uint32_t crc32;
    uint32_t valid_marker;
} device_config_t;

typedef struct {
    uint32_t session_id;
    uint64_t start_address;
    uint64_t end_address;
    uint32_t start_timestamp;
    uint32_t end_timestamp;
    uint8_t status;
    uint32_t crc32;
} session_header_t;

typedef struct {
    uint32_t magic_start;
    uint32_t sequence_num;
    uint16_t record_count;
    uint16_t record_size;
    uint32_t timestamp_start;
} chunk_header_t;

typedef struct {
    uint32_t crc32;
    uint32_t magic_end;
} chunk_footer_t;

#pragma pack(pop)

typedef struct {
    uint8_t index;
    const char *name;
    uint8_t name_length;
    float *data;
    const char *unit;
    uint8_t unit_length;
} datalog_param_desc_t;

esp_err_t data_logger_init(void);

esp_err_t data_logger_write_record(const device_datalog_t *data);

void Write_Datalog(void);

esp_err_t data_logger_sync(void);

void data_logger_set_reading_state(bool is_reading);

bool data_logger_is_reading(void);

esp_err_t data_logger_export_csv_header(char *out_buf, size_t max_len);

const datalog_param_desc_t* data_logger_get_schema(size_t *num_entries);

#ifdef __cplusplus
}
#endif

#endif