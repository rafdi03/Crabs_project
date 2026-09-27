/*
 * ringbuff_com.c
 *
 *  Created on: 7 Sept 2026
 *      Author: Rafdi
 */

#include "ringbuff_com.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

static const char *TAG = "RINGBUF_COM";
static RingbufHandle_t s_comm_ringbuf = NULL;
static ringbuf_com_stats_t s_stats = {0};

esp_err_t ringbuf_com_init(size_t buffer_size) {
    if (s_comm_ringbuf != NULL) {
        ESP_LOGW(TAG, "RingBuffer sudah pernah diinisialisasi sebelumnya.");
        return ESP_OK;
    }

    size_t size = (buffer_size == 0) ? RINGBUF_COMM_DEFAULT_SIZE : buffer_size;
    
    s_comm_ringbuf = xRingbufferCreate(size, RINGBUF_TYPE_NOSPLIT);
    if (s_comm_ringbuf == NULL) {
        ESP_LOGE(TAG, "Gagal mengalokasikan memori untuk RingBuffer (%u bytes)!", (unsigned int)size);
        return ESP_ERR_NO_MEM;
    }

    s_stats.total_sent = 0;
    s_stats.total_received = 0;
    s_stats.dropped_packets = 0;
    s_stats.free_bytes = size;

    ESP_LOGI(TAG, "RingBuffer Komunikasi aktif (%u bytes, No-Split Mode).", (unsigned int)size);
    return ESP_OK;
}

void ringbuf_com_deinit(void) {
    if (s_comm_ringbuf != NULL) {
        vRingbufferDelete(s_comm_ringbuf);
        s_comm_ringbuf = NULL;
        ESP_LOGI(TAG, "RingBuffer Komunikasi dinonaktifkan.");
    }
}

bool ringbuf_com_send(const void *data, size_t len, uint32_t wait_ms) {
    if (s_comm_ringbuf == NULL || data == NULL || len == 0) {
        return false;
    }

    TickType_t ticks = (wait_ms == 0) ? 0 : pdMS_TO_TICKS(wait_ms);
    BaseType_t res = xRingbufferSend(s_comm_ringbuf, data, len, ticks);

    if (res == pdTRUE) {
        s_stats.total_sent++;
        return true;
    } else {
        s_stats.dropped_packets++;
        return false;
    }
}

bool ringbuf_com_send_from_isr(const void *data, size_t len,
                               BaseType_t *pxHigherPriorityTaskWoken) {
    if (s_comm_ringbuf == NULL || data == NULL || len == 0) {
        return false;
    }

    BaseType_t res = xRingbufferSendFromISR(s_comm_ringbuf, data, len,
                                            pxHigherPriorityTaskWoken);
    if (res == pdTRUE) {
        __atomic_fetch_add(&s_stats.total_sent, 1, __ATOMIC_RELAXED);
        return true;
    } else {
        __atomic_fetch_add(&s_stats.dropped_packets, 1, __ATOMIC_RELAXED);
        return false;
    }
}

void* ringbuf_com_receive(size_t *item_size, uint32_t wait_ms) {
    if (s_comm_ringbuf == NULL) {
        if (item_size) *item_size = 0;
        return NULL;
    }

    TickType_t ticks = (wait_ms == 0) ? 0 : pdMS_TO_TICKS(wait_ms);
    void *item = xRingbufferReceive(s_comm_ringbuf, item_size, ticks);

    if (item != NULL) {
        s_stats.total_received++;
    }
    return item;
}

void ringbuf_com_free(void *item) {
    if (s_comm_ringbuf != NULL && item != NULL) {
        vRingbufferReturnItem(s_comm_ringbuf, item);
    }
}

ringbuf_com_stats_t ringbuf_com_get_stats(void) {
    if (s_comm_ringbuf != NULL) {
        s_stats.free_bytes = xRingbufferGetCurFreeSize(s_comm_ringbuf);
    }
    return s_stats;
}

void ringbuf_com_print_stats(void) {
    ringbuf_com_stats_t stats = ringbuf_com_get_stats();
    ESP_LOGI(TAG, "[STATISTIK] Sent: %lu | Recv: %lu | Dropped: %lu | Free: %u bytes",
             (unsigned long)stats.total_sent,
             (unsigned long)stats.total_received,
             (unsigned long)stats.dropped_packets,
             (unsigned int)stats.free_bytes);
}

uint16_t comm_crc16(const void *data, size_t len) {
    if (data == NULL || len == 0) return 0;
    return esp_rom_crc16_le(UINT16_MAX, (const uint8_t*)data, len);
}

bool comm_verify_crc16(const void *data, size_t len, uint16_t expected_crc) {
    if (data == NULL || len == 0) return false;
    uint16_t calculated = esp_rom_crc16_le(UINT16_MAX, (const uint8_t*)data, len);
    return (calculated == expected_crc);
}

uint32_t comm_crc32(const void *data, size_t len) {
    if (data == NULL || len == 0) return 0;
    return esp_rom_crc32_le(UINT32_MAX, (const uint8_t*)data, len);
}

bool comm_verify_crc32(const void *data, size_t len, uint32_t expected_crc) {
    if (data == NULL || len == 0) return false;
    uint32_t calculated = esp_rom_crc32_le(UINT32_MAX, (const uint8_t*)data, len);
    return (calculated == expected_crc);
}
