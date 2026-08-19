#!/usr/bin/env python3
"""Dump DPU INTF_1 (the DSI interface) and watch its frame/line counters.

Offsets are the ones dpu_hw_intf.c programs.  INTF_1 sits at mdp base
0x05e01000 + 0x6a800 per the sm6125 catalog entry.
"""

import mmap
import os
import struct
import time

MDP = 0x05E01000
INTF1 = 0x6A800

REGS = [
    ("TIMING_ENGINE_EN", 0x000),
    ("CONFIG", 0x004),
    ("HSYNC_CTL", 0x008),
    ("VSYNC_PERIOD_F0", 0x00C),
    ("VSYNC_PULSE_WIDTH_F0", 0x014),
    ("DISPLAY_V_START_F0", 0x01C),
    ("DISPLAY_V_END_F0", 0x024),
    ("ACTIVE_V_START_F0", 0x02C),
    ("ACTIVE_V_END_F0", 0x034),
    ("DISPLAY_HCTL", 0x03C),
    ("ACTIVE_HCTL", 0x040),
    ("BORDER_COLOR", 0x044),
    ("UNDERFLOW_COLOR", 0x048),
    ("HSYNC_SKEW", 0x04C),
    ("POLARITY_CTL", 0x050),
    ("TEST_CTL", 0x054),
    ("CONFIG2", 0x060),
    ("DISPLAY_DATA_HCTL", 0x064),
    ("ACTIVE_DATA_HCTL", 0x068),
    ("DSI_CMD_MODE_TRIGGER_EN", 0x084),
    ("PANEL_FORMAT", 0x090),
    ("FRAME_LINE_COUNT_EN", 0x0A8),
    ("TPG_ENABLE", 0x100),
    ("PROG_FETCH_START", 0x170),
    ("MUX", 0x25C),
    ("STATUS", 0x26C),
]

fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=MDP + 0x6A000)
os.close(fd)
B = 0x800


def r(off):
    return struct.unpack_from("<I", m, B + off)[0]


print("== DPU INTF_1 ==")
for name, off in REGS:
    print("  %-24s @%#05x = %#010x" % (name, off, r(off)))

print("\n== counters ==")
prev = None
for _ in range(6):
    fr, ln = r(0x0AC), r(0x0B0)
    delta = "" if prev is None else "  (+%d frames)" % (fr - prev)
    print("  frame=%-8d line=%-6d%s" % (fr, ln, delta))
    prev = fr
    time.sleep(0.1)
