#include "io.h"
#include "board.h"
#include "xy_buttons.h"

static bool s_g14;

void io_init(void)
{
    gpio_set_level(PIN_LED_BLUE, 1);
    gpio_set_level(PIN_LED_RED, 1);
    gpio_set_level(PIN_BUZZER, 0);
    gpio_config_t out = {
        .pin_bit_mask = BIT(PIN_LED_BLUE) | BIT(PIN_LED_RED) | BIT(PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    /* GPIO14 is left as it is (an input since reset) until io_g14_init()
     * can drive it straight to its saved level: no pulse in between. */
    gpio_config_t in = {
        .pin_bit_mask = BIT(PIN_K_DOWN) | BIT(PIN_K_UP),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);
    gpio_config_t set = {               /* GPIO16: no internal pull-up; board has one */
        .pin_bit_mask = BIT(PIN_K_SET),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&set);
}

uint8_t io_buttons_raw(void)
{
    uint8_t m = 0;
    if (!gpio_get_level(PIN_K_UP)) m |= XY_BTN_UP;
    if (!gpio_get_level(PIN_K_DOWN)) m |= XY_BTN_DOWN;
    if (!gpio_get_level(PIN_K_SET)) m |= XY_BTN_SET;
    return m;
}

void io_led_blue(bool on) { gpio_set_level(PIN_LED_BLUE, on ? 0 : 1); }
void io_led_red(bool on) { gpio_set_level(PIN_LED_RED, on ? 0 : 1); }
void io_g14_init(bool high)
{
    /* Latch the level first, then enable the output driver. */
    gpio_set_level(PIN_G14, high ? 1 : 0);
    gpio_config_t g = { .pin_bit_mask = BIT(PIN_G14), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&g);
    gpio_set_level(PIN_G14, high ? 1 : 0);
    s_g14 = high;
}

void io_g14_set(bool high) { s_g14 = high; gpio_set_level(PIN_G14, high ? 1 : 0); }
bool io_g14_get(void) { return s_g14; }
