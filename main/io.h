#ifndef IO_H
#define IO_H
#include <stdbool.h>
#include <stdint.h>

void io_init(void);
/* Bitmask of pressed buttons (XY_BTN_UP / DOWN / SET). */
uint8_t io_buttons_raw(void);
void io_led_blue(bool on);
void io_led_red(bool on);
/* First drive of GPIO14 at boot, straight to its saved level. */
void io_g14_init(bool high);
void io_g14_set(bool high);
bool io_g14_get(void);

#endif
