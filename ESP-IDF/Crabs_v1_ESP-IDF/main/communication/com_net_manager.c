/*
 * com_net_manager.c
 *
 *  Alur:
 *    BOOT → coba WiFi (10s) → OK? → WIFI_ACTIVE
 *                            → gagal/timeout? → GSM PERMANEN
 *    WiFi drop kapanpun → langsung GSM PERMANEN (tidak balik WiFi)
 *
 *  Recovery:
 *    - MQTT disconnect > 120s → REBOOT ESP32
 *    - GSM init gagal → retry cepat (~35s), bukan tunggu 5 menit
 *    - GSM gagal 3x → REBOOT ESP32
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
 * Konstanta
 * ============================================================ */
#define MQTT_REBOOT_THRESHOLD_S       120    /* MQTT down >120s → reboot     */
#define GSM_INIT_RETRY_DELAY_MS       5000   /* Jeda antar retry init gagal  */

/* ============================================================
 * State
 * ============================================================ */
typedef enum {
    ST_BOOT_WIFI,
    ST_WIFI_ACTIVE,
    ST_BOOT_GSM,
    ST_GSM_ACTIVE,
    ST_GSM_WAIT,
    ST_REBOOT_PENDING,
} net_state_t;

static volatile net_state_t s_state           = ST_BOOT_WIFI;
static volatile bool        s_wifi_ok         = false;
static volatile bool        s_gsm_ok          = false;
static volatile bool        s_gsm_init_failed = false;   /* ⭐ baru */
static int64_t              s_state_enter     = 0;
static com_net_iface_t      s_active          = COM_NET_NONE;
static uint8_t              s_gsm_retry       = 0;
static uint32_t             s_boot_fails      = 0;

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
 * Helpers
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
 * Callbacks dari WiFi / GSM layer
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
static void act_on_connected(com_net_iface_t iface) {
    s_active = iface;
    s_gsm_init_failed = false;   /* ⭐ reset */

    ESP_LOGI(TAG, ">>> [%s] AKTIF — inisialisasi semua fitur <<<",
             (iface == COM_NET_WIFI) ? "WiFi" : "GSM");

    com_mqtt_init(MQTT_BROKER_URI_DEFAULT, MQTT_CLIENT_ID_DEFAULT);
    com_ota_init();

    guard_clear();
}

