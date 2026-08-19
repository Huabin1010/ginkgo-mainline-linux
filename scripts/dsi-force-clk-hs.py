#!/usr/bin/env python3
"""Live test: force the DSI clock lane into continuous HS mode.

Sets DSI_LANE_CTRL BIT(28) (CLKLN_HS_FORCE_REQUEST), which is what
downstream's dsi_ctrl_set_continuous_clk(true) does for PHY v2.0, and
reports whether the lanes leave LP-11 and the HS FIFOs start draining.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
LANE_CTRL = 0x0AC
LANE_STATUS = 0x0A8
FIFO_STATUS = 0x00C
STATUS = 0x008

fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED,
              mmap.PROT_READ | mmap.PROT_WRITE, offset=DSI)
os.close(fd)


def r(off):
    return struct.unpack_from("<I", m, off)[0]


def w(off, val):
    struct.pack_into("<I", m, off, val)


def sample(tag):
    seen = {}
    for _ in range(3000):
        seen[r(LANE_STATUS)] = seen.get(r(LANE_STATUS), 0) + 1
    print("%-8s LANE_CTRL=%#010x STATUS=%#010x FIFO=%#010x"
          % (tag, r(LANE_CTRL), r(STATUS), r(FIFO_STATUS)))
    print("         LANE_STATUS %s"
          % {hex(k): v for k, v in sorted(seen.items())})


sample("before")

w(LANE_CTRL, r(LANE_CTRL) | (1 << 28))
time.sleep(0.2)

sample("after")
