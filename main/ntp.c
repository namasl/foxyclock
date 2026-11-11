/* SNTP with ordered failover (design §5.1).
 *
 * lwIP moves to the next configured server after a 15 s receive timeout and
 * keeps polling whichever server answered. To return to the primary after a
 * fallback, SNTP is restarted with the list rotated so the primary sits at
 * lwIP's current index (sntp_stop() does not reset that index). */
#include "ntp.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "clock_task.h"
#include "ds1307.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include "lwip/tcpip.h"
#include "sntp.h"
#include "store.h"
#include "xy_ntpord.h"

static const char *TAG = "ntp";

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_started;
static xy_settings_t s_cfg;
static const char *s_names[XY_NUM_NTP];   /* compacted, priority order */
static int s_n;
static int s_cur;                          /* lwIP's current index (mirrored) */
static char s_last_server[64];
static int64_t s_last_sync;
static time_t s_pending_sync;              /* set in the lwIP callback, handled in our task */

#define EV_SYNCED  1u
#define EV_RESTART 2u

/* Runs in the lwIP thread, queued from sync_cb: lwIP marks the responding
 * server's reachability bit only after sync_cb returns. */
static void find_responder(void *arg)
{
    (void)arg;
    for (int i = 0; i < s_n; i++)
        if (sntp_getreachability((u8_t)i) & 1) { s_cur = i; break; }
    xTaskNotify(s_task, EV_SYNCED, eSetBits);
}

/* Runs in the lwIP thread. */
static void sync_cb(struct timeval *tv)
{
    s_pending_sync = tv->tv_sec;
    if (tcpip_callback(find_responder, NULL) != ERR_OK)
        xTaskNotify(s_task, EV_SYNCED, eSetBits);
}

static void load_cfg(void)
{
    store_get(&s_cfg);
    s_n = 0;
    for (int i = 0; i < XY_NUM_NTP; i++)
        if (s_cfg.ntp[i][0]) s_names[s_n++] = s_cfg.ntp[i];
}

/* Runs in the lwIP thread. */
static void lwip_restart(void *arg)
{
    (void)arg;
    if (sntp_enabled()) sntp_stop();
    /* Reload here, in the lwIP thread: lwIP keeps pointers to these names. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_cfg();
    xSemaphoreGive(s_lock);
    const char *rot[XY_NUM_NTP];
    xy_ntp_rotate(s_names, s_n, s_cur, rot);
    for (int i = 0; i < XY_NUM_NTP; i++) sntp_setservername((u8_t)i, i < s_n ? rot[i] : NULL);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_interval(s_cfg.ntp_interval_s * 1000u);   /* <= 86400000: fits */
    sntp_set_time_sync_notification_cb(sync_cb);
    sntp_init();
}

static void ntp_task(void *arg)
{
    (void)arg;
    TickType_t wait = portMAX_DELAY;
    for (;;) {
        uint32_t ev = 0;
        if (!xTaskNotifyWait(0, UINT32_MAX, &ev, wait)) {
            /* Timed out: we are on a fallback server and an interval has passed. */
            ev = EV_RESTART;
        }
        if (ev & EV_RESTART) {
            tcpip_callback(lwip_restart, NULL);
            ESP_LOGI(TAG, "SNTP (re)start requested");
            wait = portMAX_DELAY;
        }
        if (ev & EV_SYNCED) {
            time_t t = s_pending_sync;
            ds1307_write_utc(t);
            clock_set_ntp_synced();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const char *who = (s_cur >= 0 && s_cur < s_n) ? sntp_getservername((u8_t)s_cur) : NULL;
            snprintf(s_last_server, sizeof s_last_server, "%s", who ? who : "");
            s_last_sync = t;
            bool on_primary = s_n == 0 || (who && strcmp(who, s_names[0]) == 0);
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "time set from %s", s_last_server);
            /* After a fallback, go back to the primary at the next interval. */
            wait = on_primary ? portMAX_DELAY : (TickType_t)s_cfg.ntp_interval_s * configTICK_RATE_HZ;
        }
    }
}

void ntp_start(void)
{
    if (s_started) return;
    s_started = true;
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(ntp_task, "ntp", 2560, NULL, 4, &s_task);
    xTaskNotify(s_task, EV_RESTART, eSetBits);
}

void ntp_restart(void)
{
    if (s_started) xTaskNotify(s_task, EV_RESTART, eSetBits);
}

void ntp_status(char *server, size_t n, int64_t *last_sync_epoch)
{
    if (!s_started) { snprintf(server, n, "%s", ""); *last_sync_epoch = 0; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(server, n, "%s", s_last_server);
    *last_sync_epoch = s_last_sync;
    xSemaphoreGive(s_lock);
}
