# AGENTS.md

Guide for coding agents working on this repo: FOXY Clock, custom firmware for the
Sinilink XY-Clock (ESP8285, TM1650 display, DS1307 RTC), built with
ESP8266_RTOS_SDK v3.4. Read this first; it is kept short on purpose and points
to the other documents instead of repeating them.

## Documents

- `README.md`: front page, a short introduction linking to the documents below.
- `docs/INSTALLATION.md`: stock backup, first flash over serial with esptool,
  web page updates.
- `docs/USER_MANUAL.md`: what the clock does, as built. **The source of truth
  for behaviour.** Update it in the same change as any behaviour change.
- `docs/HARDWARE.md`: pinout and how each part is connected, datasheets.
- `docs/DEVELOPMENT.md`: build container, building, tests, code layout, web
  API, build notes.

## Commands

All from the repo root. The SDK and toolchain live only in a Podman container.

```sh
make -C test                 # host unit tests for core/ (fast; run after every change)
tools/idf build              # firmware -> build/xyclock.bin (runs idf.py in the container)
python3 -I tools/check_image.py build/xyclock.bin   # prints version, checks SHA-256
tools/idf -p /dev/ttyUSB0 flash monitor             # needs the board in flash mode
```

A change is done when: host tests pass, the firmware builds with no warnings
in `core/` or `main/`, and the manual matches. Nothing can be run on the
hardware from here: say what is untested on the device.

## Layout

- `core/` (+ `core/include/xy_*.h`): hardware-independent logic, plain C, no
  SDK headers, unit-tested on the host (`test/test_*.c`, harness
  `test/minitest.h`). Put any testable logic here, with a test written first.
- `main/`: drivers and SDK glue. `clock_task.c` is the 20 ms loop (buttons,
  display, alarms, sounds, LEDs). `net.c` WiFi/scan/connection test, `ntp.c`,
  `web.c` HTTP API, `ota.c` upload, `store.c` settings in NVS,
  `early_nvs.c` boot storage recovery, `web/index.html` the page (embedded).

## Hardware facts (verified on the owner's board)

- Pins in `main/board.h`. Buttons GPIO10 UP, GPIO9 DOWN, GPIO16 SET (active
  low; GPIO16 has no interrupt, so poll). LEDs GPIO0 blue, GPIO2 red (active
  low). Beeper GPIO5 is **passive**: PWM tones only. GPIO14 is the user output.
- One I2C bus (GPIO12 SCL, GPIO13 SDA) shared by TM1650 and DS1307 (0x68).
  TM1650 has no address: the register is the "address" byte.
- TM1650 dot bits are cross-wired: digit-1 reg -> DP of digit 3, digit-2 reg
  -> DP of digit 2, digit-3 reg -> upper colon dot, digit-4 reg -> lower.
- The RTC battery is often absent: never trust the RTC unless CH is clear and
  the date is valid.

## Traps (each cost real debugging time)

- **The product is named "FOXY Clock", but internal names stay `xyclock`**:
  the CMake project name (checked by the web updater in `ota.c`, so renaming
  it makes installed clocks reject updates), `xyclock.bin`, the NVS namespace,
  the `X-XYClock` header, and the defaults for the setup network and device name.
- **Flash mode must stay DOUT**: GPIO9/10 are the flash's quad lines.
- **`-u _printf_float -u _scanf_float`** in `main/CMakeLists.txt` is required.
  Without it newlib-nano cannot print numbers and every JSON reply fails
  (it showed up as "out of memory").
- **mDNS** (`CONFIG_ENABLE_MDNS`) selects IPv6, so use `IP_ADDR4()` and do not
  assume `ip_addr_t` is IPv4-only.
- **The SDK starts NVS before `app_main`** (`esp_mac_init`, asserted).
  Storage recovery is therefore in `early_nvs.c` via `-Wl,--wrap=esp_mac_init`;
  recovery code in `store_init()` alone never runs.
- **This old `esp_http_server`** has no `httpd_resp_sendstr` or
  `HTTPD_RESP_USE_STRLEN`; use `send_str()` in `web.c`. URI handler slots are
  limited (`max_uri_handlers`): raise it when adding routes.
- **SDK APIs differ from current ESP-IDF.** Check the real header in the
  container before using an API:
  `podman run --rm localhost/xyclock-rtos-sdk:3.4 cat /opt/esp/ESP8266_RTOS_SDK/components/<path>`.
- **lwIP SNTP** keeps polling whichever server answered last; `ntp.c` rotates
  the list to return to the primary. Call lwIP from its own thread
  (`tcpip_callback`).
- **The WiFi scan reports some encrypted networks as open** (WPA2/WPA3 with
  802.11r). Classify by the pairwise cipher too (`h_scan` in `web.c`).
- **Single radio**: the connection test leaves the current network, so the
  page loses contact briefly by design.
- **Secure boot / signed images are ESP32-only** in this SDK. Uploads are
  protected by the required web password, not by signatures.

## Rules for changes

- **Settings format.** `xy_settings_t` is stored as a raw blob. To add a
  setting: append fields at the end only, bump `XY_SETTINGS_VERSION`, add the
  previous version's stored size (`XY_SETTINGS_Vn_SIZE`, **measured with the
  target compiler**, not the host) and a case in `xy_settings_from_blob()`,
  plus a migration test. Getting this wrong wipes users' saved settings.
- **The web password is required but is deliberately not part of
  `xy_settings_validate()`.** Settings stored by older firmware have no
  password and must still load. The rule is in `core/webpass.c` and enforced
  in `web.c` (`web_admin_allowed`, `h_post_settings`) and `ota.c`.
- **Settings saves are compare-and-save** (`store_get_rev` /
  `store_put_if_rev`). Do not read the revision separately from the settings.
- **Shared state** between the clock task, HTTP task, NTP task and WiFi
  events goes through locks (`store.c`, `clock_task.c`). Keep handoffs atomic
  (e.g. alarm cancels plus settings reload in `clock_settings_saved()`).
- **Alarms** use monotonic time for timeouts and snoozes, and local wall time
  only for matching. GPIO14 outputs are composed per tick in `core/alarm.c`.
  Behaviour is specified in the manual, section 6: keep them in step.
- **POSTs** need the `X-XYClock: 1` header (cross-site guard); the page sends
  it via `api()`.

## Working with the owner

- The owner decides behaviour. Ask only when it genuinely cannot be inferred;
  otherwise make a sensible choice and state it.
- Verify review feedback against the code before acting on it, and push back
  with evidence when it is wrong.
- Do not claim hardware behaviour you have not seen. Ask the owner for the
  serial log (`tools/idf -p /dev/ttyUSB0 monitor | tee clock.log`).

## Commits

- Repo-local author is `Nick Masluk <nick@randombytes.net>`: do not change it,
  and do not use the global git identity.
- One logical change per commit (fixes separate from docs); messages explain
  why. End with the co-author trailer the session gives you.
- Versions are git tags `vMAJOR.MINOR.PATCH`; `tools/idf` stamps builds with
  `git describe` (see docs/DEVELOPMENT.md, "Versions and releases"). Tag only
  when the owner asks.
- Never commit `build/`, `sdkconfig` or `clock.log` (see `.gitignore`).
