#!/usr/bin/env python3
"""Re-run the DSI enable handshake with CLKLN_HS_FORCE_REQUEST set.

mainline and downstream both program DSI_LANE_CTRL *before* setting
DSI_CTRL.ENABLE, so the controller may only sample the clock-lane HS
request at enable time.  Escalate through plain re-enable, controller
soft reset and PHY software reset, reporting lane state after each step.
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
PHY_RESET = 0x12C

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
    hs = "HS!" if any(k & 0x1F != 0x1F for k in seen) else "all LP-11"
    print("  %-22s CTRL=%#08x LANE_CTRL=%#010x ST=%#x FIFO=%#010x  %s  %s"
          % (tag, r(CTRL), r(LANE_CTRL), r(STATUS), r(FIFO), hs,
             {hex(k): v for k, v in sorted(seen.items())}))


saved = r(CTRL)
report("baseline")

print("A: disable -> set bit28 -> enable")
w(CTRL, saved & ~1)
time.sleep(0.05)
w(LANE_CTRL, CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)
time.sleep(0.2)
report("after re-enable")

print("B: + controller soft reset")
w(CTRL, saved & ~1)
w(SOFT_RESET, 1)
time.sleep(0.01)
w(SOFT_RESET, 0)
w(LANE_CTRL, CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)
time.sleep(0.2)
report("after soft reset")

print("C: + PHY software reset")
w(CTRL, saved & ~1)
w(PHY_RESET, 1)
time.sleep(0.01)
w(PHY_RESET, 0)
time.sleep(0.01)
w(LANE_CTRL, CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)
time.sleep(0.3)
report("after phy reset")
