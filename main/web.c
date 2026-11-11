/* HTTP server on port 80: the settings page and its JSON API (design §10). */
#include "web.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "cJSON.h"
#include "clock_task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "mbedtls/base64.h"
#include "net.h"
#include "ntp.h"
#include "ota.h"
#include "store.h"
#include "xy_tz.h"
#include "xy_webpass.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

#define MAX_BODY 4096

/* ---- helpers ----------------------------------------------------------- */

static esp_err_t send_str(httpd_req_t *req, const char *s)
{
    return httpd_resp_send(req, s, strlen(s));
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void web_restart_soon(void)
{
    xTaskCreate(restart_task, "restart", 2048, NULL, 3, NULL);
}

static bool consttime_eq(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char d = (unsigned char)(la != lb);
    for (size_t i = 0; i < la; i++) d |= (unsigned char)(a[i] ^ b[i % (lb ? lb : 1)]);
    return d == 0;
}

bool web_authorized(httpd_req_t *req)
{
    xy_settings_t s;
    store_get(&s);
    if (!s.web_pass[0]) return true;
    char hdr[160];
    bool ok = false;
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof hdr) == ESP_OK &&
        strncmp(hdr, "Basic ", 6) == 0) {
        unsigned char dec[100];
        size_t n = 0;
        if (mbedtls_base64_decode(dec, sizeof dec - 1, &n, (const unsigned char *)hdr + 6,
                                  strlen(hdr + 6)) == 0) {
            dec[n] = 0;
            const char *colon = strchr((const char *)dec, ':');   /* any user name accepted */
            if (colon) ok = consttime_eq(colon + 1, s.web_pass);
        }
    }
    if (!ok) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"xyclock\"");
        send_str(req, "Password required");
    }
    return ok;
}

esp_err_t web_send_json_error(httpd_req_t *req, const char *status, const char *msg)
{
    ESP_LOGW(TAG, "%s %s -> %s: %s (heap free %u, min %u)", req->method == HTTP_POST ? "POST" : "GET",
             req->uri, status, msg, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", msg);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = send_str(req, s ? s : "{\"error\":\"out of memory\"}");
    free(s);
    return r;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return web_send_json_error(req, "500 Internal Server Error", "out of memory (print)");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = send_str(req, s);
    free(s);
    return r;
}

/* Cross-site request guard: every POST must carry X-XYClock: 1. A browser
 * only sends a custom header from another site after a CORS preflight,
 * which this server never approves. */
static bool same_origin_post(httpd_req_t *req)
{
    char v[8];
    if (httpd_req_get_hdr_value_str(req, "X-XYClock", v, sizeof v) == ESP_OK && strcmp(v, "1") == 0)
        return true;
    web_send_json_error(req, "403 Forbidden", "missing X-XYClock header");
    return false;
}

bool web_post_allowed(httpd_req_t *req)
{
    return web_authorized(req) && same_origin_post(req);
}

/* Administrative POSTs: as web_post_allowed(), and a web password must
 * already be set (owner decision: the password is required). */
bool web_admin_allowed(httpd_req_t *req)
{
    if (!web_post_allowed(req)) return false;
    xy_settings_t s;
    store_get(&s);
    const char *e = xy_web_admin_gate(s.web_pass[0] != 0, false);
    if (e) { web_send_json_error(req, "403 Forbidden", e); return false; }
    return true;
}

/* cJSON recurses per nesting level; keep bodies shallow so a crafted
 * request cannot overflow the server task's stack. */
static bool nesting_ok(const char *s)
{
    int depth = 0;
    bool in_str = false;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) s++;
            else if (*s == '"') in_str = false;
        } else if (*s == '"') in_str = true;
        else if (*s == '{' || *s == '[') { if (++depth > 4) return false; }
        else if (*s == '}' || *s == ']') depth--;
    }
    return true;
}

