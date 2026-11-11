#ifndef TM1650_H
#define TM1650_H
#include <stdint.h>
#include "esp_err.h"
#include "xy_display.h"

esp_err_t tm1650_init(void);
/* Invalidates the control and frame caches so the next calls rewrite them. */
void tm1650_invalidate(void);
/* Writes digit registers that changed, or all four after invalidation. */
esp_err_t tm1650_show(const xy_frame_t *f);
/* level 0 = display off, 1..8 brightness. */
esp_err_t tm1650_set_level(uint8_t level);

#endif
