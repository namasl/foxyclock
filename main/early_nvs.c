/* Boot-time storage recovery.
 *
 * The SDK's startup code calls esp_mac_init() before app_main(), and asserts
 * on its result. esp_mac_init() starts NVS, so a damaged settings partition
 * would stop the boot (a reboot loop) before our own recovery code or the
 * UP+DOWN factory-reset hold could run. This wrapper (linked with
 * -Wl,--wrap=esp_mac_init) erases and remounts NVS on any mount failure first,
 * and never lets a MAC-backup problem stop the boot: the factory MAC from the
 * eFuse is used either way. */
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

esp_err_t __real_esp_mac_init(void);

esp_err_t __wrap_esp_mac_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW("early_nvs", "settings storage unusable (%s); erasing", esp_err_to_name(err));
        nvs_flash_erase();
        nvs_flash_init();
    }
    if (__real_esp_mac_init() != ESP_OK)
        ESP_LOGW("early_nvs", "MAC backup unavailable; using eFuse MAC");
    return ESP_OK;
}
