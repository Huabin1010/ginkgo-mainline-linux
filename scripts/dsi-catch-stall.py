#!/usr/bin/env python3
"""Restart the DSI link and catch the moment the data lanes stop.

The link transmits for a short while after every controller reset and then
wedges.  Sample LANE_STATUS in a tight loop, note when the data lanes stop
leaving LP-11, and snapshot the error/status registers plus the DPU frame
counter at that instant.
"""

import mmap
import os
import struct
import time

DSI = 0x05E94000
INTF_PAGE = 0x05E6B000
INTF_OFF = 0x800

CTRL = 0x004
STATUS = 0x008
FIFO = 0x00C
LANE_STATUS = 0x0A8
LANE_CTRL = 0x0AC
TIMEOUT_STATUS = 0x0C0
SOFT_RESET = 0x118
INT_CTRL = 0x110

HS_REQ_SEL_PHY = 1 << 24
CLKLN_HS_FORCE_REQUEST = 1 << 28


def _map(base, write=False):
    prot = mmap.PROT_READ | (mmap.PROT_WRITE if write else 0)
    fd = os.open("/dev/mem", (os.O_RDWR if write else os.O_RDONLY) | os.O_SYNC)
    try:
        return mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, prot, offset=base)
    finally:
        os.close(fd)


d = _map(DSI, write=True)
intf = _map(INTF_PAGE)


def r(off):
    return struct.unpack_from("<I", d, off)[0]


def w(off, val):
    struct.pack_into("<I", d, off, val)


def frames():
    return struct.unpack_from("<I", intf, INTF_OFF + 0x0AC)[0]


def snap(tag, t):
    print("  [%7.3fs] %-12s LANE_STATUS=%#06x LANE_CTRL=%#010x ST=%#x "
          "FIFO=%#010x TIMEOUT=%#x INT_CTRL=%#010x frames=%d"
          % (t, tag, r(LANE_STATUS), r(LANE_CTRL), r(STATUS), r(FIFO),
             r(TIMEOUT_STATUS), r(INT_CTRL), frames()))


saved = r(CTRL)
snap("pre-restart", 0.0)

w(CTRL, saved & ~1)
w(SOFT_RESET, 1)
time.sleep(0.01)
w(SOFT_RESET, 0)
w(LANE_CTRL, HS_REQ_SEL_PHY | CLKLN_HS_FORCE_REQUEST)
w(CTRL, saved)

t0 = time.monotonic()
f0 = frames()
snap("restarted", 0.0)

# A data lane out of LP-11 means HS traffic; count how long that keeps up.
last_hs = None
idle_run = 0
stalled_at = None
deadline = 8.0

while True:
    now = time.monotonic() - t0
    if now > deadline:
        break
    ls = r(LANE_STATUS)
    if (ls & 0x0F) != 0x0F:
        last_hs = now
        idle_run = 0
    else:
        idle_run += 1
        if idle_run > 20000 and stalled_at is None:
            stalled_at = now
            snap("STALLED", now)
            print("       last HS activity at %.3fs, %d frames since restart"
                  % (last_hs if last_hs else -1, frames() - f0))

print("\n  last HS activity: %s" % ("%.3fs" % last_hs if last_hs else "never"))
print("  frames advanced : %d" % (frames() - f0))
snap("final", time.monotonic() - t0)
