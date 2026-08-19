#!/usr/bin/env python3
"""Dump the ginkgo 14nm DSI PHY and compare against what dsi_phy_14nm.c writes.

Bases come straight from the mdss_dsi0_phy node in sm6125.dtsi:
  dsi_phy       0x05e94400
  dsi_phy_lane  0x05e94500  (5 blocks of 0x80: LN0..LN3 then CKLN)
  dsi_pll       0x05e94800
  dsi_phy_clamp 0x05e01400
Unlike the DSI controller, the PHY has no io_offset shift.
"""

import mmap
import os
import struct

CMN = 0x05E94400
LANE = 0x05E94500
PLL = 0x05E94800
CLAMP = 0x05E01400

LANE_STRIDE = 0x80
CKLN = 4

CMN_REGS = [
    ("REVISION_ID0", 0x00, None),
    ("CLK_CFG0", 0x10, None),
    ("CLK_CFG1", 0x14, None),
    ("GLBL_TEST_CTRL", 0x18, 0x01),
    ("CTRL_0", 0x1C, 0xFF),
    ("CTRL_1", 0x20, 0x00),
    ("PLL_CNTRL", 0x48, None),
    ("LDO_CNTRL", 0x4C, 0x1C),
]

LANE_REGS = [
    ("CFG0", 0x00, None),
    ("CFG1", 0x04, None),
    ("CFG2", 0x08, 0x10),
    ("CFG3", 0x0C, None),
    ("TEST_DATAPATH", 0x10, 0x00),
    ("TEST_STR", 0x14, 0x88),
    ("TIMING_CTRL_4", 0x18, None),
    ("TIMING_CTRL_5", 0x1C, None),
    ("TIMING_CTRL_6", 0x20, None),
    ("TIMING_CTRL_7", 0x24, None),
    ("TIMING_CTRL_8", 0x28, None),
    ("TIMING_CTRL_9", 0x2C, None),
    ("TIMING_CTRL_10", 0x30, None),
    ("TIMING_CTRL_11", 0x34, None),
    ("STRENGTH_CTRL_0", 0x38, 0xFF),
    ("STRENGTH_CTRL_1", 0x3C, None),
    ("VREG_CNTRL", 0x64, 0x1D),
]


def _map(base, size=0x1000):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        return mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    finally:
        os.close(fd)


page = _map(0x05E94000)
mdp = _map(0x05E01000)


def rd(off):
    return struct.unpack_from("<I", page, off)[0]


def flag(val, want):
    if want is None:
        return ""
    return "  OK" if val == want else "  <-- expected %#04x" % want


print("== PHY CMN (0x5e94400) ==")
for name, off, want in CMN_REGS:
    v = rd(0x400 + off)
    print("  %-16s %#010x%s" % (name, v, flag(v, want)))

print("\n== PHY lanes (LN0-3 data, LN4 clk) ==")
for name, off, want in LANE_REGS:
    vals = []
    for i in range(5):
        vals.append(rd(0x500 + i * LANE_STRIDE + off))
    line = " ".join("%02x" % v for v in vals)
    note = ""
    if want is not None:
        bad = [i for i in range(4) if vals[i] != want]
        note = "  OK" if not bad else "  <-- data lanes %s expected %#04x" % (bad, want)
    print("  %-16s %s%s" % (name, line, note))

print("\n== PHY clamp (0x5e01400) ==")
for off in (0x00, 0x04, 0x14, 0x50, 0x54, 0x58):
    v = struct.unpack_from("<I", mdp, 0x400 + off)[0]
    tag = "  <-- ULPS_CLAMP_ENABLE (want 0)" if off == 0x54 else ""
    print("  +%#04x            %#010x%s" % (off, v, tag))

print("\n== PLL (0x5e94800) ==")
for name, off in (("CLKBUFLR_EN", 0x01C), ("SYSCLK_EN_RESET", 0x028),
                  ("RESETSM_CNTRL", 0x02C), ("RESETSM_CNTRL5", 0x03C)):
    print("  %-16s %#010x" % (name, rd(0x800 + off)))
