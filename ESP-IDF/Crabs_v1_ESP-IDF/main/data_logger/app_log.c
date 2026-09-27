/*
 * app_log.c
 *
 *  Created on: 8 Sept 2026
 *      Author: Rafdi
 *
 *  Deskripsi: Implementasi driver eksekutor fisik penyimpanan (SPI Flash partition & SD Card)
 *             dengan manajemen sektor dan optimasi Pre-Erase latar belakang.
 */

#include "app_log.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "APP_LOG_BACKEND";
static const esp_partition_t *s_log_partition = NULL;
static uint32_t s_last_pre_erased_sector = UINT32_MAX;

/**
 * @brief  Inisialisasi backend penyimpanan SPI Flash melalui partisi 'log_data'.
 * @param  None
 * @retval ESP_OK jika partisi ditemukan, atau ESP_FAIL jika partisi tidak ada.
 */
static esp_err_t spi_flash_init(void) {
    s_log_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "log_data");
    
    if (s_log_partition == NULL) {
        ESP_LOGE(TAG, "Partisi 'log_data' tidak ditemukan pada partition table!");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SPI Flash Backend siap. Ukuran: %lu bytes (%lu KB)", 
             (unsigned long)s_log_partition->size, 
             (unsigned long)(s_log_partition->size / 1024));
    return ESP_OK;
}

/**
 * @brief  Menulis data byte ke alamat fisik partisi SPI Flash.
 * @param  address Alamat offset fisik partisi.
 * @param  data Pointer ke buffer data yang akan ditulis.
 * @param  length Ukuran data dalam bytes.
 * @retval ESP_OK jika penulisan berhasil, atau kode error esp_err_t jika gagal.
 */
static esp_err_t spi_flash_write(uint64_t address, const void *data, size_t length) {
    if (s_log_partition == NULL || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    
    return esp_partition_write(s_log_partition, (size_t)address, data, length);
}

/**
 * @brief  Membaca data byte dari alamat fisik partisi SPI Flash.
 * @param  address Alamat offset fisik partisi yang akan dibaca.
 * @param  data Pointer ke buffer penampung data pembacaan.
 * @param  length Ukuran data yang ingin dibaca dalam bytes.
 * @retval ESP_OK jika pembacaan berhasil, atau kode error esp_err_t jika gagal.
 */
static esp_err_t spi_flash_read(uint64_t address, void *data, size_t length) {
    if (s_log_partition == NULL || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    
    return esp_partition_read(s_log_partition, (size_t)address, data, length);
}

/**
 * @brief  Menghapus rentang sektor memori SPI Flash (Wajib kelipatan 4KB).
 * @param  address Alamat awal penghapusan sektor (harus ter-align 4KB).
 * @param  length Panjang area yang dihapus (harus kelipatan 4KB).
 * @retval ESP_OK jika penghapusan berhasil, atau kode error esp_err_t jika gagal.
 */
static esp_err_t spi_flash_erase(uint64_t address, size_t length) {
    if (s_log_partition == NULL) return ESP_FAIL;
    
    return esp_partition_erase_range(s_log_partition, (size_t)address, length);
}

/**
 * @brief  Menyinkronkan cache write buffer ke media fisik.
 * @param  None
 * @retval ESP_OK jika sinkronisasi berhasil.
 */
static esp_err_t spi_flash_sync(void) {
    return ESP_OK; 
}

/**
 * @brief  Mendapatkan informasi geometri dan batas fisik media penyimpanan SPI Flash.
 * @param  info Pointer ke struktur storage_info_t yang akan diisi.
 * @retval ESP_OK jika berhasil, atau ESP_ERR_INVALID_ARG jika pointer NULL.
 */
static esp_err_t spi_flash_get_info(storage_info_t *info) {
    if (info == NULL || s_log_partition == NULL) return ESP_ERR_INVALID_ARG;
    
    info->total_size = s_log_partition->size;
    info->write_block_size = 256;   // Ukuran satu halaman program SPI NOR Flash (Page = 256B)
    info->erase_block_size = 4096;  // Ukuran satu sektor penghapusan SPI NOR Flash (Sector = 4KB)
    info->requires_erase = true;    // Flash NOR membutuhkan proses erase sebelum penulisan
    info->type = STORAGE_MEDIA_SPI_FLASH;
    
    return ESP_OK;
}

static storage_backend_t s_spi_flash_backend = {
    .init     = spi_flash_init,
    .write    = spi_flash_write,
    .read     = spi_flash_read,
    .erase    = spi_flash_erase,
    .sync     = spi_flash_sync,
    .get_info = spi_flash_get_info
};

/**
 * @brief  Mendapatkan pointer ke struktur backend penyimpanan aktif.
 * @param  None
 * @retval Pointer ke storage_backend_t.
 */
storage_backend_t* app_log_get_backend(void) {
    return &s_spi_flash_backend;
}

/**
 * @brief  Memeriksa dan mengeksekusi penghapusan sektor berikutnya (Pre-Erase N+1) di background.
 * @param  current_address Alamat tulis aktif saat ini dalam byte.
 * @retval ESP_OK jika sektor berhasil disiapkan/dihapus, atau esp_err_t jika gagal.
 */
esp_err_t app_log_pre_erase_check(uint64_t current_address) {
    if (s_log_partition == NULL) return ESP_ERR_INVALID_STATE;

    const uint32_t sector_size = 4096;
    uint32_t current_sector = (uint32_t)(current_address / sector_size);
    uint32_t next_sector = current_sector + 1;
    uint32_t next_sector_addr = next_sector * sector_size;

    if (next_sector_addr >= s_log_partition->size) {
        next_sector = 0; // Wrap around jika melingkar (circular logging)
        next_sector_addr = 0;
    }

    if (s_last_pre_erased_sector != next_sector) {
        esp_err_t err = spi_flash_erase(next_sector_addr, sector_size);
        if (err == ESP_OK) {
            s_last_pre_erased_sector = next_sector;
            ESP_LOGD(TAG, "Pre-erased next sector %lu @ 0x%08lX", (unsigned long)next_sector, (unsigned long)next_sector_addr);
        }
        return err;
    }

    return ESP_OK;
}


