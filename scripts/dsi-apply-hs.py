#!/usr/bin/env python3
"""Bring the DSI link into continuous-clock HS mode and paint colour bars.

Applies what the compiled fix will do on a fresh boot: DSI_LANE_CTRL
CLKLN_HS_FORCE_REQUEST plus a controller soft reset so the clock-lane HS
request is latched.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
CTRL = 0x004
STATUS = 0x008
FIFO = 0x00C
LANE_STATUS = 0x0A8
LANE_CTRL = 0x0AC
SOFT_RESET = 0x118

CLKLN_HS_FORCE_REQUEST = 1 << 28

fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED,
              mmap.PROT_READ | mmap.PROT_WRITE, offset=DSI)
os.close(fd)


def r(off):
    return struct.unpack_from("<I", m, off)[0]


def w(off, val):
    struct.pack_into("<I", m, off, val)


def report(tag):
    seen = {}
    for _ in range(2000):
        seen[r(LANE_STATUS)] = seen.get(r(LANE_STATUS), 0) + 1
    print("  %-16s LANE_CTRL=%#010x ST=%#x FIFO=%#010x  %s"
          % (tag, r(LANE_CTRL), r(STATUS), r(FIFO),
             {hex(k): v for k, v in sorted(seen.items())}))


saved = r(CTRL)
report("before")

w(CTRL, saved & ~1)
w(SOFT_RESET, 1)
time.sleep(0.01)
w(SOFT_RESET, 0)
w(LANE_CTRL, CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)
time.sleep(0.3)
report("after")


def paint():
    """Eight horizontal colour bars in XRGB8888."""
    w_px, h_px = 1080, 2340
    colours = [0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00,
               0x00FF00FF, 0x0000FFFF, 0x00FFFFFF, 0x00808080]
    band = h_px // len(colours)
    with open("/dev/fb0", "wb") as fb:
        for idx, colour in enumerate(colours):
            rows = band if idx < len(colours) - 1 else h_px - band * (len(colours) - 1)
            fb.write(struct.pack("<I", colour) * w_px * rows)


try:
    paint()
    print("  painted colour bars to /dev/fb0")
except OSError as exc:
    print("  framebuffer paint failed: %s" % exc)

for path in ("/sys/class/backlight/backlight",):
    try:
        with open(path + "/bl_power", "w") as f:
            f.write("0")
        with open(path + "/max_brightness") as f:
            mx = f.read().strip()
        with open(path + "/brightness", "w") as f:
            f.write(mx)
        print("  backlight %s -> %s" % (path, mx))
    except OSError as exc:
        print("  backlight %s: %s" % (path, exc))

time.sleep(0.5)
report("final")
