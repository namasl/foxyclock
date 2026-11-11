# FOXY Clock user manual

For FOXY Clock, custom firmware for the Sinilink XY-Clock. Installing the
firmware is covered in [Installation](INSTALLATION.md), the board in
[Hardware](HARDWARE.md), and building in [Development](DEVELOPMENT.md).

## Contents

1. [Overview](#1-overview)
2. [First setup](#2-first-setup)
3. [The display and LEDs](#3-the-display-and-leds)
4. [Buttons](#4-buttons)
5. [The settings page](#5-the-settings-page)
6. [Alarms](#6-alarms)
7. [Clock sounds](#7-clock-sounds)
8. [GPIO14 output](#8-gpio14-output)
9. [Firmware updates](#9-firmware-updates)
10. [Recovery: setup mode and factory reset](#10-recovery-setup-mode-and-factory-reset)
11. [Troubleshooting](#11-troubleshooting)
12. [Reference](#12-reference)

---

## 1. Overview

The clock sets itself from internet time servers (NTP) over WiFi and keeps
time with its on-board real-time clock (RTC). Everything is configured from a
web page served by the clock itself. There is no app and no cloud service.

- 12- or 24-hour display, adjustable brightness (fixed or day/night).
- Up to 4 NTP servers, tried in order.
- 6 alarms with snooze, days of the week, and an optional output pin (GPIO14).
- Optional ticking and hourly chimes.
- Firmware updates from the web page.

**RTC battery.** The holder on the board takes a **CR927** (3 V lithium,
non-rechargeable). Without it, the clock forgets the time whenever it loses
power and shows `--:--` until it reaches an NTP server again. Settings are
stored in flash and are kept with or without the battery.

**WiFi.** 2.4 GHz networks only, with WPA2, WPA3 or mixed WPA2/WPA3 security
(see [Troubleshooting](#11-troubleshooting) for networks that do not work).

---

## 2. First setup

1. **Power the clock.** With nothing configured, it starts its own WiFi
   network (*setup mode*): the display shows `AP` and the blue LED blinks.
2. **Join the clock's network** from a phone or laptop:
   - name: `XYClock-XXXX` (`XXXX` = the last four hex digits of the clock's MAC
     address)
   - password: **`xyclock123`**
3. **Open http://192.168.4.1** in a browser. A yellow banner asks for first
   setup.
4. **Choose your time zone** (required; the page will not save without it).
5. **Choose your WiFi network** from **Nearby networks**: this fills in its name
   and security type. Or type the name in. Enter the WiFi password.
6. Optionally press **Test connection** to check the password and network
   before committing (see [Network](#network)).
7. **Choose a web page password** (Security section, 8-32 characters, typed
   twice). It is required: it protects the clock's settings and firmware.
8. Press **Save**. The clock restarts and joins your network. From now on the
   browser asks for the password: any user name works.

The display shows `--:--` until the first NTP answer (usually a few seconds),
then the time. To find the clock's address on your network, press **SET**: the
IP address scrolls across the display. The page is also available at
**http://xyclock.local** (or *device-name*`.local`) on computers that support
mDNS.

Once configured, the clock **always** tries to reach your router, retrying
every few seconds at first and then every 30 seconds, for as long as it takes.
It never turns itself back into an access point. To get back to setup mode,
see [Recovery](#10-recovery-setup-mode-and-factory-reset).

---

## 3. The display and LEDs

### Time

- **12-hour** (default): no leading zero, for example ` 9:05`. There is no AM/PM
  indicator.
- **24-hour**: with leading zero, for example `09:05`.
- `--:--` means the clock does not know the time yet: there is no RTC battery
  (or it has been removed) and no NTP server has answered since power-up.
  Alarms and chimes do not sound until the time is known.

### Colon

Set on the page (**Display** section):

- **Blink once a second**: on for the first half of each second.
- **Steady**: always on.

If no NTP server has answered for **two sync intervals** (two hours with the
default 60-minute interval), the colon changes to warn you, as configured:

| Setting | Normal colon | When NTP is not answering |
|---|---|---|
| Blinking colon goes steady | blinking | steady |
| Steady colon starts blinking | steady | blinking |
| Colon dots alternate | either | upper and lower dot take turns |
| No indication | either | unchanged |

The clock keeps running on its RTC meanwhile, and the warning clears at the
next successful sync.

### Messages

| Display | Meaning |
|---|---|
| `AP` | setup mode (shown for the whole time) |
| scrolling digits | IP address, after pressing SET |
| `rSt?` | UP and DOWN held at power-on: keep holding for a factory reset |
| `rSt` | factory reset in progress |
| `UPd` | a firmware upload is being written |
| `Err` | a factory reset could not erase the settings (six beeps); the clock restarts with them kept |

### LEDs

| LED | On | Blinking |
|---|---|---|
| Blue | connected to WiFi | setup mode |
| Red | time not set, or NTP not answering | — |

In normal use with a working connection, blue is on and red is off.

---

## 4. Buttons

Every button press beeps.

| Situation | UP | DOWN | SET |
|---|---|---|---|
| Normal | brighter | dimmer | scroll the IP address (`no IP` if not connected; not in setup mode, where the address is 192.168.4.1) |
| Display switched off | wakes it (level 1) | wakes it | wakes it (except while an alarm rings: the key goes to the alarm and the display stays off) |
| A sounding alarm is ringing | dismiss | dismiss | snooze |
| A sounding alarm is snoozed | dismiss | dismiss | nothing |

**Brightness.** There are 8 levels. With **fixed** brightness, DOWN goes down
to **off**. With a **day/night schedule**, UP and DOWN change whichever level
is in use at the moment (day or night) and stop at 1. Changes are saved about
3 seconds after the last press and survive power cuts (once first setup has
been saved; before that, nothing is written to flash).

**Held while plugging in the power**, see [Recovery](#10-recovery-setup-mode-and-factory-reset):

| Held at power-on | Result |
|---|---|
| any one button | setup mode, settings kept (two beeps) |
| UP + DOWN for 3 seconds | factory reset (three beeps; then release) |

---

## 5. The settings page

Open the clock's address in a browser. Change what you need, then press
**Save** at the bottom. Fields left empty for passwords keep the current
password.

Saving **restarts the clock** if you changed any network setting (WiFi name,
password or security; DHCP or static addresses; device name; setup access
point password), and always in setup mode. Other settings take effect
immediately.

If the page says *"settings changed on the clock since this page loaded;
reload and try again"*, something changed on the clock after you opened the
page: a button brightness change, a ring-once alarm switching itself off, or
a GPIO14 change (the page's own Set high / Set low buttons, or an alarm
action). The page reloads the current settings; make your changes again and
save. (This stops an old page from silently undoing those changes.)

### Status

Updates every 2 seconds:

- **Time**, for example `2026-11-09 23:30:58 Mon`.
- **Time source**: *OK*; *NTP not answering (using RTC)*; or *not set yet*.
- **Last NTP sync**: how long ago, and which server answered.
- **Network**: signal strength when connected. When not connected, it gives
  the reason for the last disconnect, for example *4-way handshake timeout
  (wrong password?)*.
- **IP address**, **MAC address**, **Uptime**.
- **Warning** (only if it happens): a change made on the clock, such as a
  brightness or GPIO14 change, could not be saved to flash and will be lost
  at the next power cut.

**Sync now** asks the NTP servers immediately and reports which one answered.
It needs a WiFi connection (not available in setup mode).

### Network

- **Nearby networks**: networks the clock can see, strongest first (hidden
  networks are not listed; type their name instead). Choosing
  one fills in the name and security type. **Scan again** refreshes the list
  (about 2-4 seconds). A network shown as *"WPA2/WPA3, probably with fast
  roaming"* is encrypted but advertises extras (such as 802.11r) that the
  clock's WiFi library cannot name; it selects *WPA2/WPA3 mixed*, which
  normally works. A network shown as *"security type N, may not work"* uses
  security the clock does not support (open, WEP, WPA1-only or Enterprise).
- **WiFi network**, **WiFi password** (8-63 characters, or a 64-digit hex key).
- **Security**: WPA2, WPA2/WPA3 mixed, or WPA3. Choose what your router uses.
- **Address**: DHCP (automatic), or a static IP address with netmask, gateway
  and **DNS server**. The DNS server is required unless all your NTP servers
  are given as IP addresses.
- **Device name**: lowercase letters, digits and `-` (not first or last), up to
  32 characters (default `xyclock`). Used for `name.local` and shown to your router.
- **Test connection**: tries the network as entered above (name, password,
  security, DHCP or static address) **without saving anything**, then goes
  back to how it was: setup mode, or your current network. It reports either
  *Success: connected ... and got IP ...*, or the reason it failed, for example
  a wrong password, the network not being found, or joining but getting no IP
  address. It takes up to about 20 seconds. The clock has a single radio, so
  during the test it leaves its current network (or, in setup mode, its own
  access point may pause); the page loses contact briefly and reconnects by
  itself. In setup mode your phone or laptop may drop off `XYClock-XXXX` for a
  moment, and may need rejoining if it does not reconnect automatically.

### Time

- **Time zone**: 26 named zones, including US Eastern, Central, Mountain,
  Arizona, Pacific, Alaska and Hawaii. Daylight saving is handled
  automatically where the zone has it (Arizona and Hawaii do not). **Custom...** accepts any POSIX TZ string, for example
  `EST5EDT,M3.2.0,M11.1.0`.
- **Clock format**: 12-hour or 24-hour.
- **NTP servers 1-4**, in priority order (host names or IP addresses). Defaults:
  `pool.ntp.org`, `time.google.com`, `time.cloudflare.com`, `time.nist.gov`.
  If a server does not answer within 15 seconds, the next one is tried. After
  using a fallback, the clock returns to server 1 at the next sync.
- **Sync interval**: 1-1440 minutes (default 60).

### Display

- **Brightness**: *Fixed* (level Off or 1-8, default 4), or *Day / night
  schedule* (a day level and a night level, 1-8, each with a start time;
  defaults 6 from 07:00 and 1 from 22:00).
- **Colon** and **When NTP is not answering**: see [Colon](#colon). Choices that
  do not fit the colon style are greyed out.

### Sounds

See [Clock sounds](#7-clock-sounds).

### Alarms

See [Alarms](#6-alarms).

### Security

- **Web page password**: **required**, 8-32 characters, typed twice. The
  browser asks for it before showing any page; any user name is accepted.
  Leave both fields empty to keep the current one. It cannot be removed, only
  changed.
- **Setup access point password**: the password for `XYClock-XXXX` in setup
  mode (8-63 characters, default `xyclock123`).

The clock has no HTTPS, so passwords cross your network unencrypted. The web
password keeps casual visitors out, not someone listening on your WiFi.

Without the password nothing can be changed: settings, GPIO14, Sync now,
restart, factory reset and firmware upload all require it. A clock set up with
older firmware, which has no password yet, shows a banner asking for one, and
accepts only a save that sets it until then. (The setup-mode connection test
is the one exception, so you can check WiFi before choosing a password.)

### GPIO14, Firmware, Maintenance

- **GPIO14**: shows the pin's state, with **Set high** / **Set low** buttons.
  See [GPIO14 output](#8-gpio14-output).
- **Firmware**: shows the running version (also shown in the page header);
  upload a new one. See [Firmware updates](#9-firmware-updates).
- **Restart**, and **Factory reset** (needs the web password; erases all
  settings and restarts into setup mode; asks for confirmation). If the
  settings cannot be erased, the page says *factory reset failed* and
  everything, including the password, is kept.

---

## 6. Alarms

There are six alarms. Each has:

| Setting | Notes |
|---|---|
| enabled | |
| time | hour and minute |
| days | an enabled alarm needs at least one day |
| ring once, then disable | for one-off alarms |
| sound | off = the alarm only drives GPIO14 |
| timeout | 0-240 minutes; 0 = rings until a key is pressed (default 1) |
| snooze | 1-120 minutes (default 20) |
| GPIO14 | what the alarm does to the GPIO14 pin (see below) |

The alarm's state (*ringing*, *snoozed*) is shown next to it on the page.

### When an alarm rings

- The display keeps showing the time.
- The beeper plays the alarm's number, repeated: one beep then a pause for
  alarm 1, two beeps then a pause for alarm 2, and so on.
- **UP or DOWN dismisses** it until its next scheduled day.
- **SET snoozes** it: it rings again after the snooze time, as often as you
  snooze (there is no limit). While it is snoozed, SET does nothing and UP or
  DOWN dismiss it.
- The **timeout** ends it the same way as a dismiss. Each ringing period,
  including one after a snooze, gets its own timeout.
- A **ring-once** alarm rings again after every snooze, and switches itself
  off when it finally ends (UP/DOWN or timeout).
- Switching an alarm off or editing it on the page while it is ringing or
  snoozed ends it.
- If two alarms ring together, you hear the lower-numbered one, and one key
  press handles both.
- Alarms ring only when the clock knows the time. An alarm whose time passes
  while the display shows `--:--` is skipped, not rung late.
- Changing the clock (NTP corrections, daylight saving) never makes an alarm
  ring twice on the same day. If the clock jumps *forward* by up to 2 hours
  (an NTP correction, or the start of daylight saving), alarms whose time was
  jumped over ring straight away, so an alarm at 02:30 rings at 03:00 on the
  day the US clocks go forward. A bigger jump does not catch up, and nor does
  a power cut or restart: an alarm due while the clock was off or restarting
  is skipped.
- Ringing timeouts and snoozes count real elapsed time, unaffected by clock
  corrections.
- If several ringing alarms use *High/Low while ringing*, GPIO14 stays in the
  ringing state until the last of them stops.

### GPIO14 actions

| Action | When the alarm starts | When it stops (dismiss, timeout or snooze) |
|---|---|---|
| None | — | — |
| High while ringing | high | low |
| Low while ringing | low | high |
| Set high | high | — |
| Set low | low | — |
| Toggle | toggles | — |

After a snooze, the start action is applied again when the alarm rings again.

### Alarms with sound off

These make no sound and do not change the display, so they **never take over
the buttons**, and cannot be snoozed or dismissed with a key:

- With **High/Low while ringing**, the pin holds its ringing state for the
  timeout, then switches back. The timeout cannot be 0.
- With **Set high / Set low / Toggle**, the alarm acts once at its time and is
  done.
- A GPIO14 action is required, since otherwise the alarm would do nothing.

---

## 7. Clock sounds

All off by default (**Sounds** section):

- **Tick every second**: a short click on each second.
- **Beep on the minute instead of a tick**: needs ticking switched on.
- **Chimes**: on the hour, low beeps striking the hour like a grandfather clock
  (1-12 times, about one per second; 12-hour counting even in 24-hour mode,
  so 15:00 strikes 3).
  On the half hour, one short beep.

- **Quiet hours** (off by default, 22:00 to 07:00 when switched on): no ticks
  or chimes between the start and end times (the window may cross midnight;
  the end time itself is outside it). Alarms and button beeps are not
  affected. While the clock does not know the time, it treats quiet hours as
  being in effect.

Chimes start once the clock knows the time; ticks do not need it. On the hour
and half hour, the chime replaces the tick, and ticks pause until the strikes
have finished. All clock sounds pause while an alarm is ringing.

---

## 8. GPIO14 output

Header **J2**, pin **KEY**: a 3.3 V logic output.

- It can drive a relay **module** or a transistor/opto input. Do not connect a
  load (relay coil, LED strip, motor) directly.
- Set it by hand on the page, or from alarms. The last state is restored at
  power-up (once first setup has been saved). If an alarm was holding it
  (*High/Low while ringing*) when power was lost, it comes back at the level
  it would have returned to when that alarm stopped, since the alarm itself
  does not survive the restart.
- **At power-up it is undriven (floating) for about half a second** (longer
  if a button is held at power-on), until the firmware drives it straight to
  its saved level; it is never pulsed to the opposite level. A **factory
  reset** leaves it undriven for about 5 s (about 9 s if the reset fails),
  through the restart, and it then comes up **low**, because the saved level
  was erased. Fit a pull-up or pull-down resistor on the connected device
  so it sees a defined level during that time.

---

## 9. Firmware updates

1. Get the new firmware file, `build/xyclock.bin` (see
   [Development](DEVELOPMENT.md#building)).
2. On the page, under **Firmware**, choose the file and press **Upload
   firmware**. The display shows `UPd`.
3. If it is accepted, the clock restarts into it. The **Firmware** section then
   shows the new version.

Versions follow [semantic versioning](https://semver.org): `v0.1.0` is a
release. A version like `v0.1.0-3-g1a2b3c4` is a development build three
commits after that release (commit `1a2b3c4`), and `-dirty` at the end means
it was built with uncommitted changes. To see which version a firmware file
contains before uploading it, run
`python3 -I tools/check_image.py file.bin` (see
[Development](DEVELOPMENT.md#versions-and-releases)).

The clock writes the upload into its spare firmware slot and switches to it
only if every check passes: the whole file arrived, it is an ESP8266 image
built as `xyclock` (not another device's firmware), and its checksum and
SHA-256 match. Otherwise it keeps running the current firmware, and the page
says why.

These checks catch damaged or wrong files. **They are not a security check:**
they do not prove who built the firmware. What stops someone else installing
firmware is the web page password, which uploads require. Settings are kept across updates; newer firmware upgrades older
saved settings automatically. Going back to an *older* firmware may not
recognise settings saved by a newer one: the clock then starts in first-setup
mode and needs setting up again.

There is no automatic fallback if a new firmware fails to start. Recover by
flashing over the serial adapter
([Installation](INSTALLATION.md#5-first-flash)).

---

## 10. Recovery: setup mode and factory reset

Hold buttons while **plugging in the power**:

- **Setup mode, settings kept**: hold any one button, release after **two
  beeps**. The display shows `AP`. Join `XYClock-XXXX` and open
  http://192.168.4.1 to change the WiFi settings. The browser still asks for
  the web page password (forgotten it? do a factory reset instead). Saving
  returns to normal operation, as does unplugging the power. Use this after changing your
  router, or if the clock cannot connect.
- **Factory reset**: hold **UP and DOWN** together. The display shows `rSt?`;
  keep holding for 3 seconds until `rSt` and **three beeps**, then **release**
  (if the erase fails, `Err` and six beeps instead, and the settings are kept)
  (still holding when it restarts starts another reset). All settings are
  erased (including the web password) and the clock starts in first-setup
  mode. Releasing before the three seconds gives setup mode instead, with
  nothing erased.

A factory reset is also available on the settings page, under **Maintenance**.

---

## 11. Troubleshooting

**`--:--` stays on the display.** The clock has no time. Check the blue LED:
if it is off, the clock is not connected (look at **Network** in the status
panel for the reason). If it is on, check the NTP servers and press **Sync
now**. Fitting an RTC battery avoids `--:--` after power cuts.

**Red LED on, colon behaving differently.** No NTP server has answered for two
intervals. The time is still kept by the RTC. Check that the router allows NTP
(UDP port 123), and that the servers are reachable.

**The display is partly or completely blank, or the clock has frozen.** Check
the 5 V power connector and cable: an intermittent connection can disturb the
display controller without restarting the rest of the clock. The firmware
reasserts the display control settings and rewrites all four digits at every
minute change, so a display-only failure should recover at the next minute.
If the whole clock remains frozen, disconnect the power, correct the bad
connection and power it up again.

**The clock does not join my network.** Before saving, use **Test
connection** to try the settings without losing your current connection. If
it is already set and not connecting, check the status panel's last
disconnect reason:

- *handshake timeout (wrong password?)*: re-enter the WiFi password.
- *network not found*: the network is 5 GHz-only, out of range, or the name is
  wrong. The clock supports 2.4 GHz only.
- *security type not supported*: the clock supports WPA2-Personal,
  WPA2/WPA3 and WPA3-Personal. It does not support open or WEP networks,
  WPA1-only, WPA2/WPA3-Enterprise, or networks that offer *only* 802.11r
  fast-roaming security. Networks offering 802.11r alongside standard WPA2/WPA3
  work (confirmed on the owner's router); if yours does not, turn on the
  router's 802.11r compatibility or "mixed" option, or use a network without
  802.11r.

To change WiFi settings when the clock cannot connect, use
[setup mode](#10-recovery-setup-mode-and-factory-reset).

**I forgot the web page password.** Do a factory reset with the UP+DOWN
power-on hold (see [Recovery](#10-recovery-setup-mode-and-factory-reset)).
This erases all settings.

**The page says "settings changed on the clock since this page loaded".** See
[The settings page](#5-the-settings-page): reload, and save again.

**`xyclock.local` does not open.** Use the IP address (press SET), or check
that your computer supports mDNS (on Windows, recent versions do; otherwise
install Bonjour).

**A firmware upload was rejected.** The clock is still running its previous
firmware. The page gives the reason: use the `xyclock.bin` from a complete
build, and retry if the upload was interrupted.

---

## 12. Reference

| Item | Value |
|---|---|
| Setup network | `XYClock-XXXX`, WPA2, password `xyclock123`, page at 192.168.4.1 |
| Web page password | required, 8-32 characters; any user name |
| Default device name | `xyclock` (http://xyclock.local) |
| Default NTP servers | pool.ntp.org, time.google.com, time.cloudflare.com, time.nist.gov |
| NTP sync interval | 1-1440 min, default 60; a server gets 15 s to answer |
| Brightness | Off, 1-8 (default 4, fixed) |
| Alarms | 6; timeout 0-240 min (default 1); snooze 1-120 min (default 20) |
| Alarm sound | 2.7 kHz, N x (150 ms beep + 150 ms gap), 1 s pause |
| Quiet hours | ticks and chimes only; default 22:00-07:00, off |
| Connection test | up to ~20 s; nothing saved; returns to the previous network |
| RTC battery | CR927, 3 V lithium |
| GPIO14 | header J2 "KEY", 3.3 V logic output |
| WiFi | 2.4 GHz; WPA2, WPA2/WPA3, WPA3 Personal |
