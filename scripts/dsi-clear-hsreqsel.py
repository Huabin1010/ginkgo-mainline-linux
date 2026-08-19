#!/usr/bin/env python3
"""Clear DSI_LANE_CTRL HS_REQ_SEL_PHY while keeping CLKLN_HS_FORCE_REQUEST.

The clock lane already stays in HS thanks to bit 28, but the data lanes
never leave LP-11 while bit 24 is set.  Downstream's hs_req_sel(true)
clears that bit; reproduce it here and paint colour bars so the result is
visible on the panel.
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

HS_REQ_SEL_PHY = 1 << 24
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
    for _ in range(2500):
        seen[r(LANE_STATUS)] = seen.get(r(LANE_STATUS), 0) + 1
    data_hs = any((k & 0x0F) != 0x0F for k in seen)
    clk_hs = any((k & 0x10) == 0 for k in seen)
    print("  %-14s LANE_CTRL=%#010x ST=%#x FIFO=%#010x data_hs=%s clk_hs=%s"
          % (tag, r(LANE_CTRL), r(STATUS), r(FIFO), data_hs, clk_hs))
    print("                 %s" % {hex(k): v for k, v in sorted(seen.items())})


def paint():
    """Colour bars honouring the framebuffer stride (1088 px, 1080 visible)."""
    visible, stride_px, height = 1080, 1088, 2340
    colours = [0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00,
               0x00FF00FF, 0x0000FFFF, 0x00FFFFFF, 0x00404040]
    band = height // len(colours)
    pad = b"\x00\x00\x00\x00" * (stride_px - visible)
    with open("/dev/fb0", "wb") as fb:
        for idx, colour in enumerate(colours):
            rows = band if idx < len(colours) - 1 else height - band * (len(colours) - 1)
            row = struct.pack("<I", colour) * visible + pad
            fb.write(row * rows)


saved = r(CTRL)
report("before")

w(CTRL, saved & ~1)
w(SOFT_RESET, 1)
time.sleep(0.01)
w(SOFT_RESET, 0)
w(LANE_CTRL, CLKLN_HS_FORCE_REQUEST)  # bit 28 set, bit 24 cleared
w(CTRL, saved)
time.sleep(0.3)
report("after")

try:
    paint()
    print("  painted colour bars")
except OSError as exc:
    print("  paint failed: %s" % exc)

try:
    with open("/sys/class/backlight/backlight/bl_power", "w") as f:
        f.write("0")
    with open("/sys/class/backlight/backlight/brightness", "w") as f:
        f.write("1")
except OSError as exc:
    print("  backlight: %s" % exc)

time.sleep(0.5)
report("final")
