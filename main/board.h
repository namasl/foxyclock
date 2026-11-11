/* Sinilink XY-Clock pin map (verified on hardware 2026-11-09). */
#ifndef BOARD_H
#define BOARD_H
#include "driver/gpio.h"

#define PIN_LED_BLUE   GPIO_NUM_0    /* active low */
#define PIN_LED_RED    GPIO_NUM_2    /* active low */
#define PIN_BUZZER     GPIO_NUM_5    /* S8050 NPN, passive beeper: PWM only */
#define PIN_K_DOWN     GPIO_NUM_9    /* active low */
#define PIN_K_UP       GPIO_NUM_10   /* active low */
#define PIN_G14        GPIO_NUM_14   /* user output on header J2 "KEY" */
#define PIN_K_SET      GPIO_NUM_16   /* active low, external pull-up, no interrupt */
#define PIN_SCL        GPIO_NUM_12
#define PIN_SDA        GPIO_NUM_13

#define I2C_PORT       I2C_NUM_0
#define DS1307_ADDR    0x68

#endif
