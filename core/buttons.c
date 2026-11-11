#include "xy_buttons.h"

void xy_btn_init(xy_btn_t *b, uint8_t raw)
{
    b->stable = raw;
    b->last_raw = raw;
}

uint8_t xy_btn_update(xy_btn_t *b, uint8_t raw)
{
    uint8_t settled = (uint8_t)~(raw ^ b->last_raw);     /* bits equal in both samples */
    uint8_t next = (uint8_t)((b->stable & ~settled) | (raw & settled));
    uint8_t pressed = (uint8_t)(next & ~b->stable);
    b->stable = next;
    b->last_raw = raw;
    return pressed;
}

xy_boot_t xy_boot_classify(uint8_t held_mask_at_start, uint32_t held_ms_both_updown)
{
    const uint8_t both = XY_BTN_UP | XY_BTN_DOWN;
    if ((held_mask_at_start & both) == both && held_ms_both_updown >= XY_FACTORY_HOLD_MS)
        return XY_BOOT_FACTORY;
    return held_mask_at_start ? XY_BOOT_AP : XY_BOOT_NORMAL;
}

uint8_t xy_brightness_step(uint8_t level, int dir)
{
    int v = (int)level + (dir > 0 ? 1 : dir < 0 ? -1 : 0);
    if (v < 0) v = 0;
    if (v > 8) v = 8;
    return (uint8_t)v;
}

bool xy_brightness_is_day(const xy_settings_t *s, int m)
{
    int d = s->day_start_min, n = s->night_start_min;
    if (d == n) return true;
    return d < n ? (m >= d && m < n) : (m >= d || m < n);
}

uint8_t xy_brightness_active(const xy_settings_t *s, int m)
{
    if (!s->sched) return s->brightness;
    return xy_brightness_is_day(s, m) ? s->day_level : s->night_level;
}
