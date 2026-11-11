#include "tm1650.h"
#include <string.h>
#include "i2c_bus.h"

#define TM1650_CTRL    0x48
#define TM1650_DIG(n)  (0x68 + 2 * (n))

static xy_frame_t s_last;
static bool s_have_last;
static int s_level = -1;

esp_err_t tm1650_init(void)
{
    s_have_last = false;
    s_level = -1;
    return tm1650_set_level(4);
}

void tm1650_invalidate(void)
{
    /* A marginal 5 V connection can reset the TM1650 without resetting the
     * ESP, leaving both software caches stale. */
    s_have_last = false;
    s_level = -1;
}

esp_err_t tm1650_set_level(uint8_t level)
{
    if (level == s_level) return ESP_OK;
    /* Bits 6:4 brightness (8 is encoded as 0), bit 0 display on. */
    uint8_t v = level ? (uint8_t)(((level & 7) << 4) | 0x01) : 0x00;
    i2c_bus_lock();
    esp_err_t err = i2c_bus_write(TM1650_CTRL, &v, 1);
    i2c_bus_unlock();
    if (err == ESP_OK) s_level = level;
    return err;
}

esp_err_t tm1650_show(const xy_frame_t *f)
{
    esp_err_t err = ESP_OK;
    i2c_bus_lock();
    for (int i = 0; i < 4; i++) {
        if (s_have_last && s_last.seg[i] == f->seg[i]) continue;
        esp_err_t e = i2c_bus_write(TM1650_DIG(i), &f->seg[i], 1);
        if (e != ESP_OK) err = e;
    }
    i2c_bus_unlock();
    if (err == ESP_OK) { s_last = *f; s_have_last = true; }
    else s_have_last = false;
    return err;
}
