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
#define NET_MGR_WIFI_BOOT_TIMEOUT_MS  10000      /* 10s — WiFi cuma awal   */
#define NET_MGR_GSM_BOOT_TIMEOUT_MS   300000     /* 5 menit — sinyal susah */
#define NET_MGR_GSM_DROP_WAIT_MS      30000      /* 30s — tunggu GSM reconn */
#define NET_MGR_TICK_MS               1000
#define NET_MGR_MAX_GSM_RETRY         3

/* Reboot guard */
#define NET_MGR_REBOOT_GUARD_COUNT    3
#define NET_MGR_REBOOT_GUARD_WINDOW_S 600

/* ============================================================
 * Public API
 * ============================================================ */
esp_err_t com_net_manager_start(void);
com_net_iface_t com_net_manager_get_active(void);
const char* com_net_manager_get_active_name(void);

void raf_com_net_mgr_notify_wifi(bool connected);
void raf_com_net_mgr_notify_gsm(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_COMMUNICATION_COM_NET_MANAGER_H_ */