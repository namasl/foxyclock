/* WiFi: station mode with endless reconnects, or the setup access point.
 * Once configured, the clock never falls back to AP mode (design §4.2). */
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clock_task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mdns.h"
#include "ntp.h"
#include "store.h"
#include "tcpip_adapter.h"
#include "xy_wifitest.h"

static const char *TAG = "net";

static bool s_ap, s_connected, s_mdns_started;
static int s_retries;
static TimerHandle_t s_retry_timer;
static xy_settings_t s_cfg;
static int s_last_reason;
static bool s_associated;

/* Connection test (see core/wifitest.c). While s_testing, station events go
 * to the test instead of the normal reconnect logic. s_test_armed is set once
 * the station has been pointed at the network under test, so the disconnect
 * caused by leaving the previous network is not counted as a failure. */
static SemaphoreHandle_t s_wt_lock;
static EventGroupHandle_t s_ev;
#define EV_DISCONNECTED BIT0
static xy_wt_t s_wt;
static xy_settings_t s_test_cfg;
static volatile bool s_testing, s_test_armed;

int net_last_disconnect_reason(void) { return s_last_reason; }

const char *reason_name(int r)
{
    switch (r) {
    case WIFI_REASON_AUTH_EXPIRE:              return "authentication expired";
    case WIFI_REASON_AUTH_FAIL:                return "authentication failed";
    case WIFI_REASON_ASSOC_FAIL:               return "association failed";
    case WIFI_REASON_ASSOC_EXPIRE:             return "association expired";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:   return "4-way handshake timeout (wrong password?)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:        return "handshake timeout (wrong password?)";
    case WIFI_REASON_NO_AP_FOUND:              return "network not found";
    case WIFI_REASON_BEACON_TIMEOUT:           return "lost signal";
    case WIFI_REASON_AKMP_INVALID:             return "security type not supported (AKM)";
    case WIFI_REASON_INVALID_RSN_IE_CAP:       return "security capabilities rejected (PMF?)";
    case WIFI_REASON_IE_INVALID:               return "security info rejected";
    case WIFI_REASON_ASSOC_LEAVE:              return "left network";
    case WIFI_REASON_CONNECTION_FAIL:          return "connection failed";
    default:                                   return "see 802.11 reason codes";
    }
}

bool net_is_ap(void) { return s_ap; }
bool net_connected(void) { return s_connected; }