static void act_wifi_start(void) {
    ESP_LOGI(TAG, "> WiFi START (boot-only, %us)",
             NET_MGR_WIFI_BOOT_TIMEOUT_MS / 1000);
    s_wifi_ok = false;
    if (raf_com_wifi_init(NULL, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init gagal");
    }
}

static void act_wifi_stop(void) {
    ESP_LOGI(TAG, "> WiFi STOP (permanen)");
    com_wifi_stop();
    s_wifi_ok = false;
}

/* ⭐ act_gsm_start → return bool supaya pemanggil tahu hasil init */
static bool act_gsm_start(bool blocking_wait) {
    ESP_LOGI(TAG, "> GSM START (wait=%d)", blocking_wait);
    s_gsm_ok = false;
    s_gsm_init_failed = false;

    if (raf_com_gsm_init(NULL, NULL, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "GSM init gagal — akan retry cepat");
        s_gsm_init_failed = true;
        return false;
    }

    raf_com_gsm_prepare(GSM_IMEI_DEFAULT, "ALL_BAND");

    if (blocking_wait) {
        if (raf_com_gsm_wait_connected(NET_MGR_GSM_BOOT_TIMEOUT_MS)) {
            s_gsm_ok = true;
        }
    }
    return true;
}

static void act_gsm_stop(void) {
    ESP_LOGI(TAG, "> GSM STOP");
    raf_com_gsm_stop();
    s_gsm_ok = false;
}

static void act_reboot(const char *reason) {
    ESP_LOGE(TAG, "> REBOOT (%s)", reason);
    guard_mark_fail();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/* ============================================================
 * Monitor MQTT — kalau down terlalu lama, REBOOT
 * ============================================================ */
static void mqtt_reboot_check(void) {
    /* Skip saat OTA berjalan */
    if (com_ota_is_in_progress()) return;

    if (!com_mqtt_is_reconnecting()) return;

    int64_t disc_us = com_mqtt_last_disconnect_us();
    if (disc_us <= 0) return;

    uint32_t disc_s = (uint32_t)((esp_timer_get_time() - disc_us) / 1000000ULL);

    if (disc_s >= MQTT_REBOOT_THRESHOLD_S) {
        act_reboot("MQTT disconnect terlalu lama");
    } else if ((disc_s % 30) == 0) {
        ESP_LOGW(TAG, "MQTT down %us (reboot @%us)",
                 disc_s, MQTT_REBOOT_THRESHOLD_S);
    }
}

/* ============================================================
 * Main state machine
 * ============================================================ */
static void net_mgr_task(void *arg) {
    (void)arg;
    guard_load();

    if (s_boot_fails >= NET_MGR_REBOOT_GUARD_COUNT) {
        ESP_LOGW(TAG, "Reboot loop → skip WiFi, langsung GSM");
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

        /* ---------- BOOT WiFi: hanya 10 detik ---------- */
        case ST_BOOT_WIFI:
            if (s_wifi_ok) {
                ESP_LOGI(TAG, "WiFi OK dalam %ums", (unsigned)el);
                act_on_connected(COM_NET_WIFI);
                set_state(ST_WIFI_ACTIVE);
            } else if (el >= NET_MGR_WIFI_BOOT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "WiFi timeout %us → GSM permanen",
                         NET_MGR_WIFI_BOOT_TIMEOUT_MS / 1000);
                act_wifi_stop();
                set_state(ST_BOOT_GSM);
                act_gsm_start(true);
            }
            break;

        /* ---------- WIFI ACTIVE ---------- */
        case ST_WIFI_ACTIVE:
            if (!s_wifi_ok) {
                ESP_LOGW(TAG, "WiFi drop → GSM permanen");
                act_wifi_stop();
                set_state(ST_BOOT_GSM);
                act_gsm_start(true);
                break;
            }
            mqtt_reboot_check();
            break;

        /* ---------- BOOT GSM ---------- */
        case ST_BOOT_GSM:
            if (s_gsm_ok) {
                ESP_LOGI(TAG, "GSM OK");
                act_on_connected(COM_NET_GSM);
                s_gsm_retry = 0;
                set_state(ST_GSM_ACTIVE);
            }
            /* ⭐ Kalau init gagal ATAU timeout → retry cepat */
            else if (s_gsm_init_failed || el >= NET_MGR_GSM_BOOT_TIMEOUT_MS) {
                bool was_init_failed = s_gsm_init_failed;
                s_gsm_init_failed = false;

                s_gsm_retry++;
                ESP_LOGW(TAG, "GSM gagal (retry %u/%u) — %s",
                         s_gsm_retry, NET_MGR_MAX_GSM_RETRY,
                         was_init_failed ? "init error" : "timeout");
                act_gsm_stop();

                if (s_gsm_retry >= NET_MGR_MAX_GSM_RETRY) {
                    act_reboot("GSM gagal boot berulang");
                } else {
                    /* Kalau init error → jeda singkat sebelum retry.
                     * Kalau timeout → state timer sudah cukup, langsung retry. */
                    if (was_init_failed) {
                        vTaskDelay(pdMS_TO_TICKS(GSM_INIT_RETRY_DELAY_MS));
                    }
                    set_state(ST_BOOT_GSM);
                    act_gsm_start(true);
                }
            }
            break;

        /* ---------- GSM ACTIVE ---------- */
        case ST_GSM_ACTIVE:
            if (!s_gsm_ok) {
                ESP_LOGW(TAG, "GSM drop → tunggu %us reconnect",
                         NET_MGR_GSM_DROP_WAIT_MS / 1000);
                set_state(ST_GSM_WAIT);
                break;
            }
            mqtt_reboot_check();
            break;

        /* ---------- GSM WAIT ---------- */
        case ST_GSM_WAIT:
            if (s_gsm_ok) {
                ESP_LOGI(TAG, "GSM kembali online");
                set_state(ST_GSM_ACTIVE);
            } else if (el >= NET_MGR_GSM_DROP_WAIT_MS) {
                ESP_LOGW(TAG, "GSM tidak kembali → restart GSM");
                act_gsm_stop();
                set_state(ST_BOOT_GSM);
                act_gsm_start(true);
            }
            break;

        /* ---------- REBOOT (fallback) ---------- */
        case ST_REBOOT_PENDING:
            act_reboot("state REBOOT_PENDING");
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