#!/usr/bin/env python3
"""Dump the DPU fetch path: SSPP_VIG0, CTL_0/CTL_1 and LM_0.

Bases are the sm6125 catalog offsets added to the mdp base 0x05e01000:
  sspp_0 (VIG0)  0x4000
  ctl_0 / ctl_1  0x1000 / 0x1200
  lm_0 / lm_1    0x44000 / 0x45000
The register offsets come from dpu_hw_sspp.c, dpu_hw_ctl.c and dpu_hw_lm.c.

The question this answers: is SSPP_SRC0_ADDR actually pointing at the
framebuffer, and is the CTL still driving the mixer into INTF_1?
"""

import mmap
import os
import struct
import time

SSPP_REGS = [
    ("SRC_SIZE", 0x00), ("SRC_XY", 0x08), ("OUT_SIZE", 0x0C), ("OUT_XY", 0x10),
    ("SRC0_ADDR", 0x14), ("SRC_YSTRIDE0", 0x24), ("SRC_FORMAT", 0x30),
    ("SRC_UNPACK_PATTERN", 0x34), ("SRC_OP_MODE", 0x38),
    ("SRC_CONSTANT_COLOR", 0x3C), ("FETCH_CONFIG", 0x48),
    ("SRC_ADDR_SW_STATUS", 0x70),
]

CTL_REGS = [
    ("LAYER_0", 0x000), ("LAYER_1", 0x004), ("TOP", 0x014), ("FLUSH", 0x018),
    ("START", 0x01C), ("MERGE_3D_ACTIVE", 0x0E4), ("INTF_ACTIVE", 0x0F4),
    ("FETCH_PIPE_ACTIVE", 0x0FC), ("INTF_FLUSH", 0x110),
    ("PIPE_ACTIVE", 0x12C), ("LAYER_ACTIVE", 0x130), ("INTF_MASTER", 0x134),
]


def page(base):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        return mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    finally:
        os.close(fd)


sspp = page(0x05E05000)
ctl = page(0x05E02000)
lm = page(0x05E45000)


def rd(m, off):
    return struct.unpack_from("<I", m, off)[0]


print("== SSPP_VIG0 (sspp_0, 0x5e05000) ==")
for name, off in SSPP_REGS:
    print("  %-20s @%#05x = %#010x" % (name, off, rd(sspp, off)))

for idx, base in (("CTL_0", 0x000), ("CTL_1", 0x200)):
    print("\n== %s ==" % idx)
    for name, off in CTL_REGS:
        print("  %-20s @%#05x = %#010x" % (name, off, rd(ctl, base + off)))

print("\n== LM_0 (0x5e45000) raw 0x00..0x40 ==")
for off in range(0x00, 0x44, 4):
    print("  +%#04x = %#010x" % (off, rd(lm, off)))

print("\n== SSPP_SRC0_ADDR stability ==")
for _ in range(5):
    print("  SRC0_ADDR=%#010x  SW_STATUS=%#010x"
          % (rd(sspp, 0x14), rd(sspp, 0x70)))
    time.sleep(0.1)
