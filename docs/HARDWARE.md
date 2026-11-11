# Hardware

The Sinilink XY-Clock, as used by the FOXY Clock firmware. The pin definitions used by the firmware are in [`main/board.h`](../main/board.h).

## Contents

1. [Overview](#overview)
2. [GPIO pinout](#gpio-pinout)
3. [I2C bus](#i2c-bus)
4. [Display (TM1650)](#display-tm1650)
5. [Real-time clock (DS1307)](#real-time-clock-ds1307)
6. [Buttons](#buttons)
7. [LEDs](#leds)
8. [Beeper](#beeper)
9. [Headers](#headers)

---

## Overview

| Part | Function | Datasheet |
|---|---|---|
| ESP8285H16 | ESP8266-class WiFi microcontroller with 2 MB of internal flash; 26 MHz crystal, run at 160 MHz | [ESP8285](https://documentation.espressif.com/0a-esp8285_datasheet_en.html) (Espressif) |
| DS1307-compatible RTC | battery-backed real-time clock, holder for a CR927 cell | [DS1307](https://www.analog.com/media/en/technical-documentation/data-sheets/DS1307.pdf) (Analog Devices / Maxim) |
| TM1650 | LED display driver for the 4-digit, 7-segment display with a colon | [TM1650](https://www.lcsc.com/datasheet/C2887173.pdf) (Titan Micro Electronics, via LCSC) |

The board runs from a 5 V input; the ESP8285 runs at 3.3 V.

## GPIO pinout

| GPIO | Function | Direction | Notes |
|---|---|---|---|
| 0 | blue LED | output, active low | boot strap: low at reset = flash mode (J3 pin 4) |
| 2 | red LED | output, active low | boot strap: must be high at reset |
| 4 | J2 "PIN2" | — | spare, unused by this firmware |
| 5 | beeper | output (PWM) | through an S8050 NPN transistor |
| 9 | DOWN button | input, active low | internal pull-up |
| 10 | UP button | input, active low | internal pull-up |
| 12 | I2C SCL | open drain | 4.7 kΩ external pull-up |
| 13 | I2C SDA | open drain | 4.7 kΩ external pull-up |
| 14 | J2 "KEY" | output | user output, 3.3 V logic |
| 16 | SET button | input, active low | external pull-up; no interrupt, so polled |
| TX / RX | serial console | — | J3 pins 2 and 3, 115200 baud |

**GPIO9 and GPIO10** are normally the flash chip's quad-SPI data lines
(WP/HD). Because they are used as buttons here, the flash **must be run in
DOUT mode**; QIO or DIO firmware will not boot.

**GPIO0 and GPIO2** are boot-strap pins. With the LEDs active low, both sit
high (LEDs off) at reset, which gives a normal boot. Grounding GPIO0 at reset
enters the ROM bootloader for flashing.

## I2C bus

A single bus, GPIO12 = SCL and GPIO13 = SDA, with 4.7 kΩ external pull-ups,
is shared by the display driver and the RTC. The firmware uses the ESP8266
software I2C master and serialises access with a lock.

| Device | Addressing |
|---|---|
| TM1650 | no device address: the first byte is the register (see below) |
| DS1307 | standard I2C, 7-bit address `0x68` |

The two do not collide: the TM1650's register bytes (`0x48`, `0x68`-`0x6E`)
are written as the first byte after START, where they would read as 8-bit I2C
write addresses `0x24` and `0x34`-`0x37`, not the DS1307's `0xD0`/`0xD1`.

## Display (TM1650)

A 4-digit, 7-segment display with a two-dot colon between digits 2 and 3,
driven by a TM1650.

| Register | Function |
|---|---|
| `0x48` | control: bits 6-4 brightness (1-7, and 0 = level 8), bit 0 = display on |
| `0x68` | digit 1 (leftmost) |
| `0x6A` | digit 2 |
| `0x6C` | digit 3 |
| `0x6E` | digit 4 (rightmost) |

In a digit register, bits 0-6 are segments A-G in the standard order. Bit 7
(the "DP" bit) is cross-wired on this board:

| Bit 7 of | Lights |
|---|---|
| digit 1 register | decimal point of digit **3** |
| digit 2 register | decimal point of digit 2 |
| digit 3 register | **upper** colon dot |
| digit 4 register | **lower** colon dot |

So the two colon dots can be controlled separately, and digits 1 and 4 have
no usable decimal point.

There are 8 brightness levels. The firmware's "off" clears bit 0 of the
control register.

The TM1650 runs from the 5 V supply. A marginal 5 V connection can reset it
without resetting the ESP8285, which blanks the display; the firmware rewrites
the control register and all four digits at every minute change to recover
from this.

## Real-time clock (DS1307)

A DS1307-compatible RTC at I2C address `0x68`, powered from 5 V, with a
holder for a **CR927** (3 V lithium, non-rechargeable) backup cell.

- With no battery fitted, the RTC loses the time at every power cut and comes
  up with the oscillator-halt (CH) bit set and the date 2000-01-01. The
  firmware only trusts the RTC when CH is clear and every field is a valid
  date and time.
- The firmware stores UTC in the RTC (24-hour mode), and writes it after each
  successful NTP sync, which also clears CH.
- The chip's 56 bytes of battery-backed RAM are not used. Settings are stored
  in the ESP8285's flash.

## Buttons

Three push buttons to ground (active low):

| Button | GPIO | Pull-up |
|---|---|---|
| UP | 10 | ESP8285 internal |
| DOWN | 9 | ESP8285 internal |
| SET | 16 | external, on the board |

GPIO16 has no interrupt capability on the ESP8266 family, so the firmware
polls all three buttons every 20 ms and debounces them in software.

## LEDs

| LED | GPIO | Drive |
|---|---|---|
| blue | 0 | active low |
| red | 2 | active low |

## Beeper

A **passive** beeper on GPIO5, switched by an S8050 NPN transistor. It needs a
square wave: holding GPIO5 high makes no sound. The firmware drives it with
the SDK's PWM at 50 % duty, and leaves the pin low (transistor off) when
silent.

Tones used by the firmware:

| Sound | Frequency | Length |
|---|---|---|
| button press | 4 kHz | 30 ms |
| alarm | 2.7 kHz | N × (150 ms on, 150 ms off), then 1 s pause; N = alarm number |
| tick | 3 kHz | 4 ms |
| minute beep | 2 kHz | 120 ms |
| half-hour chime | 1.5 kHz | 150 ms |
| hour chime | 1 kHz | 450 ms on, 550 ms off, once per hour struck |

## Headers

### Serial header (J3)

| Pin | Signal |
|---|---|
| 1 | GND |
| 2 | TX (ESP8285 output) |
| 3 | RX (ESP8285 input) |
| 4 | GPIO0 |
| 5 | CHIP_EN |
| 6 | VCC, 3.3 V |

There is no automatic reset wiring: flash mode is entered by grounding GPIO0
and resetting (CHIP_EN briefly to GND, or power-cycling). See
[Installation](INSTALLATION.md#2-wiring).

### Spare header (J2)

| Label | GPIO | Use in this firmware |
|---|---|---|
| KEY | 14 | user output, set from the web page or by alarms |
| PIN2 | 4 | unused |

GPIO14 is a 3.3 V logic output. It can drive a relay **module** or a
transistor or optocoupler input, but not a load such as a relay coil, LED
strip or motor directly. It is not connected to any button.

At power-up GPIO14 is undriven (an input) for about half a second until the
firmware drives it to its saved level; fit a pull-up or pull-down on the
connected device so it sees a defined level meanwhile. See the
[user manual](USER_MANUAL.md#8-gpio14-output) for its behaviour.
