#include "store.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "store";
#define NS  "xyclock"
#define KEY "cfg"

static SemaphoreHandle_t s_lock;
static xy_settings_t s_cur;
static bool s_loaded;
static uint32_t s_rev = 1;
static bool s_save_failed;     /* last runtime save failed: RAM differs from flash */

static esp_err_t save(const xy_settings_t *s)
{
    nvs_handle h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, KEY, s, sizeof *s);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t store_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Any failure to mount the settings area (e.g. ESP_ERR_NVS_NO_FREE_PAGES,
         * or corruption): erase it and start again, so the clock always boots
         * and the factory-reset hold stays usable. */
        ESP_LOGW(TAG, "settings storage unusable (%s); erasing", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    xy_settings_defaults(&s_cur);
    s_loaded = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings storage failed (%s); running on defaults, nothing will be saved",
                 esp_err_to_name(err));
        return err;
    }

    nvs_handle h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        static xy_settings_t raw, tmp;
        size_t len = sizeof raw;
        esp_err_t e = nvs_get_blob(h, KEY, &raw, &len);   /* fails if the blob is larger */
        nvs_close(h);
        if (e == ESP_OK && xy_settings_from_blob(&raw, len, &tmp)) {
            s_cur = tmp;
            s_loaded = true;
            if (raw.version != XY_SETTINGS_VERSION) {
                ESP_LOGI(TAG, "upgrading stored settings v%u -> v%u", raw.version, XY_SETTINGS_VERSION);
                save(&s_cur);
            }
        } else if (e == ESP_OK) {
            ESP_LOGW(TAG, "stored settings (v%u, %u bytes) not usable; using defaults",
                     raw.version, (unsigned)len);
        }
    }
    ESP_LOGI(TAG, "settings %s", s_loaded ? "loaded" : "defaults (first setup)");
    return ESP_OK;
}

void store_get(xy_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cur;
    xSemaphoreGive(s_lock);
}

bool store_loaded(void) { return s_loaded; }

esp_err_t store_put(const xy_settings_t *s)
{
    if (xy_settings_validate(s)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = save(s);
    if (err == ESP_OK) { s_cur = *s; s_loaded = true; s_rev++; s_save_failed = false; }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t store_update(void (*edit)(xy_settings_t *s, void *arg), void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    edit(&s_cur, arg);
    s_rev++;
    /* Before first setup the settings are incomplete (no time zone): keep
     * runtime changes in RAM only. */
    esp_err_t err = s_loaded ? save(&s_cur) : ESP_OK;
    /* The change stays in effect (it is already visible on the clock) but is
     * reported as unsaved; a later successful save clears the flag. */
    s_save_failed = err != ESP_OK;
    xSemaphoreGive(s_lock);
    return err;
}

bool store_save_failed(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool f = s_save_failed;
    xSemaphoreGive(s_lock);
    return f;
}

void store_get_rev(xy_settings_t *out, uint32_t *rev)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cur;
    *rev = s_rev;
    xSemaphoreGive(s_lock);
}

esp_err_t store_put_if_rev(const xy_settings_t *s, uint32_t rev)
{
    if (xy_settings_validate(s)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err;
    if (rev != s_rev) {
        err = ESP_ERR_INVALID_VERSION;          /* changed since the caller read it */
    } else {
        err = save(s);
        if (err == ESP_OK) { s_cur = *s; s_loaded = true; s_rev++; s_save_failed = false; }
    }
    xSemaphoreGive(s_lock);
    return err;
}


esp_err_t store_factory_reset(void)
{
    /* The RAM copy is dropped only once flash has really been erased: if the
     * erase fails the clock keeps its settings (including the web password)
     * instead of running unprotected on defaults. Held under the lock, so a
     * runtime update cannot re-save the old settings in between. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        /* Fall back to erasing the whole settings partition, then remount it
         * so later saves still work if even that fails. */
        ESP_LOGW(TAG, "factory reset: erase failed (%s); erasing partition", esp_err_to_name(err));
        nvs_flash_deinit();
        err = nvs_flash_erase();
        nvs_flash_init();
    }
    if (err == ESP_OK) {
        xy_settings_defaults(&s_cur);
        s_loaded = false;
        s_rev++;
    } else {
        ESP_LOGE(TAG, "factory reset FAILED: %s; settings kept", esp_err_to_name(err));
    }
    xSemaphoreGive(s_lock);
    return err;
}
