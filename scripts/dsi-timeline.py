#!/usr/bin/env python3
"""Record a fine-grained timeline of the DSI link across the stall.

Restarts the link, then samples LANE_STATUS, FIFO_STATUS, STATUS and the
DPU INTF line counter in a tight loop, printing only transitions.  The
order in which the error bits appear tells us whether the lanes stop first
or the FIFO errors come first.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
INTF_PAGE = 0x05E6B000
INTF_LINE = 0x800 + 0x0B0
INTF_FRAME = 0x800 + 0x0AC

CTRL, STATUS, FIFO = 0x004, 0x008, 0x00C
LANE_STATUS, LANE_CTRL = 0x0A8, 0x0AC
SOFT_RESET = 0x118

LANE_CTRL_VAL = (1 << 24) | (1 << 28)


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

saved = up("<I", d, CTRL)[0]
struct.pack_into("<I", d, CTRL, saved & ~1)
struct.pack_into("<I", d, SOFT_RESET, 1)
time.sleep(0.01)
struct.pack_into("<I", d, SOFT_RESET, 0)
struct.pack_into("<I", d, LANE_CTRL, LANE_CTRL_VAL)
struct.pack_into("<I", d, CTRL, saved)

t0 = time.monotonic()
events = []
prev = None
clock = time.monotonic

while clock() - t0 < 0.6:
    ls = up("<I", d, LANE_STATUS)[0]
    ff = up("<I", d, FIFO)[0]
    st = up("<I", d, STATUS)[0]
    key = (ls, ff, st)
    if key != prev:
        events.append((clock() - t0, ls, ff, st,
                       up("<I", intf, INTF_FRAME)[0]))
        prev = key
        if len(events) > 400:
            break

print("  %-9s %-8s %-12s %-6s %s" % ("t(ms)", "LANE", "FIFO", "ST", "frame"))
for t, ls, ff, st, fr in events:
    print("  %9.3f %#08x %#012x %#06x %d" % (t * 1000, ls, ff, st, fr))

print("\n  total transitions: %d" % len(events))
print("  final: LANE=%#x FIFO=%#x ST=%#x"
      % (up("<I", d, LANE_STATUS)[0], up("<I", d, FIFO)[0],
         up("<I", d, STATUS)[0]))
