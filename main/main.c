/* FOXY Clock: custom firmware for the Sinilink XY-Clock. See docs/. */
#include <sys/time.h>
#include "beeper.h"
#include "clock_task.h"
#include "ds1307.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "io.h"
#include "net.h"
#include "web.h"
#include "store.h"
#include "tm1650.h"
#include "xy_buttons.h"
#include "xy_display.h"

static const char *TAG = "main";

/* Power-on hold: sample buttons; if UP+DOWN stay held, time the hold (up to
 * the factory-reset threshold) while showing a countdown hint. */
static xy_boot_t read_boot_hold(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));                  /* let pull-ups settle */
    uint8_t held = io_buttons_raw();
    if (!held) return XY_BOOT_NORMAL;
    uint32_t both_ms = 0;
    const uint8_t both = XY_BTN_UP | XY_BTN_DOWN;
    if ((held & both) == both) {
        xy_frame_t f;
        xy_render_text(&f, "rSt?");
        tm1650_show(&f);
        while ((io_buttons_raw() & both) == both && both_ms < XY_FACTORY_HOLD_MS) {
            vTaskDelay(pdMS_TO_TICKS(50));
            both_ms += 50;
        }
    }
    return xy_boot_classify(held, both_ms);
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_ota_get_app_description();
    ESP_LOGI(TAG, "FOXY Clock %s (built %s %s)", app->version, app->date, app->time);
    io_init();
    i2c_bus_init();
    tm1650_init();
    beeper_init();

    xy_boot_t boot = read_boot_hold();
    store_init();          /* on failure it runs on defaults; never stops the boot */

    if (boot == XY_BOOT_FACTORY) {
        ESP_LOGW(TAG, "factory reset requested");
        xy_frame_t f;
        xy_render_text(&f, "rSt");
        tm1650_show(&f);
        if (store_factory_reset() == ESP_OK) {
            beeper_beeps(3);
        } else {
            xy_render_text(&f, "Err");     /* reset failed: say so, and do not pretend */
            tm1650_show(&f);
            beeper_beeps(6);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    xy_settings_t s;
    store_get(&s);
    io_g14_init(s.g14_state);

    time_t t;
    bool rtc_valid = false;
    if (ds1307_read_utc(&t, &rtc_valid) == ESP_OK && rtc_valid) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "time from RTC: %ld", (long)t);
    } else {
        ESP_LOGW(TAG, "RTC time not valid (no battery or never set)");
    }

    bool ap_mode = boot == XY_BOOT_AP || s.ssid[0] == 0;
    if (boot == XY_BOOT_AP) beeper_beeps(2);
    clock_task_start(rtc_valid, ap_mode);
    if (ap_mode) clock_show_text("AP", 0);
    net_start(ap_mode);
    web_start();
    ESP_LOGI(TAG, "started (%s), heap free %u", ap_mode ? "setup AP mode" : "normal",
             (unsigned)esp_get_free_heap_size());
}