void net_ip_string(char *buf, int n)
{
    tcpip_adapter_ip_info_t ip;
    tcpip_adapter_if_t itf = s_ap ? TCPIP_ADAPTER_IF_AP : TCPIP_ADAPTER_IF_STA;
    if ((s_ap || s_connected) && tcpip_adapter_get_ip_info(itf, &ip) == ESP_OK && ip.ip.addr) {
        const uint8_t *b = (const uint8_t *)&ip.ip.addr;
        snprintf(buf, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    } else {
        snprintf(buf, n, "no IP");
    }
}

void net_mac_string(char *buf, size_t n)
{
    uint8_t m[6] = {0};
    esp_wifi_get_mac(s_ap ? ESP_IF_WIFI_AP : ESP_IF_WIFI_STA, m);
    snprintf(buf, n, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_ap || !s_connected || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}

static void retry_cb(TimerHandle_t t)
{
    (void)t;
    if (!s_testing) esp_wifi_connect();
}

static void start_mdns(void)
{
    if (s_mdns_started) return;
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(s_cfg.hostname);
        mdns_instance_name_set("XY-Clock");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        s_mdns_started = true;
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && !s_ap) {
        /* The STA interface exists only from here on; set the DHCP host name now. */
        if (tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, s_cfg.hostname) != ESP_OK)
            ESP_LOGW(TAG, "could not set host name");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        s_associated = true;
        if (s_testing && s_test_armed) {
            xSemaphoreTake(s_wt_lock, portMAX_DELAY);
            xy_wt_on_associated(&s_wt, esp_timer_get_time() / 1000);
            xSemaphoreGive(s_wt_lock);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && s_testing) {
        const wifi_event_sta_disconnected_t *d = data;
        s_associated = false;
        xEventGroupSetBits(s_ev, EV_DISCONNECTED);
        if (s_test_armed) {
            ESP_LOGW(TAG, "test: disconnected from \"%s\": reason %d (%s)", s_test_cfg.ssid,
                     d ? d->reason : 0, reason_name(d ? d->reason : 0));
            xSemaphoreTake(s_wt_lock, portMAX_DELAY);
            xy_wt_on_disconnect(&s_wt, esp_timer_get_time() / 1000, d ? d->reason : 0);
            xSemaphoreGive(s_wt_lock);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && s_testing) {
        if (s_test_armed) {
            const ip_event_got_ip_t *g = data;
            char ip[16];
            const uint8_t *b = (const uint8_t *)&g->ip_info.ip.addr;
            snprintf(ip, sizeof ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            ESP_LOGI(TAG, "test: connected to \"%s\", IP %s", s_test_cfg.ssid, ip);
            xSemaphoreTake(s_wt_lock, portMAX_DELAY);
            xy_wt_on_got_ip(&s_wt, esp_timer_get_time() / 1000, ip);
            xSemaphoreGive(s_wt_lock);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && !s_ap) {
        const wifi_event_sta_disconnected_t *d = data;
        s_associated = false;
        xEventGroupSetBits(s_ev, EV_DISCONNECTED);
        s_last_reason = d ? d->reason : 0;
        ESP_LOGW(TAG, "disconnected from \"%s\": reason %d (%s)", s_cfg.ssid, s_last_reason,
                 reason_name(s_last_reason));
        s_connected = false;
        clock_set_wifi(false);
        /* 5 s for the first few attempts, then every 30 s, forever. */
        uint32_t ms = s_retries < 3 ? 5000 : 30000;
        s_retries++;
        ESP_LOGW(TAG, "disconnected; retry %d in %u s", s_retries, (unsigned)(ms / 1000));
        xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(ms), 0);
        xTimerStart(s_retry_timer, 0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_associated = false;                     /* setup mode: station idle */
        xEventGroupSetBits(s_ev, EV_DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_retries = 0;
        clock_set_wifi(true);
        char ip[20];
        net_ip_string(ip, sizeof ip);
        ESP_LOGI(TAG, "connected, IP %s", ip);
        start_mdns();
        ntp_start();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "setup client connected");
    }
}

static void start_ap(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    wifi_config_t wc = {0};
    int n = snprintf((char *)wc.ap.ssid, sizeof wc.ap.ssid, "XYClock-%02X%02X", mac[4], mac[5]);
    wc.ap.ssid_len = (uint8_t)n;
    /* Lengths are validated (ap_pass <= 63, ssid <= 32); SDK fields need no NUL at full length. */
    strncpy((char *)wc.ap.password, s_cfg.ap_pass, sizeof wc.ap.password);
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.channel = 1;
    wc.ap.max_connection = 4;
    /* AP+STA so the setup page can scan for networks; the station never connects. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "setup AP \"%s\" at 192.168.4.1", wc.ap.ssid);
}

static void set_static_ip(const xy_settings_t *c)
{
    tcpip_adapter_ip_info_t info = {0};
    uint8_t b[4];
    xy_parse_ipv4(c->ip, b);      memcpy(&info.ip.addr, b, 4);
    xy_parse_ipv4(c->netmask, b); memcpy(&info.netmask.addr, b, 4);
    xy_parse_ipv4(c->gateway, b); memcpy(&info.gw.addr, b, 4);
    tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
    tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &info);
    if (c->dns[0] && xy_parse_ipv4(c->dns, b)) {
        tcpip_adapter_dns_info_t dns = {0};
        IP_ADDR4(&dns.ip, b[0], b[1], b[2], b[3]);
        tcpip_adapter_set_dns_info(TCPIP_ADAPTER_IF_STA, TCPIP_ADAPTER_DNS_MAIN, &dns);
    }
}

/* Point the station at network c (SSID, password, security, addressing). */
static esp_err_t config_sta(const xy_settings_t *c)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, c->ssid, sizeof wc.sta.ssid);
    memcpy(wc.sta.password, c->wifi_pass, sizeof wc.sta.password);
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = c->wifi_sec == XY_SEC_WPA3;
    wc.sta.threshold.authmode = c->wifi_sec == XY_SEC_WPA3 ? WIFI_AUTH_WPA3_PSK : WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wc);
    if (c->dhcp) tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);   /* no-op if already running */
    else set_static_ip(c);
    return err;
}

static void start_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(config_sta(&s_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "joining \"%s\"", s_cfg.ssid);
}

void net_start(bool ap_mode)
{
    store_get(&s_cfg);
    s_ap = ap_mode;
    s_wt_lock = xSemaphoreCreateMutex();
    s_ev = xEventGroupCreate();
    xy_wt_init(&s_wt);
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* our NVS blob is the source of truth */
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(5000), pdFALSE, NULL, retry_cb);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    clock_set_ip_provider(net_ip_string);
    if (ap_mode) start_ap(); else start_sta();
}

