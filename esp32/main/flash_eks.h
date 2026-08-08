#ifndef FLASH_EKS_H
#define FLASH_EKS_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// DEFINISI PIN MODUL SD CARD (SPI BUS)
// ==========================================
#define SD_CS_PIN      GPIO_NUM_21  // Chip Select SD Card (D21)
#define SD_MOSI_PIN    GPIO_NUM_23  // MOSI / DI (Shared dengan SDA LCD di D23)
#define SD_SCLK_PIN    GPIO_NUM_18  // SCK / CLK (Shared dengan SCL LCD di D18)
#define SD_MISO_PIN    GPIO_NUM_19  // MISO / DO (Native VSPI MISO di D19)

#define SD_MOUNT_POINT "/sdcard"
#define SD_LOG_FILE    "/sdcard/data_tambak.csv"

// Struktur Data untuk Antrean Logger Asinkron
typedef struct {
    char timestamp[32];
    float do_val;
    float suhu_air;
    float tds_val;
    float jsn_val;
    float suhu_lingkungan;
    float kelembaban;
    bool relay1;
    bool relay2;
    bool relay3;
    bool relay4;
    bool relay5;
} sd_log_data_t;

// ==========================================
// DEKLARASI FUNGSI MODULAR FLASH_EKS
// ==========================================
void flash_eks_init(void);
bool flash_eks_is_mounted(void);
void flash_eks_log_async(const char *timestamp, float do_val, float suhu_air, 
                         float tds_val, float jsn_val, float suhu_lingkungan, 
                         float kelembaban, bool r1, bool r2, bool r3, bool r4, bool r5);

#ifdef __cplusplus
}
#endif

#endif // FLASH_EKS_H
