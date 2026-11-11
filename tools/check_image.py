#!/usr/bin/env python3
"""Check a FOXY Clock firmware image and print its version.

Usage: python3 -I tools/check_image.py build/xyclock.bin

Prints the version and build time stored in the image (the same text the web
page shows under Firmware), then checks what the clock's web updater checks:
an app description with project name "xyclock" and a valid appended SHA-256.
(esptool's own parser assumes an ESP32 extended header and fails on these.)
"""
import hashlib
import struct
import sys

PROJECT = "xyclock"
APP_DESC_MAGIC = 0xABCD5432


def cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def app_desc(d):
    """Finds esp_app_desc_t, which on ESP8266 is not at a fixed offset.
    Same rules as core/imgscan.c: 4-aligned magic, zero reserv1, terminated
    printable project name."""
    magic = struct.pack("<I", APP_DESC_MAGIC)
    i = d.find(magic)
    while i >= 0:
        if i % 4 == 0 and i + 144 <= len(d):
            x = d[i:i + 144]
            proj = x[48:80]
            if (x[8:16] == bytes(8) and b"\0" in x[16:48] and b"\0" in proj
                    and proj[0] and all(0x20 <= c <= 0x7E for c in proj.split(b"\0", 1)[0])):
                return {"version": cstr(x[16:48]), "project": cstr(proj),
                        "time": cstr(x[80:96]), "date": cstr(x[96:112]),
                        "idf": cstr(x[112:144])}
        i = d.find(magic, i + 1)
    return None


d = open(sys.argv[1], "rb").read()
if len(d) < 8:
    sys.exit("SHA-256 digest MISSING or WRONG (file too short to be an image)")
if d[0] != 0xE9:
    sys.exit("not an ESP image (bad magic)")

ok = True
desc = app_desc(d)
if desc:
    print(f"version {desc['version']}, built {desc['date']} {desc['time']}, SDK {desc['idf']}")
    if desc["project"] != PROJECT:
        print(f"project name is {desc['project']!r}, not {PROJECT!r}: the clock will reject it")
        ok = False
else:
    print("no app description found: the clock will reject it")
    ok = False

off = 8
for _ in range(d[1]):
    if off + 8 > len(d):
        sys.exit("SHA-256 digest MISSING or WRONG (file truncated)")
    _, ln = struct.unpack("<II", d[off:off + 8])
    off += 8 + ln
end = off + (16 - (off + 1) % 16) % 16 + 1          # padding + checksum byte
sha_ok = len(d) == end + 32 and hashlib.sha256(d[:end]).digest() == d[end:]
print("SHA-256 digest OK" if sha_ok else "SHA-256 digest MISSING or WRONG")
sys.exit(0 if ok and sha_ok else 1)