/* ---- network scan for the setup page ----------------------------------- */

int net_scan(net_ap_t *out, int max)
{
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                              .scan_time.active = { .min = 100, .max = 300 } };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return -1;
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) return 0;
    wifi_ap_record_t *recs = calloc(n, sizeof *recs);
    if (!recs) { uint16_t z = 0; esp_wifi_scan_get_ap_records(&z, NULL); return -1; }
    esp_wifi_scan_get_ap_records(&n, recs);       /* also frees the driver's list */
    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) continue;
        int j = 0;                                 /* keep the strongest of duplicate SSIDs */
        for (; j < count; j++) if (strcmp(out[j].ssid, ssid) == 0) break;
        if (j < count) {
            if (recs[i].rssi > out[j].rssi) {
                out[j].rssi = recs[i].rssi; out[j].auth = recs[i].authmode; out[j].cipher = recs[i].pairwise_cipher;
            }
            continue;
        }
        if (count == max) continue;
        ESP_LOGI(TAG, "scan: \"%s\" rssi %d auth %d pairwise %d group %d ch %d",
                 ssid, recs[i].rssi, recs[i].authmode, recs[i].pairwise_cipher,
                 recs[i].group_cipher, recs[i].primary);
        snprintf(out[count].ssid, sizeof out[count].ssid, "%s", ssid);
        out[count].rssi = recs[i].rssi;
        out[count].auth = recs[i].authmode;
        out[count].cipher = recs[i].pairwise_cipher;
        count++;
    }
    free(recs);
    /* strongest first */
    for (int a = 1; a < count; a++)
        for (int b = a; b > 0 && out[b].rssi > out[b - 1].rssi; b--) {
            net_ap_t tmp = out[b]; out[b] = out[b - 1]; out[b - 1] = tmp;
        }
    return count;
}

/* ---- connection test ------------------------------------------------------ */

/* Leave the current network (if any) and wait until the driver has said so. */
static void leave_network(void)
{
    if (!s_associated) return;
    xEventGroupClearBits(s_ev, EV_DISCONNECTED);
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_ev, EV_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
}

static void test_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));            /* let the HTTP reply go out first */
    for (;;) {
        xSemaphoreTake(s_wt_lock, portMAX_DELAY);
        xy_wt_act_t act = xy_wt_poll(&s_wt, esp_timer_get_time() / 1000);
        xSemaphoreGive(s_wt_lock);
        if (act == XY_WT_ACT_CONNECT_TEST) {
            ESP_LOGI(TAG, "test: trying \"%s\"", s_test_cfg.ssid);
            xTimerStop(s_retry_timer, 0);
            s_connected = false;
            clock_set_wifi(false);
            leave_network();
            config_sta(&s_test_cfg);
            tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, s_cfg.hostname);
            s_test_armed = true;
            esp_wifi_connect();
        } else if (act == XY_WT_ACT_RETRY) {
            esp_wifi_connect();
        } else if (act == XY_WT_ACT_RESTORE) {
            s_test_armed = false;
            leave_network();
            if (s_ap) {
                /* Setup mode: the station stays idle, as before the test. */
                if (!s_test_cfg.dhcp) tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
                s_testing = false;
                ESP_LOGI(TAG, "test finished; back to setup mode");
            } else {
                config_sta(&s_cfg);
                s_retries = 0;
                s_testing = false;
                ESP_LOGI(TAG, "test finished; rejoining \"%s\"", s_cfg.ssid);
                esp_wifi_connect();
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

esp_err_t net_test_start(const xy_settings_t *candidate)
{
    xSemaphoreTake(s_wt_lock, portMAX_DELAY);
    bool started = !s_testing && xy_wt_start(&s_wt, esp_timer_get_time() / 1000);
    xSemaphoreGive(s_wt_lock);
    if (!started) return ESP_ERR_INVALID_STATE;
    s_test_cfg = *candidate;
    s_test_armed = false;
    s_testing = true;
    if (xTaskCreate(test_task, "wifitest", 3072, NULL, 4, NULL) != pdPASS) {
        s_testing = false;
        xSemaphoreTake(s_wt_lock, portMAX_DELAY);
        xy_wt_init(&s_wt);
        xSemaphoreGive(s_wt_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void net_test_get(xy_wt_t *out, char *ssid, size_t n)
{
    xSemaphoreTake(s_wt_lock, portMAX_DELAY);
    *out = s_wt;
    xSemaphoreGive(s_wt_lock);
    snprintf(ssid, n, "%s", s_test_cfg.ssid);
}
