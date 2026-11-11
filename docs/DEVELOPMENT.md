# Development

Building, testing and working on the FOXY Clock firmware. The firmware is built with
[ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK) v3.4 inside a
container, so nothing but the container tool is installed on the host.

All commands below are run from the root of this repository.

## Contents

1. [Requirements](#requirements)
2. [The build container](#the-build-container)
3. [Building](#building)
4. [Versions and releases](#versions-and-releases)
5. [Flashing and the serial monitor](#flashing-and-the-serial-monitor)
6. [Tests](#tests)
7. [Code layout](#code-layout)
8. [Web API](#web-api)
9. [Adding a setting](#adding-a-setting)
10. [Build notes](#build-notes)
11. [Troubleshooting](#troubleshooting)

---

## Requirements

- Linux x86_64 (other platforms may work but are untested)
- [Podman](https://podman.io/) (recommended; the `tools/idf` wrapper uses it) or
  [Docker](https://www.docker.com/)
- About 3 GB of free disk space for the container image
- `gcc` and `make` on the host, for the unit tests
- For flashing: the serial adapter and wiring described in
  [Installation](INSTALLATION.md)

## The build container

Everything (SDK, GCC 8.4 lx106 toolchain, Python 3.8 venv) lives in the
container image `localhost/xyclock-rtos-sdk:3.4`, defined in
`tools/container/Containerfile`. It is based on Ubuntu 20.04, whose Python
satisfies the SDK's old pinned requirements; the SDK is pinned to the v3.4
release commit and the toolchain download is checksum-verified.

There is no long-running container to start or stop. Each command starts a
fresh container and removes it afterwards (`run --rm`). The repository is
mounted inside it at `/project`, and commands run as your own user, so build
output in `build/` is owned by you. ccache keeps its cache in `.ccache/` in
the repository.

### Podman, with the wrapper script

`tools/idf <args>` runs `idf.py <args>` in the container. It builds the image
automatically on first use (a few minutes), and passes through any
`/dev/ttyUSB*` / `/dev/ttyACM*` serial adapters that are plugged in when it
starts.

```sh
tools/idf --image        # build the image (automatic on first use; rerun only
                         # if the Containerfile changes)
tools/idf build          # build firmware -> build/xyclock.bin
tools/idf menuconfig     # interactive configuration
tools/idf --shell        # bash shell inside a container ("exit" to leave)
tools/idf fullclean      # wipe build/
podman images localhost/xyclock-rtos-sdk   # check the image exists
```

Commands can be combined, e.g. `tools/idf -p /dev/ttyUSB0 flash monitor`.

### Podman or Docker, without the wrapper

Build the image:

```sh
podman build -t localhost/xyclock-rtos-sdk:3.4 \
  -f tools/container/Containerfile tools/container
```

With Docker, replace `podman` with `docker`.

Run `idf.py` with Podman:

```sh
podman run --rm -it --userns=keep-id --security-opt label=disable \
  -v "$PWD":/project -w /project \
  localhost/xyclock-rtos-sdk:3.4 idf.py build
```

Docker has no `--userns=keep-id`. Pass your user and group IDs instead, so the
files in `build/` are owned by you rather than root:

```sh
docker run --rm -it --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD":/project -w /project \
  localhost/xyclock-rtos-sdk:3.4 idf.py build
```

For `flash` or `monitor`, also pass the serial adapter into the container.
With Podman, add `--device /dev/ttyUSB0 --group-add keep-groups`. With Docker,
add `--device /dev/ttyUSB0 --group-add "$(stat -c %g /dev/ttyUSB0)"` (the group
that owns the device). Then use, for example, `idf.py -p /dev/ttyUSB0 flash`.

If SELinux is enforcing and the container cannot read the mounted repository,
keep `--security-opt label=disable` (Podman) or mount with
`-v "$PWD":/project:Z`.

### Reading SDK sources

The SDK's APIs differ from current ESP-IDF. To check a real header:

```sh
podman run --rm localhost/xyclock-rtos-sdk:3.4 \
  cat /opt/esp/ESP8266_RTOS_SDK/components/<path>
```

### Copying the image to another machine

To skip the rebuild, or to use a machine without internet access:

```sh
podman save localhost/xyclock-rtos-sdk:3.4 | gzip > xyclock-rtos-sdk-3.4.tar.gz   # export
gunzip -c xyclock-rtos-sdk-3.4.tar.gz | podman load                               # import
```

Docker uses the same `save` / `load` commands, and either can load an image
the other saved.

## Building

```sh
tools/idf build
python3 -I tools/check_image.py build/xyclock.bin   # prints the version, checks the SHA-256
```

Output in `build/`:

| File | Flash address | Use |
|---|---|---|
| `bootloader/bootloader.bin` | `0x0` | serial flash only |
| `partition_table/partition-table.bin` | `0x8000` | serial flash only |
| `ota_data_initial.bin` | `0xd000` | serial flash only |
| `xyclock.bin` | `0x10000` | serial flash, and web page updates |

A change is done when the host tests pass and the firmware builds with no
warnings in `core/` or `main/`.

### Board configuration

Board settings are in `sdkconfig.defaults`:

- DOUT flash mode: required, because GPIO9 and GPIO10 (normally the flash's
  quad data lines) are buttons (see [Hardware](HARDWARE.md#gpio-pinout))
- 2 MB flash at 40 MHz
- 26 MHz crystal, 160 MHz CPU
- Partition table with two OTA app slots
- Serial console at 115200 baud; esptool at 230400 baud
- Up to 4 NTP servers, app checksum and SHA-256 verification on update, mDNS

`sdkconfig` is generated from it and is not committed; delete `sdkconfig` to
regenerate it after changing `sdkconfig.defaults`.

## Versions and releases

Releases use [semantic versioning](https://semver.org) and are git tags
named `vMAJOR.MINOR.PATCH` (for example `v0.1.0`). There is no version number
in the source: `tools/idf` stamps each build with
`git describe --tags --always --dirty --match 'v[0-9]*'`, so the version always
names the exact commit it was built from:

| Version | Built from |
|---|---|
| `v0.1.0` | the commit tagged `v0.1.0`, with no local changes |
| `v0.1.0-3-g1a2b3c4` | commit `1a2b3c4`, three commits after `v0.1.0` |
| `v0.1.0-3-g1a2b3c4-dirty` | the same commit plus uncommitted changes to tracked files |
| `1a2b3c4` | commit `1a2b3c4`, before any release tag |

The same string appears in three places, so they can be compared directly:

- the web page header and its **Firmware** section;
- the serial log at boot (`I (...) main: FOXY Clock v0.1.0 (built ...)`);
- `python3 -I tools/check_image.py some.bin`, for any image file, whatever
  it is named.

To find the commit: `git show 1a2b3c4` (the part after `-g`), or
`git show v0.1.0`. A `-dirty` build cannot be reproduced exactly.

The version is computed on every `tools/idf` run and CMake reconfigures when
it changes. Builds run with `idf.py` directly (without the wrapper) fall back
to the SDK's own `git describe`, which runs only when CMake reconfigures and
can be out of date.

To make a release:

1. Commit everything (the manual up to date, host tests passing); `git status`
   shows nothing to commit.
2. Pick the number: PATCH for fixes, MINOR for new features or behaviour
   changes, MAJOR for changes that break existing users (for example settings
   that cannot be migrated, or an update that needs a serial flash). While
   the major number is 0, MINOR may also carry breaking changes.
3. Tag and build: `git tag -a v0.2.0 -m "FOXY Clock 0.2.0"`, then `tools/idf build`.
4. Check that `python3 -I tools/check_image.py build/xyclock.bin` prints
   exactly `version v0.2.0`, with no `-g` or `-dirty`.
5. Push the tag (`git push origin v0.2.0`). To publish the image, copy it with
   the version in its name, e.g. `xyclock-v0.2.0.bin`: the file name does not
   matter to the clock, which reads the version and project name from inside
   the image.

## Flashing and the serial monitor

Wiring and entering flash mode are described in
[Installation](INSTALLATION.md#2-wiring). Plug the adapter in **before**
running any `tools/idf` command: the wrapper only passes devices that exist
when it starts.

Flash from the container (in flash mode):

```sh
tools/idf -p /dev/ttyUSB0 flash
```

If flashing fails with serial noise or corruption errors, add `-b 115200`. If
the container cannot open the serial port, use esptool on the host as in
[Installation](INSTALLATION.md#5-first-flash).

Serial monitor:

1. Remove the GPIO0-to-GND jumper.
2. Start the monitor (logging to a file as well):

   ```sh
   tools/idf -p /dev/ttyUSB0 monitor | tee clock.log
   ```

3. Reset the clock: touch CHIP_EN to GND briefly (or power-cycle it, if it is
   powered from its own 5 V input).

Exit with `Ctrl+]`. The monitor's own reset shortcut (`Ctrl+T` `Ctrl+R`) does
not work here because J3 has no reset wiring. Some garbage characters at the
very start are normal: the boot ROM prints at 74880 baud before the firmware
switches to 115200.

## Tests

Hardware-independent logic in `core/` is unit-tested on the host with the
system compiler:

```sh
make -C test                                                # build and run all tests
make -C test clean all SAN="-fsanitize=address,undefined"   # with sanitizers, if libasan is installed
```

Tests are `test/test_*.c`, using the small harness in `test/minitest.h`. Put
any testable logic in `core/`, with a test written first. Nothing in this
repository runs on the hardware automatically: behaviour on the device has to
be checked by hand, with the serial log.

## Code layout

- `core/` (headers in `core/include/xy_*.h`): plain C with no SDK headers,
  unit-tested on the host: settings (defaults, validation, JSON, stored-blob
  upgrades, network-only validation), alarms (catch-up after forward clock
  steps, monotonic timeouts, combined GPIO14 holds), clock sounds and quiet
  hours, display rendering and font, buttons and brightness, time trust, NTP
  server order, firmware-image check, connection test sequencing
  (`wifitest.c`) and the web password policy (`webpass.c`).
- `main/`: drivers and SDK glue.
  - `clock_task.c`: the 20 ms loop (buttons, display, alarms, sounds, LEDs).
  - `net.c`: WiFi, scan and connection test. `ntp.c`: SNTP.
  - `web.c`: HTTP API. `ota.c`: firmware upload.
  - `store.c`: settings in NVS (compare-and-save by revision).
    `early_nvs.c`: boot-time storage recovery (see [Build notes](#build-notes)).
  - `tm1650.c`, `ds1307.c`, `i2c_bus.c`, `beeper.c`, `io.c`: hardware drivers;
    pins in `board.h`.
  - `web/index.html`: the settings page, embedded in the firmware.
- `tools/`: `idf` (container wrapper), `container/Containerfile`,
  `check_image.py`.

## Web API

Every request, including the page, needs HTTP Basic auth once a password is
set (any user name). POSTs also need the header `X-XYClock: 1` (a cross-site
guard; the page sends it). Until a password exists, administrative POSTs
return 403, except `POST /api/wifi-test` in setup mode, and a settings save
must set one.

| Method and path | Purpose |
|---|---|
| `GET /api/settings` | current settings, with their revision `rev` |
| `POST /api/settings` | save settings (JSON; must carry the `rev` from the last GET) |
| `GET /api/status` | live status (time, sync, network, alarms) |
| `GET /api/scan` | nearby WiFi networks |
| `POST /api/ntp-sync` | sync now |
| `POST /api/gpio14` | set GPIO14, e.g. `{"state":true}` |
| `POST /api/wifi-test` | try network settings (settings-style JSON) without saving |
| `GET /api/wifi-test` | progress and result of the connection test |
| `POST /api/restart` | restart |
| `POST /api/factory-reset` | erase all settings and restart |
| `POST /api/ota` | firmware upload (raw `.bin` body) |

## Adding a setting

`xy_settings_t` is stored in NVS as a raw blob, so its layout is part of the
saved format:

1. Append the new fields at the **end** of `xy_settings_t`.
2. Bump `XY_SETTINGS_VERSION`.
3. Add the previous version's stored size (`XY_SETTINGS_Vn_SIZE`), measured
   with the **target** compiler, not the host one.
4. Teach `xy_settings_from_blob()` that size, so saved settings are upgraded
   rather than lost, and add a migration test.
5. Update the [user manual](USER_MANUAL.md) in the same change.

## Build notes

Things in `main/CMakeLists.txt` and the SDK that are easy to break:

- `-u _printf_float -u _scanf_float` is required, as newlib-nano otherwise
  cannot print JSON numbers (it shows up as "out of memory").
- `-Wl,--wrap=esp_mac_init`: the SDK starts NVS in `esp_mac_init()` before
  `app_main` and asserts on failure, which would boot-loop on a damaged
  settings partition. `early_nvs.c` erases and remounts it first.
- mDNS selects IPv6 in lwIP, so `ip_addr_t` is not IPv4-only; use `IP_ADDR4()`.
- This SDK's `esp_http_server` has no `httpd_resp_sendstr`; use `send_str()` in
  `web.c`. Raise `max_uri_handlers` when adding routes.
- The SDK's secure boot / signed apps are ESP32-only, so uploads are
  protected by the web password, not by signatures.

## Troubleshooting

**Container command fails with an image error.**
Rebuild it: `tools/idf --image`.

**Image build fails while fetching SDK submodules.**
The Containerfile fetches the SDK's submodules non-recursively on purpose: a
nested one (libcoap's tinydtls) points at a server that no longer exists. Do
not add `--recursive`.

**"Permission denied" on /dev/ttyUSB0 inside the container.**
Your user needs access to the device on the host first (see
[Installation](INSTALLATION.md#9-troubleshooting)). With `newgrp dialout`, or
`sg dialout -c 'tools/idf -p /dev/ttyUSB0 flash'`, the wrapper inherits the
group.

**The serial adapter is not found inside the container.**
It was plugged in after `tools/idf` started. Run the command again.
