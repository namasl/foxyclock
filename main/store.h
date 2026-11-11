#ifndef STORE_H
#define STORE_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "xy_settings.h"

/* Initialise NVS and load settings (defaults if missing or invalid). */
esp_err_t store_init(void);
void store_get(xy_settings_t *out);
/* Validate, save to flash and make current. ESP_ERR_INVALID_ARG if invalid. */
esp_err_t store_put(const xy_settings_t *s);
/* Update in RAM and flash without full validation of unrelated fields,
 * for small runtime changes (brightness, GPIO14, alarm auto-disable).
 * The callback edits the current settings in place under the lock. */
esp_err_t store_update(void (*edit)(xy_settings_t *s, void *arg), void *arg);
/* True if settings came from flash (false = defaults, first boot). */
bool store_loaded(void);
esp_err_t store_factory_reset(void);
/* Settings and their revision, read together. The revision increments on
 * every change (web save or runtime update), letting the web page detect
 * that it is showing stale settings. */
void store_get_rev(xy_settings_t *out, uint32_t *rev);
/* Save only if the settings are still at revision rev (compare-and-save under
 * one lock). ESP_ERR_INVALID_VERSION if they changed in between. */
esp_err_t store_put_if_rev(const xy_settings_t *s, uint32_t rev);
/* True if the last runtime update could not be written to flash. */
bool store_save_failed(void);

#endif
