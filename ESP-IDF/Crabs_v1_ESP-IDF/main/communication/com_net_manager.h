/*
 * com_net_manager.h
 *
 *  Created on: 29 Sept 2026
 *      Author: Rafdi
 *
 * Network Manager — State machine dengan WiFi-priority, GSM fallback,
 * runtime monitoring, dan auto-reboot saat kedua network down.
 */

#ifndef MAIN_COMMUNICATION_COM_NET_MANAGER_H_
#define MAIN_COMMUNICATION_COM_NET_MANAGER_H_

#include "esp_err.h"
#include "COM.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Timing Configuration
 * ============================================================ */
#define NET_MGR_BOOT_TIMEOUT_MS       20000   /* 30s: boot coba WiFi/GSM     */
#define NET_MGR_RUNTIME_DROP_MS       20000   /* 30s: tunggu reconnect runtime */
#define NET_MGR_FALLBACK_TIMEOUT_MS   20000   /* 20s: per attempt fallback    */
#define NET_MGR_FALLBACK_MAX_CYCLES   2       /* 5 cycles → reboot            */
#define NET_MGR_TICK_MS               1000    /* Polling interval 1s          */

/* Reboot guard — hindari reboot loop */
#define NET_MGR_REBOOT_GUARD_COUNT    3       /* 3 reboot dalam window → skip WiFi */
#define NET_MGR_REBOOT_GUARD_WINDOW_S 600     /* 10 menit                     */

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * @brief Start network manager task.
 *        Menggantikan com_network_start() lama.
 */
esp_err_t com_net_manager_start(void);

/**
 * @brief Query active network interface.
 */
com_net_iface_t com_net_manager_get_active(void);

/**
 * @brief Query active network name: "WiFi" / "GSM" / "NONE".
 */
const char* com_net_manager_get_active_name(void);

/**
 * @brief Callback dari WiFi layer saat status koneksi berubah.
 *        Dipanggil dari com_wifi.c event handler.
 */
void raf_com_net_mgr_notify_wifi(bool connected);

/**
 * @brief Callback dari GSM layer saat status koneksi berubah.
 *        Dipanggil dari com_GSM.c setelah IP didapat.
 */
void raf_com_net_mgr_notify_gsm(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_COM_NET_MANAGER_H_ */