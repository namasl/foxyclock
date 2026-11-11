/* Button debouncing, power-on hold classification and brightness logic. */
#ifndef XY_BUTTONS_H
#define XY_BUTTONS_H
#include <stdbool.h>
#include <stdint.h>
#include "xy_settings.h"

enum { XY_BTN_UP = 1, XY_BTN_DOWN = 2, XY_BTN_SET = 4 };

typedef struct { uint8_t stable, last_raw; } xy_btn_t;

/* raw: bitmask of buttons currently pressed. Buttons already held at init
 * do not produce a press until released and pressed again. */
void xy_btn_init(xy_btn_t *b, uint8_t raw);
/* Call every 20 ms. A change counts after two equal consecutive samples.
 * Returns the buttons that went from released to pressed on this call. */
uint8_t xy_btn_update(xy_btn_t *b, uint8_t raw);

typedef enum { XY_BOOT_NORMAL, XY_BOOT_AP, XY_BOOT_FACTORY } xy_boot_t;
#define XY_FACTORY_HOLD_MS 3000
/* held_mask_at_start: buttons held at power-on. held_ms_both_updown: how
 * long UP and DOWN stayed held together from power-on. */
xy_boot_t xy_boot_classify(uint8_t held_mask_at_start, uint32_t held_ms_both_updown);

/* Brightness: 0 = display off, 1..8. */
uint8_t xy_brightness_step(uint8_t level, int dir);
/* With a schedule: true during [day_start_min, night_start_min), wrapping. */
bool xy_brightness_is_day(const xy_settings_t *s, int local_min_of_day);
uint8_t xy_brightness_active(const xy_settings_t *s, int local_min_of_day);

#endif
