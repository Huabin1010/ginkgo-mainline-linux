#!/usr/bin/env python3
"""Sweep DSI LANE_CTRL and measure how often the data lanes leave LP-11.

Hardware offsets are the dsi.xml offsets plus the 4-byte io_offset that
dsi_host.c adds to ctrl_base for every DSI 6G revision.

The interesting bit is HS_REQ_SEL_PHY (24): mainline only clears it when the
PHY implements .set_continuous_clock, which the 14nm PHY does not, so it
keeps whatever the bootloader left behind.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
IO = 4

CTRL = 0x000 + IO
STATUS0 = 0x004 + IO
FIFO_STATUS = 0x008 + IO
LANE_STATUS = 0x0A4 + IO
LANE_CTRL = 0x0A8 + IO

HS_REQ_SEL_PHY = 1 << 24
CLKLN_HS_FORCE_REQUEST = 1 << 28

CANDIDATES = [
    ("bit28+bit24 (current)", CLKLN_HS_FORCE_REQUEST | HS_REQ_SEL_PHY),
    ("bit28 only", CLKLN_HS_FORCE_REQUEST),
    ("bit24 only", HS_REQ_SEL_PHY),
    ("zero", 0),
]

fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
d = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED,
              mmap.PROT_READ | mmap.PROT_WRITE, offset=DSI)
os.close(fd)


def r(off):
    return struct.unpack_from("<I", d, off)[0]


def w(off, val):
    struct.pack_into("<I", d, off, val)


original = r(LANE_CTRL)
print("  LANE_CTRL on entry: %#010x" % original)
print("  CTRL=%#x STATUS0=%#x\n" % (r(CTRL), r(STATUS0)))

SAMPLES = 20000
for label, val in CANDIDATES:
    w(LANE_CTRL, val)
    time.sleep(0.05)
    active = 0
    clk_hs = 0
    seen = {}
    for _ in range(SAMPLES):
        ls = r(LANE_STATUS)
        seen[ls] = seen.get(ls, 0) + 1
        if (ls & 0x0F) != 0x0F:
            active += 1
        if not (ls & 0x10):
            clk_hs += 1
    print("  %-22s LANE_CTRL=%#010x" % (label, val))
    print("      data lanes out of LP-11: %6.2f%%   clk lane in HS: %6.2f%%"
          % (100.0 * active / SAMPLES, 100.0 * clk_hs / SAMPLES))
    print("      LANE_STATUS histogram: %s"
          % {hex(k): v for k, v in sorted(seen.items(), key=lambda x: -x[1])[:4]})
    print("      FIFO_STATUS=%#010x  STATUS0=%#x\n" % (r(FIFO_STATUS), r(STATUS0)))

w(LANE_CTRL, original)
print("  restored LANE_CTRL to %#010x" % original)
