#!/usr/bin/env python3
"""Correlate the DSI stall with CLK_STATUS and the DPU INTF line counter.

Restarts the DSI link, then records every change in
(LANE_STATUS, FIFO_STATUS, CLK_STATUS) together with the INTF_1 frame and
line counters.  This shows whether DSICLK really drops out at the moment the
data lanes park, and whether the DPU keeps scanning while the DSI goes quiet.

DSI offsets are dsi.xml offsets + the 4-byte io_offset from dsi_host.c.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
IO = 4
CTRL = 0x000 + IO
FIFO_STATUS = 0x008 + IO
LANE_STATUS = 0x0A4 + IO
LANE_CTRL = 0x0A8 + IO
RESET = 0x114 + IO
CLK_STATUS = 0x11C + IO

INTF_PAGE = 0x05E6B000
INTF_FRAME = 0x800 + 0x0AC
INTF_LINE = 0x800 + 0x0B0

CLK_BITS = [
    (4, "AON_DSICLK"), (5, "DYN_DSICLK"), (6, "AON_BYTECLK"),
    (7, "DYN_BYTECLK"), (8, "AON_ESCCLK"), (9, "AON_PCLK"),
    (10, "DYN_PCLK"), (14, "VID_PCLK"), (16, "PLL_UNLOCKED"),
]


def _map(base, write=False):
    prot = mmap.PROT_READ | (mmap.PROT_WRITE if write else 0)
    fd = os.open("/dev/mem", (os.O_RDWR if write else os.O_RDONLY) | os.O_SYNC)
    try:
        return mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, prot, offset=base)
    finally:
        os.close(fd)


d = _map(DSI, write=True)
intf = _map(INTF_PAGE)
up = struct.unpack_from


def decode_clk(v):
    return ",".join(n for b, n in CLK_BITS if v & (1 << b)) or "-"


saved = up("<I", d, CTRL)[0]
print("  before restart: CLK_STATUS=%#010x  [%s]"
      % (up("<I", d, CLK_STATUS)[0], decode_clk(up("<I", d, CLK_STATUS)[0])))

struct.pack_into("<I", d, CTRL, saved & ~1)
struct.pack_into("<I", d, RESET, 1)
time.sleep(0.01)
struct.pack_into("<I", d, RESET, 0)
struct.pack_into("<I", d, CTRL, saved)

t0 = time.monotonic()
clock = time.monotonic
events = []
prev = None
while clock() - t0 < 0.25:
    ls = up("<I", d, LANE_STATUS)[0]
    ff = up("<I", d, FIFO_STATUS)[0]
    ck = up("<I", d, CLK_STATUS)[0]
    key = (ls, ff, ck)
    if key != prev:
        events.append((clock() - t0, ls, ff, ck,
                       up("<I", intf, INTF_FRAME)[0],
                       up("<I", intf, INTF_LINE)[0]))
        prev = key
        if len(events) > 60:
            break

print("\n  %-9s %-8s %-12s %-11s %-7s %s"
      % ("t(ms)", "LANE", "FIFO", "CLK_STATUS", "frame", "line"))
for t, ls, ff, ck, fr, ln in events:
    print("  %9.3f %#08x %#012x %#011x %-7d %d" % (t * 1000, ls, ff, ck, fr, ln))

print("\n  clock decode of distinct CLK_STATUS values seen:")
for v in sorted({e[3] for e in events}):
    print("    %#011x  [%s]" % (v, decode_clk(v)))
