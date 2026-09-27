/*
 * com_ota.c
 *
 *  Deskripsi: HTTP-based OTA (WiFi) + URL-based OTA (WiFi/GSM via MQTT).
 */

#include "com_ota.h"
#include "com_mqtt.h"
#include "main.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "COM_OTA";
static httpd_handle_t s_httpd_server = NULL;
static volatile bool  s_ota_in_progress = false;

#define OTA_BUFF_SIZE 4096

/* =========================================================================
 * HTML Page
 * ========================================================================= */
static const char *s_ota_html_page =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 Firmware OTA Update</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#0f172a;color:#f8fafc;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:20px;}"
".card{background:#1e293b;border-radius:16px;padding:32px;width:100%;max-width:480px;box-shadow:0 10px 25px rgba(0,0,0,.5);border:1px solid #334155;}"
"h2{margin-top:0;color:#38bdf8;font-size:24px;text-align:center;}"
".info{background:#0f172a;border-radius:8px;padding:14px;margin-bottom:24px;font-size:13px;color:#94a3b8;}"
".info div{margin-bottom:6px;}"
".info div:last-child{margin-bottom:0;}"
".info span{color:#f1f5f9;font-weight:600;}"
".file-drop{border:2px dashed #475569;border-radius:12px;padding:24px;text-align:center;margin-bottom:20px;cursor:pointer;transition:.2s;}"
".file-drop:hover{border-color:#38bdf8;background:rgba(56,189,248,.05);}"
"input[type=file]{display:none;}"
".btn{background:#0284c7;color:white;border:none;padding:12px 20px;font-size:15px;font-weight:600;border-radius:8px;width:100%;cursor:pointer;transition:.2s;}"
".btn:hover{background:#0369a1;}"
".btn:disabled{background:#475569;cursor:not-allowed;}"
".progress-container{display:none;margin-top:20px;}"
".progress-bar{width:100%;height:10px;background:#334155;border-radius:5px;overflow:hidden;}"
".progress-fill{width:0%;height:100%;background:#38bdf8;transition:width .2s;}"
".status{margin-top:10px;font-size:14px;text-align:center;color:#cbd5e1;}"
"</style></head><body>"
"<div class='card'>"
"<h2>&#9889; ESP32 OTA Firmware Update</h2>"
"<div class='info'>"
"<div>System Target: <span>ESP32 Dual-Core (FreeRTOS)</span></div>"
"<div>Update Method: <span>Browser Direct Stream</span></div>"
"<div>Supported File: <span>hello_world.bin</span></div>"
"</div>"
"<div class='file-drop' onclick='document.getElementById(\"file\").click()'>"
"<div id='file-label'>&#128193; Klik untuk memilih file firmware (.bin)</div>"
"<input type='file' id='file' accept='.bin' onchange='onFileSelected()'>"
"</div>"
"<button id='upload-btn' class='btn' onclick='uploadFirmware()' disabled>Upload & Flash Firmware</button>"
"<div class='progress-container' id='prg-box'>"
"<div class='progress-bar'><div class='progress-fill' id='prg-fill'></div></div>"
"<div class='status' id='status-text'>Mengunggah: 0%</div>"
"</div>"
"</div>"
"<script>"
"var selectedFile=null;"
"function onFileSelected(){"
"var fi=document.getElementById('file');"
"if(fi.files.length>0){"
"selectedFile=fi.files[0];"
"document.getElementById('file-label').innerHTML='&#128196; '+selectedFile.name+' ('+Math.round(selectedFile.size/1024)+' KB)';"
"document.getElementById('upload-btn').disabled=false;"
"}}"
"function uploadFirmware(){"
"if(!selectedFile)return;"
"var btn=document.getElementById('upload-btn');"
"btn.disabled=true;"
"document.getElementById('prg-box').style.display='block';"
"var xhr=new XMLHttpRequest();"
"xhr.open('POST','/update',true);"
"xhr.upload.onprogress=function(e){"
"if(e.lengthComputable){"
"var pct=Math.round((e.loaded/e.total)*100);"
"document.getElementById('prg-fill').style.width=pct+'%';"
"document.getElementById('status-text').innerText='Mengunggah & Flashing: '+pct+'%';"
"}};"
"xhr.onload=function(){"
"if(xhr.status==200){"
"document.getElementById('status-text').innerHTML='<span style=\"color:#4ade80;\">&#10004; Flashing Sukses! Me-reboot ESP32...</span>';"
"setTimeout(function(){location.reload();},6000);"
"}else{"
"document.getElementById('status-text').innerHTML='<span style=\"color:#f87171;\">&#10008; Gagal: '+xhr.responseText+'</span>';"
"btn.disabled=false;"
"}};"
"xhr.onerror=function(){"
"document.getElementById('status-text').innerHTML='<span style=\"color:#f87171;\">&#10008; Error jaringan.</span>';"
"btn.disabled=false;"
"};"
"xhr.send(selectedFile);"
"}"
"</script></body></html>";