/* Reads the whole body (<= MAX_BODY). Returns malloc'd NUL-terminated text or NULL (error sent). */
static char *read_body(httpd_req_t *req)
{
    if (req->content_len > MAX_BODY) {
        web_send_json_error(req, "413 Payload Too Large", "request too large");
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) { web_send_json_error(req, "500 Internal Server Error", "out of memory"); return NULL; }
    size_t got = 0;
    int timeouts = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (r <= 0) { free(buf); web_send_json_error(req, "400 Bad Request", "incomplete request"); return NULL; }
        got += (size_t)r;
    }
    buf[got] = 0;
    if (!nesting_ok(buf)) {
        free(buf);
        web_send_json_error(req, "400 Bad Request", "JSON nested too deeply");
        return NULL;
    }
    return buf;
}

/* ---- handlers ---------------------------------------------------------- */

static esp_err_t h_index(httpd_req_t *req)
{
    if (!web_authorized(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t h_get_settings(httpd_req_t *req)
{
    if (!web_authorized(req)) return ESP_OK;
    ESP_LOGI(TAG, "GET /api/settings: heap free %u, min %u", (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    xy_settings_t s;
    uint32_t rev;
    store_get_rev(&s, &rev);                    /* consistent pair for the page */
    char *js = xy_settings_to_json(&s);
    if (!js) return web_send_json_error(req, "500 Internal Server Error", "out of memory (settings json)");
    cJSON *o = js ? cJSON_Parse(js) : NULL;
    free(js);
    if (!o) return web_send_json_error(req, "500 Internal Server Error", "out of memory (parse)");
    cJSON *tz = cJSON_AddArrayToObject(o, "tz_table");
    for (size_t i = 0; tz && i < XY_TZ_COUNT; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", XY_TZ_TABLE[i].name);
        cJSON_AddStringToObject(e, "posix", XY_TZ_TABLE[i].posix);
        cJSON_AddItemToArray(tz, e);
    }
    cJSON_AddBoolToObject(o, "web_pass_set", s.web_pass[0] != 0);
    cJSON_AddNumberToObject(o, "web_pass_min", XY_WEB_PASS_MIN);
    cJSON_AddBoolToObject(o, "wifi_pass_set", s.wifi_pass[0] != 0);
    cJSON_AddBoolToObject(o, "first_setup", !store_loaded());
    cJSON_AddNumberToObject(o, "rev", rev);
    return send_json(req, o);
}

static bool network_changed(const xy_settings_t *a, const xy_settings_t *b)
{
    return strcmp(a->ssid, b->ssid) || strcmp(a->wifi_pass, b->wifi_pass) ||
           a->wifi_sec != b->wifi_sec || a->dhcp != b->dhcp || strcmp(a->ip, b->ip) ||
           strcmp(a->netmask, b->netmask) || strcmp(a->gateway, b->gateway) ||
           strcmp(a->dns, b->dns) || strcmp(a->hostname, b->hostname) ||
           strcmp(a->ap_pass, b->ap_pass);
}

static esp_err_t h_post_settings(httpd_req_t *req)
{
    if (!web_post_allowed(req)) return ESP_OK;
    char *body = read_body(req);
    if (!body) return ESP_OK;
    /* Reject saves from a page showing settings that changed since it loaded
     * (e.g. a ring-once alarm that disabled itself, or a button brightness
     * change): saving them would silently undo those changes. */
    static const char *STALE = "settings changed on the clock since this page loaded; reload and try again";
    cJSON *probe = cJSON_Parse(body);
    const cJSON *rv = probe ? cJSON_GetObjectItemCaseSensitive(probe, "rev") : NULL;
    bool have_rev = cJSON_IsNumber(rv);
    uint32_t page_rev = have_rev ? (uint32_t)rv->valuedouble : 0;
    cJSON_Delete(probe);
    xy_settings_t cur, next;
    uint32_t cur_rev;
    store_get_rev(&cur, &cur_rev);              /* settings and revision read together */
    if (!have_rev || page_rev != cur_rev) {
        free(body);
        return web_send_json_error(req, "409 Conflict", STALE);
    }
    const char *e = xy_settings_from_json(body, &cur, &next);
    free(body);
    if (e) return web_send_json_error(req, "400 Bad Request", e);
    e = xy_web_pass_policy(next.web_pass, strcmp(next.web_pass, cur.web_pass) != 0);
    if (e) return web_send_json_error(req, "400 Bad Request", e);
    next.g14_state = cur.g14_state;          /* owned by the GPIO14 control, not the form */
    /* Commit only if nothing changed since cur was read (compare-and-save):
     * a button press or alarm in between makes this a 409, not an overwrite. */
    esp_err_t err = store_put_if_rev(&next, cur_rev);
    if (err == ESP_ERR_INVALID_VERSION) return web_send_json_error(req, "409 Conflict", STALE);
    if (err != ESP_OK)
        return web_send_json_error(req, "500 Internal Server Error", "could not save settings");
    /* An alarm that is switched off or edited while active ends as if
     * dismissed, with its old GPIO14 stop action applied. */
    int cancel = 0;
    for (int i = 0; i < XY_NUM_ALARMS; i++)
        if (memcmp(&cur.alarm[i], &next.alarm[i], sizeof cur.alarm[i]) != 0) cancel |= 1 << i;
    clock_settings_saved(cancel);
    bool reboot = network_changed(&cur, &next) || net_is_ap();
    if (!reboot && (memcmp(cur.ntp, next.ntp, sizeof cur.ntp) || cur.ntp_interval_s != next.ntp_interval_s))
        ntp_restart();
    ESP_LOGI(TAG, "settings saved%s", reboot ? "; restarting" : "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "reboot", reboot);
    esp_err_t r = send_json(req, o);
    if (reboot) web_restart_soon();
    return r;
}

static const char *phase_name(xy_al_phase_t p)
{
    return p == XY_AL_RINGING ? "ringing" : p == XY_AL_SNOOZED ? "snoozed" : "idle";
}

static esp_err_t h_status(httpd_req_t *req)
{
    if (!web_authorized(req)) return ESP_OK;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[64];
    cJSON *o = cJSON_CreateObject();
    xy_trust_t tr = clock_trust();
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %a", &lt);   /* e.g. 2026-11-09 23:30:58 Mon */
    cJSON_AddStringToObject(o, "datetime", buf);
    cJSON_AddStringToObject(o, "trust", tr == XY_TIME_TRUSTED ? "trusted" :
                                        tr == XY_TIME_STALE ? "stale" : "untrusted");
    int64_t last = 0;
    ntp_status(buf, sizeof buf, &last);
    cJSON_AddStringToObject(o, "ntp_server", buf);
    cJSON_AddNumberToObject(o, "last_sync", (double)last);
    cJSON_AddNumberToObject(o, "now", (double)t);
    net_ip_string(buf, sizeof buf);
    cJSON_AddStringToObject(o, "ip", buf);
    net_mac_string(buf, sizeof buf);
    cJSON_AddStringToObject(o, "mac", buf);
    cJSON_AddNumberToObject(o, "rssi", net_rssi());
    cJSON_AddBoolToObject(o, "ap_mode", net_is_ap());
    cJSON_AddBoolToObject(o, "wifi", net_connected());
    cJSON_AddBoolToObject(o, "save_failed", store_save_failed());
    int reason = net_last_disconnect_reason();
    cJSON_AddNumberToObject(o, "wifi_reason", reason);
    cJSON_AddStringToObject(o, "wifi_reason_text", reason ? reason_name(reason) : "");
    const esp_app_desc_t *d = esp_ota_get_app_description();
    cJSON_AddStringToObject(o, "fw_version", d->version);
    snprintf(buf, sizeof buf, "%s %s", d->date, d->time);
    cJSON_AddStringToObject(o, "fw_built", buf);
    cJSON_AddStringToObject(o, "idf", d->idf_ver);
    cJSON_AddNumberToObject(o, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(o, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(o, "heap_min", esp_get_minimum_free_heap_size());
    cJSON_AddBoolToObject(o, "g14", io_g14_get());
    cJSON *al = cJSON_AddArrayToObject(o, "alarms");
    for (int i = 0; al && i < XY_NUM_ALARMS; i++)
        cJSON_AddItemToArray(al, cJSON_CreateString(phase_name(clock_alarm_phase(i))));
    return send_json(req, o);
}

/* Networks for the WiFi drop-down. auth maps to the security setting:
 * "wpa3" (WPA3 only), "wpa2_wpa3", "wpa2", or "other" (open/WEP/WPA1/
 * enterprise, which this clock does not support). */
static esp_err_t h_scan(httpd_req_t *req)
{
    if (!web_authorized(req)) return ESP_OK;
    net_ap_t *aps = malloc(20 * sizeof *aps);
    if (!aps) return web_send_json_error(req, "500 Internal Server Error", "out of memory");
    int n = net_scan(aps, 20);
    if (n < 0) { free(aps); return web_send_json_error(req, "503 Service Unavailable", "scan failed; try again"); }
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "networks");
    for (int i = 0; arr && i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(e, "rssi", aps[i].rssi);
        /* The SDK reports networks whose key management it does not know
         * (e.g. WPA2/WPA3 that also offers 802.11r fast roaming) as "open",
         * but their pairwise cipher still shows they are encrypted. */
        bool enc = aps[i].cipher == WIFI_CIPHER_TYPE_CCMP || aps[i].cipher == WIFI_CIPHER_TYPE_TKIP ||
                   aps[i].cipher == WIFI_CIPHER_TYPE_TKIP_CCMP;
        const char *a = aps[i].auth == WIFI_AUTH_WPA3_PSK ? "wpa3"
                      : aps[i].auth == WIFI_AUTH_WPA2_WPA3_PSK ? "wpa2_wpa3"
                      : (aps[i].auth == WIFI_AUTH_WPA2_PSK || aps[i].auth == WIFI_AUTH_WPA_WPA2_PSK) ? "wpa2"
                      : (aps[i].auth == WIFI_AUTH_OPEN && enc) ? "encrypted"
                      : "other";
        cJSON_AddStringToObject(e, "auth", a);
        cJSON_AddNumberToObject(e, "auth_raw", aps[i].auth);   /* wifi_auth_mode_t, for diagnosis */
        cJSON_AddItemToArray(arr, e);
    }
    free(aps);
    return send_json(req, o);
}

/* POST /api/wifi-test: same JSON as a settings save (only the network fields
 * matter). Validated against the current settings, then tried without saving. */
static esp_err_t h_wifi_test(httpd_req_t *req)
{
    if (!web_post_allowed(req)) return ESP_OK;
    {
        xy_settings_t s;
        store_get(&s);
        const char *g = xy_web_admin_gate(s.web_pass[0] != 0, net_is_ap());
        if (g) return web_send_json_error(req, "403 Forbidden", g);
    }
    char *body = read_body(req);
    if (!body) return ESP_OK;
    xy_settings_t cur, cand;
    store_get(&cur);
    /* Only the network fields matter here. Validate them alone: in first setup
     * the page sends an empty time zone, which must not block the test. */
    const char *e = xy_settings_from_json_net(body, &cur, &cand);
    free(body);
    if (e) return web_send_json_error(req, "400 Bad Request", e);
    if (!cand.ssid[0]) return web_send_json_error(req, "400 Bad Request", "ssid: enter a network name");
    esp_err_t err = net_test_start(&cand);
    if (err == ESP_ERR_INVALID_STATE)
        return web_send_json_error(req, "409 Conflict", "a connection test is already running");
    if (err != ESP_OK) return web_send_json_error(req, "500 Internal Server Error", "could not start the test");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

/* GET /api/wifi-test: progress and result of the last test. */
static esp_err_t h_wifi_test_get(httpd_req_t *req)
{
    if (!web_authorized(req)) return ESP_OK;
    xy_wt_t w;
    char ssid[33];
    net_test_get(&w, ssid, sizeof ssid);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", w.state == XY_WT_RUNNING ? "running" : w.state == XY_WT_DONE ? "done" : "idle");
    cJSON_AddStringToObject(o, "ssid", ssid);
    cJSON_AddBoolToObject(o, "ok", w.ok);
    cJSON_AddStringToObject(o, "ip", w.ip);
    cJSON_AddNumberToObject(o, "reason", w.reason);
    const char *why = w.ok ? "" : w.timed_out ? (w.associated ? "joined the network but got no IP address (DHCP or static address problem)"
                                                               : "no answer from the network")
                    : w.reason ? reason_name(w.reason) : "";
    cJSON_AddStringToObject(o, "why", why);
    return send_json(req, o);
}

static esp_err_t h_gpio14(httpd_req_t *req)
{
    if (!web_admin_allowed(req)) return ESP_OK;
    char *body = read_body(req);
    if (!body) return ESP_OK;
    cJSON *o = cJSON_Parse(body);
    free(body);
    const cJSON *st = o ? cJSON_GetObjectItemCaseSensitive(o, "state") : NULL;
    if (!cJSON_IsBool(st)) {
        cJSON_Delete(o);
        return web_send_json_error(req, "400 Bad Request", "state must be true or false");
    }
    clock_set_g14(cJSON_IsTrue(st));
    cJSON_Delete(o);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddBoolToObject(r, "g14", io_g14_get());
    return send_json(req, r);
}

/* "Sync now": restart SNTP, which queries the servers in priority order. */
static esp_err_t h_ntp_sync(httpd_req_t *req)
{
    if (!web_admin_allowed(req)) return ESP_OK;
    if (net_is_ap() || !net_connected())
        return web_send_json_error(req, "409 Conflict", "not connected to a WiFi network, so NTP cannot be reached");
    ntp_restart();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

static esp_err_t h_restart(httpd_req_t *req)
{
    if (!web_admin_allowed(req)) return ESP_OK;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    send_json(req, o);
    web_restart_soon();
    return ESP_OK;
}

static esp_err_t h_factory_reset(httpd_req_t *req)
{
    if (!web_admin_allowed(req)) return ESP_OK;
    ESP_LOGW(TAG, "factory reset from web page");
    if (store_factory_reset() != ESP_OK)
        return web_send_json_error(req, "500 Internal Server Error",
                                   "factory reset failed: settings could not be erased (try again, or use the UP+DOWN power-on hold)");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    send_json(req, o);
    web_restart_soon();
    return ESP_OK;
}

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 4;
    httpd_handle_t h = NULL;
    if (httpd_start(&h, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/",                  .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/settings",      .method = HTTP_GET,  .handler = h_get_settings },
        { .uri = "/api/settings",      .method = HTTP_POST, .handler = h_post_settings },
        { .uri = "/api/status",        .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/gpio14",        .method = HTTP_POST, .handler = h_gpio14 },
        { .uri = "/api/scan",          .method = HTTP_GET,  .handler = h_scan },
        { .uri = "/api/wifi-test",     .method = HTTP_POST, .handler = h_wifi_test },
        { .uri = "/api/wifi-test",     .method = HTTP_GET,  .handler = h_wifi_test_get },
        { .uri = "/api/ntp-sync",      .method = HTTP_POST, .handler = h_ntp_sync },
        { .uri = "/api/restart",       .method = HTTP_POST, .handler = h_restart },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = h_factory_reset },
        { .uri = "/api/ota",           .method = HTTP_POST, .handler = ota_handle_upload },
    };
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++)
        httpd_register_uri_handler(h, &routes[i]);
    ESP_LOGI(TAG, "web server on port 80 (heap free %u)", (unsigned)esp_get_free_heap_size());
}
