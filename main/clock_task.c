#include "clock_task.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "beeper.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "io.h"
#include "store.h"
#include "tm1650.h"
#include "xy_buttons.h"
#include "xy_display.h"
#include "xy_sound.h"

static const char *TAG = "clock";

#define LOOP_MS          20
#define BRIGHT_SAVE_MS   3000

static SemaphoreHandle_t s_lock;          /* protects the fields below */
static xy_settings_t s_cfg;               /* local copy used by the loop */
static bool s_cfg_dirty;                 /* protected by s_lock, with s_cancel_mask */
static xy_timetrust_t s_tt;
static xy_alarms_t s_alarms;
static bool s_wifi, s_ap_mode;
static char s_text[8];
static int64_t s_text_until_ms;           /* 0 = no text, INT64_MAX = until cleared */
static int s_cancel_mask;
static char s_cfg_tz_prev[64];           /* time zone of the loaded settings */
static void (*s_ip_fn)(char *, int);

static int64_t mono_ms(void) { return esp_timer_get_time() / 1000; }

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

void clock_notify_settings_changed(void) { lock(); s_cfg_dirty = true; unlock(); }

void clock_settings_saved(int cancel_mask)
{
    /* One hand-off under the lock: the loop sees the cancels and the reload
     * together, and runs the cancels (with the OLD settings) first. */
    lock();
    s_cancel_mask |= cancel_mask;
    s_cfg_dirty = true;
    unlock();
}

void clock_show_text(const char *s, uint32_t ms)
{
    lock();
    snprintf(s_text, sizeof s_text, "%s", s);
    s_text_until_ms = ms ? mono_ms() + ms : INT64_MAX;
    unlock();
}

void clock_clear_text(void) { lock(); s_text_until_ms = 0; unlock(); }
void clock_set_wifi(bool c) { s_wifi = c; }
void clock_set_ip_provider(void (*fn)(char *, int)) { s_ip_fn = fn; }

void clock_set_ntp_synced(void)
{
    lock();
    xy_tt_on_sync(&s_tt, mono_ms() / 1000);
    unlock();
}

xy_trust_t clock_trust(void)
{
    lock();
    xy_trust_t t = xy_tt_state(&s_tt, mono_ms() / 1000);
    unlock();
    return t;
}

void clock_cancel_alarm(int i)
{
    if (i < 0 || i >= XY_NUM_ALARMS) return;
    lock();
    s_cancel_mask |= 1 << i;
    unlock();
}

xy_al_phase_t clock_alarm_phase(int i)
{
    lock();
    xy_al_phase_t p = (i >= 0 && i < XY_NUM_ALARMS) ? s_alarms.st[i].phase : XY_AL_IDLE;
    unlock();
    return p;
}

/* ---- persistence helpers (run from the clock task) ------------------- */

static void edit_g14(xy_settings_t *s, void *arg) { s->g14_state = *(bool *)arg; }

void clock_set_g14(bool high)
{
    io_g14_set(high);
    store_update(edit_g14, &high);
    lock(); s_cfg.g14_state = high; unlock();
}

struct bright_edit { bool sched, day; uint8_t level; };
static void edit_bright(xy_settings_t *s, void *arg)
{
    struct bright_edit *b = arg;
    if (!b->sched) s->brightness = b->level;
    else if (b->day) s->day_level = b->level;
    else s->night_level = b->level;
}

static void edit_disable(xy_settings_t *s, void *arg)
{
    const bool *dis = arg;
    for (int i = 0; i < XY_NUM_ALARMS; i++)
        if (dis[i]) s->alarm[i].enabled = false;
}

static void apply_tz(const xy_settings_t *s)
{
    setenv("TZ", s->tz[0] ? s->tz : "UTC0", 1);
    tzset();
}

/* Apply an alarm's GPIO14 output. persist: the level to save for after a
 * reboot (-1 = the level applied). While a "while ringing" hold is active the
 * stop level is saved, since a reboot ends the alarm. */