/* =========================================================================
 * HTTP HANDLERS (WiFi only — upload via browser)
 * ========================================================================= */
static esp_err_t ota_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_ota_html_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/update");
    return httpd_resp_send(req, NULL, 0);
}

static void delayed_restart_task(void *pv) {
    (void)pv;
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

static esp_err_t ota_post_handler(httpd_req_t *req) {
    esp_ota_handle_t h = 0;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA HTTP → partisi '%s' (%lu KB)", part->label,
             (unsigned long)(part->size / 1024));

    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_BUFF_SIZE);
    if (buf == NULL) {
        esp_ota_abort(h);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No mem");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int received = 0;
    bool first = true;

    while (remaining > 0) {
        int n = httpd_req_recv(req, buf,
                    (remaining > OTA_BUFF_SIZE) ? OTA_BUFF_SIZE : remaining);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf); esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv err");
            return ESP_FAIL;
        }
        if (first) {
            first = false;
            if ((uint8_t)buf[0] != 0xE9) {
                free(buf); esp_ota_abort(h);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid binary");
                return ESP_FAIL;
            }
        }
        err = esp_ota_write(h, buf, n);
        if (err != ESP_OK) {
            free(buf); esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write err");
            return ESP_FAIL;
        }
        received += n;
        remaining -= n;
    }
    free(buf);

    if ((err = esp_ota_end(h)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Verify err");
        return ESP_FAIL;
    }
    if ((err = esp_ota_set_boot_partition(part)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot err");
        return ESP_FAIL;
    }
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, ">>> OTA HTTP SUKSES (%d bytes) <<<", received);
    httpd_resp_sendstr(req, "OTA Update Sukses! Rebooting...");
    xTaskCreate(delayed_restart_task, "ota_rst", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t com_ota_init(void) {
    if (s_httpd_server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 8;
    config.stack_size        = 12288;
    config.lru_purge_enable  = true;
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

    esp_err_t ret = httpd_start(&s_httpd_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start gagal: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t uri_root = { .uri="/",       .method=HTTP_GET,  .handler=index_get_handler };
    httpd_uri_t uri_get  = { .uri="/update", .method=HTTP_GET,  .handler=ota_get_handler };
    httpd_uri_t uri_post = { .uri="/update", .method=HTTP_POST, .handler=ota_post_handler };
    httpd_register_uri_handler(s_httpd_server, &uri_root);
    httpd_register_uri_handler(s_httpd_server, &uri_get);
    httpd_register_uri_handler(s_httpd_server, &uri_post);

    ESP_LOGI(TAG, "HTTP server OTA aktif di http://<IP_ESP32>/update");
    return ESP_OK;
}

void com_ota_stop(void) {
    if (s_httpd_server != NULL) {
        httpd_stop(s_httpd_server);
        s_httpd_server = NULL;
        ESP_LOGI(TAG, "HTTP server OTA dihentikan");
    }
}

/* =========================================================================
 * URL-BASED OTA (works on WiFi & GSM, triggered via MQTT)
 * ========================================================================= */

static void ota_publish_status(const char *state, int progress, const char *msg) {
    char buf[256];
    int n;
    if (msg != NULL) {
        n = snprintf(buf, sizeof(buf),
                     "{\"state\":\"%s\",\"progress\":%d,\"msg\":\"%s\"}",
                     state, progress, msg);
    } else {
        n = snprintf(buf, sizeof(buf),
                     "{\"state\":\"%s\",\"progress\":%d}", state, progress);
    }
    if (n > 0) {
        com_mqtt_publish_raw(MQTT_TOPIC_OTA_STATUS, buf, n);
    }
}

static esp_err_t _http_event_cb(esp_http_client_event_t *evt) {
    (void)evt;
    return ESP_OK;
}

static void ota_from_url_task(void *pv) {
    char *url = (char *)pv;
    ESP_LOGW(TAG, "=== OTA URL START ===");
    ESP_LOGW(TAG, "URL: %s", url);
    ota_publish_status("started", 0, NULL);

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        ESP_LOGE(TAG, "No OTA partition");
        ota_publish_status("error", 0, "no_partition");
        goto cleanup;
    }

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .event_handler     = _http_event_cb,
        .timeout_ms        = 30000,
        .buffer_size       = 4096,
        .buffer_size_tx    = 1024,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true,   /* untuk debug — produksi: false */
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        ota_publish_status("error", 0, "http_init");
        goto cleanup;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open: %s", esp_err_to_name(err));
        ota_publish_status("error", 0, "http_open");
        esp_http_client_cleanup(client);
        goto cleanup;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status=%d, len=%d", status_code, content_len);

    if (status_code != 200 || content_len <= 0) {
        ota_publish_status("error", 0, "http_status");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto cleanup;
    }

    esp_ota_handle_t h = 0;
    err = esp_ota_begin(part, content_len, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(err));
        ota_publish_status("error", 0, "ota_begin");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        goto cleanup;
    }

    char *buf = malloc(OTA_BUFF_SIZE);
    if (buf == NULL) {
        esp_ota_abort(h);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ota_publish_status("error", 0, "no_mem");
        goto cleanup;
    }

    int total = 0;
    bool first = true;
    int timeouts = 0;

    while (total < content_len) {
        int n = esp_http_client_read(client, buf, OTA_BUFF_SIZE);
        if (n < 0) {
            if (++timeouts > 10) { ESP_LOGE(TAG, "read timeout x10"); break; }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (n == 0) {
            if (++timeouts > 20) break;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        timeouts = 0;

        if (first) {
            first = false;
            if ((uint8_t)buf[0] != 0xE9) {
                ESP_LOGE(TAG, "Bukan firmware ESP32 (magic byte salah)");
                free(buf); esp_ota_abort(h);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                ota_publish_status("error", 0, "invalid_image");
                goto cleanup;
            }
        }

        err = esp_ota_write(h, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write: %s", esp_err_to_name(err));
            free(buf); esp_ota_abort(h);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            ota_publish_status("error", 0, "ota_write");
            goto cleanup;
        }

        total += n;

        static int last_pct = -1;
        int pct = (int)((int64_t)total * 100 / content_len);
        if (pct / 5 != last_pct / 5) {
            last_pct = pct;
            ESP_LOGI(TAG, "OTA %d%% (%d/%d)", pct, total, content_len);
            ota_publish_status("downloading", pct, NULL);
        }
    }
    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total != content_len) {
        ESP_LOGE(TAG, "Incomplete: %d/%d", total, content_len);
        esp_ota_abort(h);
        ota_publish_status("error", 0, "incomplete");
        goto cleanup;
    }

    ota_publish_status("verifying", 100, NULL);
    if ((err = esp_ota_end(h)) != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(err));
        ota_publish_status("error", 100, "verify");
        goto cleanup;
    }
    if ((err = esp_ota_set_boot_partition(part)) != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition: %s", esp_err_to_name(err));
        ota_publish_status("error", 100, "set_boot");
        goto cleanup;
    }
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, ">>> OTA URL SUKSES (%d bytes) <<<", total);
    ota_publish_status("success", 100, "rebooting");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

cleanup:
    free(url);
    s_ota_in_progress = false;
    vTaskDelete(NULL);
}

void com_ota_trigger_url(const char *url) {
    if (url == NULL || url[0] == '\0') return;
    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA sedang berjalan, request diabaikan");
        ota_publish_status("busy", 0, NULL);
        return;
    }

    char *copy = strdup(url);
    if (copy == NULL) {
        ota_publish_status("error", 0, "no_mem");
        return;
    }

    s_ota_in_progress = true;
    BaseType_t res = xTaskCreate(ota_from_url_task, "ota_url",
                                 10240, copy, 5, NULL);
    if (res != pdPASS) {
        free(copy);
        s_ota_in_progress = false;
        ESP_LOGE(TAG, "Gagal buat task OTA");
        ota_publish_status("error", 0, "task_create");
    }
}

bool com_ota_is_in_progress(void) {
    return s_ota_in_progress;
}