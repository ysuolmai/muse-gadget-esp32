/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "provision_portal.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "identity.h"
#include "muse_state.h"
#include "muse_voice.h"
#include "wifi_known.h"
#include "wifi_mgr.h"

static const char *TAG = "muse.portal";

#define PORTAL_SSID_MAX 32
#define PORTAL_PASSWORD_MAX 64
#define PORTAL_TOKEN_MAX (IDENTITY_SDK_TOKEN_LEN + 1)
#define PORTAL_FORM_MAX 2048
#define PORTAL_AP_IP "192.168.4.1"

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static atomic_bool s_start_requested;
static char s_ap_ssid[PORTAL_SSID_MAX + 1];
static char s_ap_password[PORTAL_PASSWORD_MAX + 1];

static void wipe(void *data, size_t len)
{
    volatile unsigned char *p = data;
    while (len--) *p++ = 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode one application/x-www-form-urlencoded field without allocating. */
static bool form_value(const char *body, const char *name,
                       char *out, size_t cap, bool *present)
{
    size_t name_len = strlen(name);
    if (present) *present = false;
    if (!body || !out || cap == 0) return false;
    out[0] = '\0';

    const char *p = body;
    while (*p) {
        const char *key = p;
        const char *eq = strchr(key, '=');
        if (!eq) break;
        const char *end = strchr(eq + 1, '&');
        if (!end) end = eq + 1 + strlen(eq + 1);
        if ((size_t)(eq - key) == name_len
            && strncmp(key, name, name_len) == 0) {
            if (present) *present = true;
            size_t n = 0;
            for (const char *q = eq + 1; q < end; q++) {
                char decoded;
                if (*q == '+') {
                    decoded = ' ';
                } else if (*q == '%' && q + 2 < end) {
                    int hi = hex_digit(q[1]);
                    int lo = hex_digit(q[2]);
                    if (hi < 0 || lo < 0 || n + 1 >= cap) return false;
                    decoded = (char)((hi << 4) | lo);
                    q += 2;
                } else if (*q == '%') {
                    return false;
                } else {
                    decoded = *q;
                }
                if (n + 1 >= cap || decoded == '\0'
                    || (unsigned char)decoded < 0x20
                    || (unsigned char)decoded == 0x7f) return false;
                out[n++] = decoded;
            }
            out[n] = '\0';
            return true;
        }
        p = *end ? end + 1 : end;
    }
    return true;
}

static esp_err_t send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_error_page(httpd_req_t *req, const char *message)
{
    char page[1800];
    int n = snprintf(page, sizeof(page),
        "<!doctype html><meta charset=utf-8><meta name=viewport "
        "content='width=device-width,initial-scale=1'><title>Muse 配网</title>"
        "<style>body{font:16px system-ui;max-width:560px;margin:40px auto;padding:0 18px}"
        ".err{color:#b00020;background:#fff0f1;padding:12px;border-radius:8px}"
        "a{display:inline-block;margin-top:18px}</style>"
        "<h1>Muse 配网</h1><p class=err>%s</p>"
        "<a href='/'>返回配置页</a>", message ? message : "保存失败");
    if (n < 0 || (size_t)n >= sizeof(page)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page too large");
    return send_html(req, page);
}

static esp_err_t handle_index(httpd_req_t *req)
{
    char page[2600];
    int n = snprintf(page, sizeof(page),
        "<!doctype html><meta charset=utf-8><meta name=viewport "
        "content='width=device-width,initial-scale=1'><title>Muse 配网</title>"
        "<style>body{font:16px system-ui;max-width:560px;margin:30px auto;padding:0 18px}"
        "label{display:block;margin-top:16px;font-weight:600}input{box-sizing:border-box;"
        "width:100%%;padding:11px;font-size:16px;border:1px solid #bbb;border-radius:7px}"
        "button{margin-top:24px;padding:12px 18px;font-size:16px;border:0;border-radius:7px;"
        "background:#1463d6;color:white}small{color:#555}code{background:#eee;padding:2px 4px}"
        "</style><h1>Muse 配网</h1>"
        "<p>设备已进入本地配置模式。填写 Wi-Fi 和 Muse SDK token，保存后设备会自动重启。</p>"
        "<form method=post action='/save'>"
        "<label>Wi-Fi 名称（SSID）</label><input name=ssid maxlength=32 required autofocus>"
        "<label>Wi-Fi 密码</label><input name=password type=password maxlength=64 "
        "autocomplete=current-password><small>开放网络留空。</small>"
        "<label>Muse SDK token</label><input name=token type=password maxlength=48 "
        "placeholder='mgst_...' autocomplete=off>"
        "<small>留空表示保留设备中已有 token。token 只保存在设备 NVS，不会显示在页面上。</small>"
        "<button type=submit>保存并重启</button></form>"
        "<p><small>保存后请断开 <code>%s</code>，让手机和设备回到同一个 Wi-Fi，再用 Muse App 完成配对。</small></p>",
        s_ap_ssid);
    if (n < 0 || (size_t)n >= sizeof(page)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page too large");
    return send_html(req, page);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "restarting after portal configuration");
    esp_restart();
    vTaskDelete(NULL);
}

static esp_err_t handle_save(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= PORTAL_FORM_MAX) {
        return send_error_page(req, "表单太大或为空。");
    }
    char *body = calloc(1, req->content_len + 1);
    if (!body) return send_error_page(req, "设备内存不足。");
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            wipe(body, req->content_len);
            free(body);
            return send_error_page(req, "读取表单失败，请重试。");
        }
        got += (size_t)n;
    }

    char ssid[PORTAL_SSID_MAX + 1] = {0};
    char password[PORTAL_PASSWORD_MAX + 1] = {0};
    char token[PORTAL_TOKEN_MAX] = {0};
    bool token_present = false;
    bool ok = form_value(body, "ssid", ssid, sizeof(ssid), NULL)
              && form_value(body, "password", password, sizeof(password), NULL)
              && form_value(body, "token", token, sizeof(token), &token_present);
    wipe(body, req->content_len);
    free(body);
    if (!ok || !ssid[0] || strlen(ssid) > WIFI_KNOWN_SSID_MAX
        || strlen(password) > WIFI_KNOWN_PASS_MAX) {
        wipe(ssid, sizeof(ssid));
        wipe(password, sizeof(password));
        wipe(token, sizeof(token));
        return send_error_page(req, "SSID 或密码长度不正确。");
    }
    if (token_present && token[0] && !identity_sdk_token_valid(token)) {
        wipe(ssid, sizeof(ssid));
        wipe(password, sizeof(password));
        wipe(token, sizeof(token));
        return send_error_page(req, "SDK token 格式不正确，应为完整的 mgst_ token。");
    }
    if (token_present && token[0] && !identity_set_sdk_token(token)) {
        wipe(ssid, sizeof(ssid));
        wipe(password, sizeof(password));
        wipe(token, sizeof(token));
        return send_error_page(req, "SDK token 保存失败，请重试。");
    }
    if (!wifi_known_remember(ssid, password, false)) {
        wipe(ssid, sizeof(ssid));
        wipe(password, sizeof(password));
        wipe(token, sizeof(token));
        return send_error_page(req, "Wi-Fi 保存失败，请重试。");
    }
    wipe(ssid, sizeof(ssid));
    wipe(password, sizeof(password));
    wipe(token, sizeof(token));

    static const char page[] =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>已保存</title><style>body{font:16px system-ui;max-width:560px;margin:40px auto;padding:0 18px}</style>"
        "<h1>已保存</h1><p>设备将在几秒后重启。请断开配网热点，等待设备连接 Wi-Fi，然后打开 Muse App 配对。</p>";
    esp_err_t err = send_html(req, page);
    if (xTaskCreate(restart_task, "portal_restart", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start restart task");
    }
    return err;
}

