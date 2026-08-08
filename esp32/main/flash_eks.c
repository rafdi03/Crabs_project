/*
 * flash_eks.c - Modular SD Card Data Logger via SPI (FreeRTOS Background Queue Task)
 * Fitur: Non-blocking, Auto Hot-Plug Detection, Aman tanpa mengganggu Sensor / Relay / MQTT
 */

#include "flash_eks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "FLASH_EKS";

static bool is_sd_mounted = false;
static sdmmc_card_t *sd_card = NULL;
static QueueHandle_t sd_log_queue = NULL;

// Helper untuk mencoba mount SD Card secara aman
static bool try_mount_sdcard(void) {
    if (is_sd_mounted) return true;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000; // 10MHz SPI

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = SPI2_HOST;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);
    if (ret == ESP_OK) {
        is_sd_mounted = true;
        ESP_LOGI(TAG, ">>> SD Card BERHASIL Dikenali & Di-mount di %s! <<<", SD_MOUNT_POINT);

        // Buat header CSV jika file baru
        struct stat st;
        if (stat(SD_LOG_FILE, &st) != 0) {
            FILE *f = fopen(SD_LOG_FILE, "w");
            if (f != NULL) {
                fprintf(f, "Timestamp,DO_mgL,Suhu_Air_C,TDS_PPM,JSN_cm,Suhu_Udara_C,Kelembaban_pct,Relay1,Relay2,Relay3,Relay4,Relay5\n");
                fclose(f);
                ESP_LOGI(TAG, "Header CSV berhasil dibuat di %s", SD_LOG_FILE);
            }
        }
        return true;
    } else {
        is_sd_mounted = false;
        sd_card = NULL;
        return false;
    }
}

// ==========================================
// TUGAS FREERTOS LATAR BELAKANG (ASYNC WRITER)
// ==========================================
static void sd_logger_task(void *pvParameters) {
    sd_log_data_t item;
    TickType_t last_mount_attempt = 0;

    while (1) {
        // Tunggu antrean log data tanpa membebani CPU
        if (xQueueReceive(sd_log_queue, &item, portMAX_DELAY) == pdTRUE) {
            // Jika SD Card belum terpasang, coba deteksi berkala setiap 30 detik (Hot-Plug)
            if (!is_sd_mounted) {
                TickType_t now = xTaskGetTickCount();
                if ((now - last_mount_attempt) > pdMS_TO_TICKS(30000)) {
                    last_mount_attempt = now;
                    try_mount_sdcard();
                }
            }

            // Jika SD Card aktif, tulis data ke file CSV
            if (is_sd_mounted) {
                FILE *f = fopen(SD_LOG_FILE, "a");
                if (f != NULL) {
                    fprintf(f, "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
                            item.timestamp,
                            item.do_val,
                            item.suhu_air,
                            item.tds_val,
                            item.jsn_val,
                            item.suhu_lingkungan,
                            item.kelembaban,
                            item.relay1 ? 1 : 0,
                            item.relay2 ? 1 : 0,
                            item.relay3 ? 1 : 0,
                            item.relay4 ? 1 : 0,
                            item.relay5 ? 1 : 0);
                    fflush(f);
                    fclose(f);
                    ESP_LOGI(TAG, "[Data Logger] 1 Baris data tersimpan ke SD Card (%s)", item.timestamp);
                } else {
                    ESP_LOGW(TAG, "Gagal membuka file %s untuk penulisan.", SD_LOG_FILE);
                    is_sd_mounted = false; // Deteksi jika kartu dicabut tiba-tiba
                }
            }
        }
    }
}

// Inisialisasi Modul SD Card
void flash_eks_init(void) {
    ESP_LOGI(TAG, "Inisialisasi Modul SD Card Adapter SPI (CS: D%d)...", SD_CS_PIN);

    // Setup CS Pin SD Card
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << SD_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(SD_CS_PIN, 1); // Deselect SD Card secara default

    // Coba mount awal
    if (!try_mount_sdcard()) {
        ESP_LOGW(TAG, "SD Card tidak terpasang saat boot (Sistem tetap beroperasi 100%% normal tanpa SD Card).");
    }

    // Buat FreeRTOS Queue (Kapasitas 15 antrean)
    if (sd_log_queue == NULL) {
        sd_log_queue = xQueueCreate(15, sizeof(sd_log_data_t));
    }

    // Jalankan background logger task dengan priority 2 (Rendah, tidak mengganggu sensor/relay)
    xTaskCreate(sd_logger_task, "sd_logger_task", 3072, NULL, 2, NULL);
}

// Cek status apakah SD Card sedang aktif
bool flash_eks_is_mounted(void) {
    return is_sd_mounted;
}

// Fungsi non-blocking untuk mengirim data ke background logger
void flash_eks_log_async(const char *timestamp, float do_val, float suhu_air, 
                         float tds_val, float jsn_val, float suhu_lingkungan, 
                         float kelembaban, bool r1, bool r2, bool r3, bool r4, bool r5) {
    if (sd_log_queue == NULL) return;

    sd_log_data_t data;
    strncpy(data.timestamp, timestamp, sizeof(data.timestamp) - 1);
    data.timestamp[sizeof(data.timestamp) - 1] = '\0';
    data.do_val = do_val;
    data.suhu_air = suhu_air;
    data.tds_val = tds_val;
    data.jsn_val = jsn_val;
    data.suhu_lingkungan = suhu_lingkungan;
    data.kelembaban = kelembaban;
    data.relay1 = r1;
    data.relay2 = r2;
    data.relay3 = r3;
    data.relay4 = r4;
    data.relay5 = r5;

    // Kirim non-blocking (Timeout = 0) sehingga sensor task tidak pernah terhambat
    xQueueSend(sd_log_queue, &data, 0);
}
