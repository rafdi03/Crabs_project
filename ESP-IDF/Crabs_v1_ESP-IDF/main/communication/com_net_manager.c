/*
 * com_net_manager.c
 *
 *  Created on: 29 Sept 2026
 *      Author: Rafdi
 */
 #include "com_net_manager.h"
 #include "com_wifi.h"
 #include "com_GSM.h"
 #include "com_mqtt.h"
 #include "com_ota.h"
 #include "main.h"
 #include "esp_log.h"
 #include "esp_timer.h"
 #include "esp_system.h"
 #include "nvs_flash.h"
 #include "nvs.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 static const char *TAG = "NET_MGR";

 /* ============================================================
  * State definitions
  * ============================================================ */
 typedef enum {
     ST_BOOT_WIFI,
     ST_BOOT_GSM,
     ST_WIFI_ACTIVE,
     ST_GSM_ACTIVE,
     ST_WIFI_WAIT,
     ST_GSM_WAIT,
     ST_FALLBACK_GSM,
     ST_FALLBACK_WIFI,
     ST_REBOOT_PENDING,
 } net_state_t;

 static volatile net_state_t s_state       = ST_BOOT_WIFI;
 static volatile bool        s_wifi_ok     = false;
 static volatile bool        s_gsm_ok      = false;
 static volatile uint8_t     s_fb_cycles   = 0;
 static int64_t              s_state_enter = 0;
 static com_net_iface_t      s_active      = COM_NET_NONE;
 static uint32_t             s_boot_fails  = 0;

 /* ============================================================
  * NVS Reboot Guard
  * ============================================================ */
 #define NVS_NS      "net_mgr"
 #define NVS_K_CNT   "rb_cnt"
 #define NVS_K_TIME  "rb_time"

 static void guard_load(void) {
     nvs_handle_t h;
     if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

     uint32_t cnt = 0, t = 0;
     nvs_get_u32(h, NVS_K_CNT, &cnt);
     nvs_get_u32(h, NVS_K_TIME, &t);
     nvs_close(h);

     uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
     if (now - t < NET_MGR_REBOOT_GUARD_WINDOW_S) {
         s_boot_fails = cnt + 1;
         ESP_LOGW(TAG, "Reboot loop: %lu (window %us)",
                  (unsigned long)s_boot_fails, NET_MGR_REBOOT_GUARD_WINDOW_S);
     } else {
         s_boot_fails = 0;
     }
 }

 static void guard_mark_fail(void) {
     nvs_handle_t h;
     if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
     uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
     nvs_set_u32(h, NVS_K_CNT, s_boot_fails);
     nvs_set_u32(h, NVS_K_TIME, now);
     nvs_commit(h);
     nvs_close(h);
 }

 static void guard_clear(void) {
     nvs_handle_t h;
     if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
     nvs_erase_key(h, NVS_K_CNT);
     nvs_erase_key(h, NVS_K_TIME);
     nvs_commit(h);
     nvs_close(h);
 }

 /* ============================================================
  * State helpers
  * ============================================================ */
 static void set_state(net_state_t s) {
     if (s_state == s) return;
     ESP_LOGI(TAG, ">> State %d -> %d", (int)s_state, (int)s);
     s_state = s;
     s_state_enter = esp_timer_get_time();
 }

 static uint32_t state_ms(void) {
     return (uint32_t)((esp_timer_get_time() - s_state_enter) / 1000);
 }

 /* ============================================================
  * External callbacks (dipanggil dari WiFi/GSM layer)
  * ============================================================ */
 void raf_com_net_mgr_notify_wifi(bool connected) {
     bool prev = s_wifi_ok;
     s_wifi_ok = connected;
     if (connected && !prev) ESP_LOGI(TAG, "[EVT] WiFi up");
     if (!connected && prev) ESP_LOGW(TAG, "[EVT] WiFi down");
 }

 void raf_com_net_mgr_notify_gsm(bool connected) {
     bool prev = s_gsm_ok;
     s_gsm_ok = connected;
     if (connected && !prev) ESP_LOGI(TAG, "[EVT] GSM up");
     if (!connected && prev) ESP_LOGW(TAG, "[EVT] GSM down");
 }

 /* ============================================================
  * Actions
  * ============================================================ */
 static void act_wifi_start(void) {
     ESP_LOGI(TAG, "> WiFi START");
     s_wifi_ok = false;
     if (raf_com_wifi_init(NULL, NULL) != ESP_OK) {
         ESP_LOGW(TAG, "WiFi init gagal");
     }
 }

 static void act_wifi_stop(void) {
     ESP_LOGI(TAG, "> WiFi STOP");
     com_wifi_stop();
     s_wifi_ok = false;
 }

 static void act_gsm_start(bool blocking_wait) {
     ESP_LOGI(TAG, "> GSM START (wait=%d)", blocking_wait);
     s_gsm_ok = false;

     if (raf_com_gsm_init(NULL, NULL, NULL) != ESP_OK) {
         ESP_LOGW(TAG, "GSM init gagal");
         return;
     }
     raf_com_gsm_set_imei(GSM_IMEI_DEFAULT);

     if (blocking_wait) {
         if (raf_com_gsm_wait_connected(NET_MGR_BOOT_TIMEOUT_MS)) {
             s_gsm_ok = true;
         }
     }
 }

 static void act_gsm_stop(void) {
     ESP_LOGI(TAG, "> GSM STOP");
     raf_com_gsm_stop();
     s_gsm_ok = false;
 }

 static void act_mqtt_start(void) {
     ESP_LOGI(TAG, "> MQTT START");
     com_mqtt_init(MQTT_BROKER_URI_DEFAULT, MQTT_CLIENT_ID_DEFAULT);
 }

 static void act_reboot(void) {
     ESP_LOGE(TAG, "> REBOOT");
     guard_mark_fail();
     vTaskDelay(pdMS_TO_TICKS(500));
     esp_restart();
 }

 /* ============================================================
  * Main state machine task
  * ============================================================ */
 static void net_mgr_task(void *arg) {
     (void)arg;

     guard_load();

     /* ---- BOOT PHASE ---- */
     if (s_boot_fails >= NET_MGR_REBOOT_GUARD_COUNT) {
         ESP_LOGW(TAG, "Reboot loop terdeteksi → skip WiFi, langsung GSM");
         set_state(ST_BOOT_GSM);
         act_gsm_start(true);
     } else {
         set_state(ST_BOOT_WIFI);
         act_wifi_start();
     }

     while (1) {
         vTaskDelay(pdMS_TO_TICKS(NET_MGR_TICK_MS));
         uint32_t el = state_ms();

         switch (s_state) {

         /* ---------- BOOT WIFI ---------- */
         case ST_BOOT_WIFI:
             if (s_wifi_ok) {
                 s_active = COM_NET_WIFI;
                 act_mqtt_start();
                 com_ota_init();
                 guard_clear();
                 set_state(ST_WIFI_ACTIVE);
             } else if (el >= NET_MGR_BOOT_TIMEOUT_MS) {
                 ESP_LOGW(TAG, "WiFi boot timeout → GSM");
                 act_wifi_stop();
                 set_state(ST_BOOT_GSM);
                 act_gsm_start(true);
             }
             break;

         /* ---------- BOOT GSM ---------- */
         case ST_BOOT_GSM:
             if (s_gsm_ok) {
                 s_active = COM_NET_GSM;
                 act_mqtt_start();
                 set_state(ST_GSM_ACTIVE);
             } else if (el >= NET_MGR_BOOT_TIMEOUT_MS) {
                 ESP_LOGE(TAG, "GSM boot timeout → FALLBACK cycle");
                 act_gsm_stop();
                 s_fb_cycles = 0;
                 set_state(ST_FALLBACK_GSM);
                 act_gsm_start(true);
             }
             break;

         /* ---------- WIFI ACTIVE ---------- */
         case ST_WIFI_ACTIVE:
             if (!s_wifi_ok) {
                 ESP_LOGW(TAG, "WiFi drop → tunggu %us",
                          NET_MGR_RUNTIME_DROP_MS / 1000);
                 set_state(ST_WIFI_WAIT);
             }
             break;

         /* ---------- GSM ACTIVE ---------- */
         case ST_GSM_ACTIVE:
             if (!s_gsm_ok) {
                 ESP_LOGW(TAG, "GSM drop → tunggu %us",
                          NET_MGR_RUNTIME_DROP_MS / 1000);
                 set_state(ST_GSM_WAIT);
             }
             break;

         /* ---------- WIFI WAIT ---------- */
         case ST_WIFI_WAIT:
             if (s_wifi_ok) {
                 ESP_LOGI(TAG, "WiFi kembali");
                 set_state(ST_WIFI_ACTIVE);
             } else if (el >= NET_MGR_RUNTIME_DROP_MS) {
                 ESP_LOGW(TAG, "WiFi timeout → GSM");
                 act_wifi_stop();
                 set_state(ST_BOOT_GSM);
                 act_gsm_start(true);
             }
             break;

         /* ---------- GSM WAIT ---------- */
         case ST_GSM_WAIT:
             if (s_gsm_ok) {
                 ESP_LOGI(TAG, "GSM kembali");
                 set_state(ST_GSM_ACTIVE);
             } else if (el >= NET_MGR_RUNTIME_DROP_MS) {
                 ESP_LOGW(TAG, "GSM timeout → WiFi");
                 act_gsm_stop();
                 set_state(ST_BOOT_WIFI);
                 act_wifi_start();
             }
             break;

         /* ---------- FALLBACK: GSM ---------- */
         case ST_FALLBACK_GSM:
             if (s_gsm_ok) {
                 ESP_LOGI(TAG, "Fallback GSM OK");
                 s_active = COM_NET_GSM;
                 act_mqtt_start();
                 s_fb_cycles = 0;
                 set_state(ST_GSM_ACTIVE);
             } else if (el >= NET_MGR_FALLBACK_TIMEOUT_MS) {
                 ESP_LOGW(TAG, "Fallback GSM gagal → WiFi");
                 act_gsm_stop();
                 set_state(ST_FALLBACK_WIFI);
                 act_wifi_start();
             }
             break;

         /* ---------- FALLBACK: WiFi ---------- */
         case ST_FALLBACK_WIFI:
             if (s_wifi_ok) {
                 ESP_LOGI(TAG, "Fallback WiFi OK");
                 s_active = COM_NET_WIFI;
                 act_mqtt_start();
                 com_ota_init();
                 s_fb_cycles = 0;
                 set_state(ST_WIFI_ACTIVE);
             } else if (el >= NET_MGR_FALLBACK_TIMEOUT_MS) {
                 act_wifi_stop();
                 s_fb_cycles++;
                 ESP_LOGW(TAG, "Fallback cycle %u/%u",
                          s_fb_cycles, NET_MGR_FALLBACK_MAX_CYCLES);

                 if (s_fb_cycles >= NET_MGR_FALLBACK_MAX_CYCLES) {
                     set_state(ST_REBOOT_PENDING);
                 } else {
                     set_state(ST_FALLBACK_GSM);
                     act_gsm_start(true);
                 }
             }
             break;

         /* ---------- REBOOT ---------- */
         case ST_REBOOT_PENDING:
             act_reboot();
             break;
         }
     }
 }

 /* ============================================================
  * Public API
  * ============================================================ */
 esp_err_t com_net_manager_start(void) {
     BaseType_t r = xTaskCreatePinnedToCore(
         net_mgr_task, "net_mgr", 6144, NULL, 4, NULL, 1);
     return (r == pdPASS) ? ESP_OK : ESP_FAIL;
 }

 com_net_iface_t com_net_manager_get_active(void) {
     return s_active;
 }

 const char* com_net_manager_get_active_name(void) {
     switch (s_active) {
     case COM_NET_WIFI: return "WiFi";
     case COM_NET_GSM:  return "GSM";
     default:           return "NONE";
     }
 }