static esp_err_t handle_probe(httpd_req_t *req)
{
    return handle_index(req);
}

static void build_ap_credentials(void)
{
    const char *node = identity_node_id();
    const char *dash = node ? strrchr(node, '-') : NULL;
    const char *suffix = dash && dash[1] ? dash + 1 : "DEVICE";
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Muse-Setup-%s", suffix);
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t i = 0; i < 16; i++) {
        s_ap_password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    s_ap_password[16] = '\0';
}

static bool start_ap(void)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            ESP_LOGE(TAG, "could not create AP netif");
            return false;
        }
    }
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, s_ap_password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK
        || esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        ESP_LOGE(TAG, "could not configure setup AP");
        return false;
    }
    return true;
}

static bool start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    if (httpd_start(&s_server, &config) != ESP_OK) return false;

    const httpd_uri_t index = {
        .uri = "/", .method = HTTP_GET, .handler = handle_index,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = handle_save,
    };
    const httpd_uri_t probe[] = {
        { .uri = "/generate_204", .method = HTTP_GET, .handler = handle_probe },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handle_probe },
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = handle_probe },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = handle_probe },
    };
    if (httpd_register_uri_handler(s_server, &index) != ESP_OK
        || httpd_register_uri_handler(s_server, &save) != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return false;
    }
    for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        if (httpd_register_uri_handler(s_server, &probe[i]) != ESP_OK) {
            ESP_LOGW(TAG, "could not register captive probe %s", probe[i].uri);
        }
    }
    return true;
}

static void portal_task(void *arg)
{
    (void)arg;
    build_ap_credentials();
    wifi_mgr_set_portal_active(true);
    if (!wifi_mgr_portal_wait_idle(15000) || !start_ap() || !start_server()) {
        ESP_LOGE(TAG, "setup portal failed to start");
        if (s_server) {
            httpd_stop(s_server);
            s_server = NULL;
        }
        esp_wifi_set_mode(WIFI_MODE_STA);
        wifi_mgr_set_portal_active(false);
        muse_voice_cancel_shutdown();
        atomic_store(&s_start_requested, false);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "setup portal started: SSID=%s URL=http://%s/",
             s_ap_ssid, PORTAL_AP_IP);
#if CONFIG_MUSE_UI_HEADLESS
    printf("Muse setup AP: SSID=%s PASSWORD=%s URL=http://%s/\n",
           s_ap_ssid, s_ap_password, PORTAL_AP_IP);
#else
    muse_state_set_caption("SETUP %s\nPASS %s\nhttp://%s/",
                           s_ap_ssid, s_ap_password, PORTAL_AP_IP);
#endif
    /* The portal remains available until the user saves and the device reboots. */
    for (;;) vTaskDelay(portMAX_DELAY);
}

bool provision_portal_start_async(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_start_requested, &expected, true)) {
        ESP_LOGI(TAG, "setup portal already starting or running");
        return false;
    }
    if (xTaskCreate(portal_task, "muse_portal", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not create setup portal task");
        atomic_store(&s_start_requested, false);
        return false;
    }
    return true;
}
