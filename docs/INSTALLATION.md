# Installation

The first installation is done over a serial adapter with
[esptool](https://github.com/espressif/esptool). After that, new firmware can
be uploaded from the clock's web page, and the serial adapter is only needed
for recovery.

All commands below are run from the root of this repository.

## Contents

1. [What you need](#1-what-you-need)
2. [Wiring](#2-wiring)
3. [Entering flash mode](#3-entering-flash-mode)
4. [Back up the stock firmware](#4-back-up-the-stock-firmware)
5. [First flash](#5-first-flash)
6. [First setup](#6-first-setup)
7. [Later updates from the web page](#7-later-updates-from-the-web-page)
8. [Restoring the stock firmware](#8-restoring-the-stock-firmware)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. What you need

- A **3.3 V** USB-to-serial adapter (5 V will destroy the ESP8285), with
  jumper wires.
- esptool on the computer. For example, in a Python virtual environment:

  ```sh
  python3 -m venv ~/esptool-venv
  ~/esptool-venv/bin/pip install esptool
  source ~/esptool-venv/bin/activate      # puts esptool on the PATH
  ```

  The commands below use esptool v4.8 / v5 syntax (`write-flash`,
  `read-flash`). Older versions spell them with underscores (`write_flash`),
  and may be called as `esptool.py`.
- The firmware files, from a build (see [Development](DEVELOPMENT.md)):

  | File | Flash address |
  |---|---|
  | `build/bootloader/bootloader.bin` | `0x0` |
  | `build/partition_table/partition-table.bin` | `0x8000` |
  | `build/ota_data_initial.bin` | `0xd000` |
  | `build/xyclock.bin` | `0x10000` |

## 2. Wiring

Header **J3** on the clock board (see [Hardware](HARDWARE.md#serial-header-j3)):

| J3 pin | Clock   | Adapter                                   |
|--------|---------|-------------------------------------------|
| 1      | GND     | GND                                       |
| 2      | TX      | RX                                        |
| 3      | RX      | TX                                        |
| 4      | GPIO0   | jumper to GND to enter flash mode         |
| 5      | CHIP_EN | leave free (touch to GND briefly = reset) |
| 6      | VCC     | 3.3 V                                     |

Power the board **either** from the adapter's 3.3 V on pin 6 **or** from the
clock's own 5 V input, not both at once.

Find the adapter's port with `ls /dev/ttyUSB* /dev/ttyACM*`. The examples use
`/dev/ttyUSB0`.

## 3. Entering flash mode

The header has no automatic reset wiring, so entering flash mode is manual:

1. Connect GPIO0 (J3 pin 4) to GND.
2. Power the board up, or if it is already powered, touch CHIP_EN (pin 5) to
   GND briefly.

The chip now waits for esptool. GPIO0 can stay grounded until the esptool
command finishes. Each esptool command below resets the chip when it is done,
so **re-enter flash mode before each one**.

## 4. Back up the stock firmware

The stock firmware cannot be downloaded anywhere, so save your own copy before
flashing anything. In flash mode:

```sh
esptool --port /dev/ttyUSB0 --baud 115200 read-flash 0 0x200000 stock-flash.bin
```

This reads the whole 2 MB flash (a few minutes). Keep the file somewhere safe:
it is specific to your clock, as it includes the clock's WiFi calibration data.

## 5. First flash

Erase the flash once, so that no stock settings remain. In flash mode:

```sh
esptool --port /dev/ttyUSB0 erase-flash
```

Re-enter flash mode, then write the four files:

```sh
esptool --port /dev/ttyUSB0 --baud 230400 write-flash \
  --flash-mode dout --flash-freq 40m --flash-size 2MB \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xd000  build/ota_data_initial.bin \
  0x10000 build/xyclock.bin
```

(With esptool older than v4.8, write `--flash_mode`, `--flash_freq` and
`--flash_size`.) The flash mode **must be `dout`**: the flash's other data
lines are wired to buttons on this board, and any other mode will not boot.

The build also writes these arguments to `build/flash_project_args`, so the
same flash can be done with:

```sh
cd build && esptool --port /dev/ttyUSB0 --baud 230400 write-flash @flash_project_args; cd ..
```

If you have the build container, `tools/idf -p /dev/ttyUSB0 flash` does the
same thing from inside it (see [Development](DEVELOPMENT.md#flashing-and-the-serial-monitor)).

When esptool finishes, remove the GPIO0-to-GND jumper and reset or power-cycle
the clock. It starts in setup mode: the display shows `AP` and the blue LED
blinks.

## 6. First setup

Join the WiFi network `XYClock-XXXX` (password `xyclock123`), open
http://192.168.4.1, choose a time zone, your network and a web page password,
and save. The [user manual](USER_MANUAL.md#2-first-setup) covers this step by
step.

The serial adapter can now be disconnected.

## 7. Later updates from the web page

Once this firmware is installed, updates no longer need the serial adapter:

1. Build the new firmware. Only `build/xyclock.bin` is needed.
   `python3 -I tools/check_image.py build/xyclock.bin` confirms the file
   carries its SHA-256, which the clock checks.
2. Open the clock's page, go to **Firmware**, choose `xyclock.bin` and press
   **Upload firmware**. The display shows `UPd`.
3. If the file is accepted, the clock restarts into it, and the **Firmware**
   section shows the new version. Settings are kept.

The clock writes the upload into its spare firmware slot and only switches to
it if the whole file arrived and is a valid `xyclock` image; otherwise it keeps
running the current firmware. Details are in the
[user manual](USER_MANUAL.md#9-firmware-updates).

There is no automatic fallback if a new firmware fails to start. In that case,
flash over serial again as in [First flash](#5-first-flash) (the erase can be
skipped to keep the settings).

## 8. Restoring the stock firmware

In flash mode, write your backup back:

```sh
esptool --port /dev/ttyUSB0 --baud 115200 write-flash 0 stock-flash.bin
```

## 9. Troubleshooting

**"Permission denied" on /dev/ttyUSB0.**
Check `ls -l /dev/ttyUSB0`. If the device belongs to group `dialout` and you
are not in it: `sudo usermod -aG dialout $USER`, then log out and back in. To
use it straight away, run `newgrp dialout`; that shell (and anything started
from it) then has the group.

**"Failed to connect" / "Timed out waiting for packet header".**
The chip is not in flash mode: check GPIO0 is grounded, reset (CHIP_EN to GND
briefly, or power-cycle) and try again. Check TX and RX are crossed (clock TX
to adapter RX).

**Serial noise or corruption errors while flashing.**
Retry slower, with `--baud 115200`.

**The clock does not start after flashing.**
Check that GPIO0 is no longer grounded, then reset. Check that the flash was
written with `--flash-mode dout`. To see what the clock is doing, watch its
serial output at 115200 baud (see
[Development](DEVELOPMENT.md#flashing-and-the-serial-monitor)).