static void apply_g14(int8_t g, int8_t persist)
{
    if (g < 0) return;
    bool v = g == 2 ? !io_g14_get() : g == 1;
    bool keep = persist >= 0 ? persist == 1 : v;
    io_g14_set(v);
    store_update(edit_g14, &keep);
    lock(); s_cfg.g14_state = keep; unlock();
}

/* ---- the loop ---------------------------------------------------------- */

static void clock_task(void *arg)
{
    (void)arg;
    xy_btn_t btn;
    xy_btn_init(&btn, io_buttons_raw());
    xy_scroll_t scroll;
    bool scrolling = false;
    xy_sound_t sound;
    xy_sound_init(&sound);
    int64_t bright_save_at = 0;
    struct bright_edit pending_bright = {0};
    int refreshed_min = -1;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(LOOP_MS));
        int64_t now = mono_ms();

        /* Web-page changes. Alarms switched off or edited there end with their
         * OLD settings (so e.g. a "high while ringing" alarm still drives
         * GPIO14 low); only then are the new settings (and time zone) loaded. */
        xy_al_out_t out;
        lock();
        int cancel = s_cancel_mask;
        s_cancel_mask = 0;
        bool reload = s_cfg_dirty;
        s_cfg_dirty = false;
        unlock();

        for (int i = 0; i < XY_NUM_ALARMS; i++) {
            if (!(cancel & (1 << i))) continue;
            xy_alarms_cancel(&s_alarms, s_cfg.alarm, i, &out);
            apply_g14(out.g14, out.persist);
        }
        if (reload) {
            xy_settings_t s;
            store_get(&s);
            lock();
            s_cfg = s;
            xy_tt_set_interval(&s_tt, s.ntp_interval_s);
            unlock();
            if (strcmp(s.tz, s_cfg_tz_prev) != 0) {
                /* A new time zone moves local time; that is not a clock step
                 * to catch up on. */
                s_alarms.prev_day = -1;
                snprintf(s_cfg_tz_prev, sizeof s_cfg_tz_prev, "%s", s.tz);
            }
            apply_tz(&s);
        }

        struct timeval tv;
        gettimeofday(&tv, NULL);
        time_t t = tv.tv_sec;
        struct tm lt;
        localtime_r(&t, &lt);
        int64_t wall_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        int min_of_day = lt.tm_hour * 60 + lt.tm_min;

        lock();
        xy_trust_t trust = xy_tt_state(&s_tt, now / 1000);
        unlock();

        /* Buttons: every press beeps. */
        uint8_t pressed = xy_btn_update(&btn, io_buttons_raw());
        xy_key_t key = XY_KEY_NONE;
        if (pressed) {
            beeper_click();
            bool day = xy_brightness_is_day(&s_cfg, min_of_day);
            uint8_t *lvl = !s_cfg.sched ? &s_cfg.brightness
                         : day ? &s_cfg.day_level : &s_cfg.night_level;
            if (xy_alarms_owns_keys(&s_alarms, s_cfg.alarm)) {
                key = (pressed & XY_BTN_SET) ? XY_KEY_SNOOZE : XY_KEY_DISMISS;
            } else if (*lvl == 0) {
                /* Display off: any button only wakes it (design §6.3). */
                *lvl = 1;
                pending_bright = (struct bright_edit){ s_cfg.sched, day, 1 };
                bright_save_at = now + BRIGHT_SAVE_MS;
            } else if (pressed & (XY_BTN_UP | XY_BTN_DOWN)) {
                int dir = (pressed & XY_BTN_UP) ? +1 : -1;
                uint8_t nv = xy_brightness_step(*lvl, dir);
                if (s_cfg.sched && nv < 1) nv = 1;           /* scheduled levels are 1..8 */
                *lvl = nv;
                pending_bright = (struct bright_edit){ s_cfg.sched, day, nv };
                bright_save_at = now + BRIGHT_SAVE_MS;
            } else if (pressed & XY_BTN_SET) {
                char ip[20] = "no IP";
                if (s_ip_fn) s_ip_fn(ip, sizeof ip);
                xy_scroll_start(&scroll, ip, now);
                scrolling = true;
            }
        }
        if (bright_save_at && now >= bright_save_at) {
            bright_save_at = 0;
            store_update(edit_bright, &pending_bright);
        }

        /* Local calendar day number (days since 1970-01-01 in local time):
         * consecutive days differ by exactly 1, as alarm catch-up needs. */
        int y = lt.tm_year + 1900 - 1;
        int32_t local_day = (int32_t)((y - 1969) * 365 + (y - 1968) / 4 - (y - 1900) / 100 + (y - 1600) / 400
                                      + lt.tm_yday);
        /* Alarm timeouts and snoozes run on monotonic time. */
        xy_alarms_tick(&s_alarms, s_cfg.alarm, now / 1000, lt.tm_wday, lt.tm_hour, lt.tm_min, local_day,
                       trust != XY_TIME_UNTRUSTED, key, &out);
        apply_g14(out.g14, out.persist);
        bool any_disable = false;
        for (int i = 0; i < XY_NUM_ALARMS; i++) any_disable |= out.disable[i];
        if (any_disable) {
            store_update(edit_disable, out.disable);
            for (int i = 0; i < XY_NUM_ALARMS; i++)
                if (out.disable[i]) s_cfg.alarm[i].enabled = false;
            ESP_LOGI(TAG, "ring-once alarm finished and disabled");
        }
        beeper_pattern(out.sound_alarm);
        xy_snd_ev_t snd = xy_sound_tick(&sound, &s_cfg, t, lt.tm_hour, lt.tm_min, lt.tm_sec,
                                        trust != XY_TIME_UNTRUSTED, out.sound_alarm != 0);
        if (snd.kind != XY_SND_NONE) beeper_clock_sound(snd.kind, snd.count);
        beeper_tick(now);

        /* Display. */
        xy_frame_t f;
        lock();
        bool text = s_text_until_ms && now < s_text_until_ms;
        if (text) xy_render_text(&f, s_text);
        unlock();
        if (scrolling && !text) {
            scrolling = xy_scroll_frame(&scroll, now, &f);
            text = scrolling;
        }
        if (!text) {
            bool up, lo;
            if (trust == XY_TIME_UNTRUSTED) {
                xy_render_dashes(&f);
                up = lo = true;
            } else {
                xy_render_time(&f, lt.tm_hour, lt.tm_min, s_cfg.h24);
                xy_colon_dots(s_cfg.colon, s_cfg.stale, trust == XY_TIME_STALE, wall_ms, &up, &lo);
            }
            xy_frame_set_colon(&f, up, lo);
        }
        /* A marginal power connection can reset the TM1650 without resetting
         * the ESP.  Reassert its control register and complete frame once per
         * minute so stale driver caches cannot leave the display blank. */
        if (min_of_day != refreshed_min) {
            tm1650_invalidate();
            refreshed_min = min_of_day;
        }
        tm1650_set_level(xy_brightness_active(&s_cfg, min_of_day));
        tm1650_show(&f);

        /* LEDs: blue = WiFi connected (blinks in setup mode), red = time not trusted. */
        io_led_blue(s_ap_mode ? (now / 500) % 2 : s_wifi);
        io_led_red(trust != XY_TIME_TRUSTED);
    }
}

void clock_task_start(bool rtc_valid, bool ap_mode)
{
    s_lock = xSemaphoreCreateMutex();
    store_get(&s_cfg);
    snprintf(s_cfg_tz_prev, sizeof s_cfg_tz_prev, "%s", s_cfg.tz);
    apply_tz(&s_cfg);
    xy_tt_init(&s_tt, rtc_valid, s_cfg.ntp_interval_s);
    xy_alarms_init(&s_alarms);
    s_ap_mode = ap_mode;
    xTaskCreate(clock_task, "clock", 4096, NULL, 5, NULL);
}
