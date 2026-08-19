#!/usr/bin/env python3
"""Sweep DSI_LANE_CTRL bit 24/28 combinations and record lane behaviour.

For each combination the controller is disabled, soft reset, LANE_CTRL is
programmed and the controller re-enabled - the same order the driver uses.
Lane state is then sampled immediately and again after a delay so a
transient burst can be told apart from steady transmission.
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
LANE_SWAP_CTRL = 0x0B0
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


def sample(n=2500):
    seen = {}
    for _ in range(n):
        v = r(LANE_STATUS)
        seen[v] = seen.get(v, 0) + 1
    return seen


def describe(seen):
    data_hs = sum(v for k, v in seen.items() if (k & 0x0F) != 0x0F)
    clk_hs = sum(v for k, v in seen.items() if (k & 0x10) == 0)
    total = sum(seen.values())
    return ("data_hs=%3d%% clk_hs=%3d%% %s"
            % (100 * data_hs // total, 100 * clk_hs // total,
               {hex(k): v for k, v in sorted(seen.items())}))


print("LANE_SWAP_CTRL = %#010x (0 = identity lane map)" % r(LANE_SWAP_CTRL))
saved = r(CTRL)

for label, val in (("0x00000000", 0),
                   ("bit24 only", HS_REQ_SEL_PHY),
                   ("bit28 only", CLKLN_HS_FORCE_REQUEST),
                   ("bit24|bit28", HS_REQ_SEL_PHY | CLKLN_HS_FORCE_REQUEST)):
    w(CTRL, saved & ~1)
    w(SOFT_RESET, 1)
    time.sleep(0.01)
    w(SOFT_RESET, 0)
    w(LANE_CTRL, val)
    w(CTRL, saved)

    time.sleep(0.05)
    early = sample()
    time.sleep(0.5)
    late = sample()

    print("\n%-12s LANE_CTRL=%#010x ST=%#x FIFO=%#010x"
          % (label, r(LANE_CTRL), r(STATUS), r(FIFO)))
    print("  early %s" % describe(early))
    print("  late  %s" % describe(late))

# leave the controller in the state the driver would program
w(CTRL, saved & ~1)
w(SOFT_RESET, 1)
time.sleep(0.01)
w(SOFT_RESET, 0)
w(LANE_CTRL, HS_REQ_SEL_PHY | CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)
