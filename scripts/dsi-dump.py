#!/usr/bin/env python3
"""Dump the ginkgo DSI controller, 14nm PHY/PLL and DPU INTF_1 state.

One 4 KiB window at 0x05e94000 covers the whole DSI island:
  0x000..0x3ff  DSI controller (downstream dsi_ctrl_reg.h offsets)
  0x400..0x4ff  PHY CMN
  0x500..0x7ff  PHY lanes DLN0..DLN3 + CKLN (0x80 stride)
  0x800..0x987  PHY PLL
INTF_1 lives at DPU mdp base 0x05e01000 + 0x6a800.
"""

import mmap
import os
import struct
import sys
import time

DSI = 0x05E94000
INTF_PAGE = 0x05E6B000
INTF_OFF = 0x800

PHY = 0x400
LN = 0x500
LN_STRIDE = 0x80
PLL = 0x800


def _map(base, size=0x1000, write=False):
    prot = mmap.PROT_READ | (mmap.PROT_WRITE if write else 0)
    flags = os.O_RDWR if write else os.O_RDONLY
    fd = os.open("/dev/mem", flags | os.O_SYNC)
    try:
        return mmap.mmap(fd, size, mmap.MAP_SHARED, prot, offset=base)
    finally:
        os.close(fd)


class Blk:
    def __init__(self, base, write=False):
        self.m = _map(base, write=write)

    def r(self, off):
        return struct.unpack_from("<I", self.m, off)[0]

    def w(self, off, val):
        struct.pack_into("<I", self.m, off, val)


def bits(val, names):
    return " ".join(n for i, n in names if val & (1 << i)) or "-"


def main():
    dsi = Blk(DSI)
    intf = Blk(INTF_PAGE)

    print("== DSI controller ==")
    ctrl = dsi.r(0x004)
    print("  CTRL              %#010x  %s" % (ctrl, bits(ctrl, [
        (0, "DSI_EN"), (1, "VID_EN"), (2, "CMD_EN"), (4, "LN0"), (5, "LN1"),
        (6, "LN2"), (7, "LN3"), (8, "CLKLN")])))
    st = dsi.r(0x008)
    print("  STATUS            %#010x  %s" % (st, bits(st, [
        (0, "CMD_ENG_BUSY"), (1, "CMD_DMA_BUSY"), (2, "CMD_MDP_BUSY"),
        (3, "VID_ENG_BUSY"), (4, "DSI_BUSY")])))
    fifo = dsi.r(0x00C)
    print("  FIFO_STATUS       %#010x  %s" % (fifo, bits(fifo, [
        (0, "VID_MDP_OVFL"), (3, "VID_MDP_UNFL"), (16, "DLN0_HS_EMPTY"),
        (17, "DLN0_HS_FULL"), (19, "DLN0_HS_UNFL"), (20, "DLN1_HS_EMPTY"),
        (24, "DLN2_HS_EMPTY"), (28, "DLN3_HS_EMPTY")])))
    print("  VIDEO_MODE_CTRL   %#010x" % dsi.r(0x010))
    print("  VID_DATA_CTRL     %#010x" % dsi.r(0x020))
    for name, off in (("ACTIVE_H", 0x024), ("ACTIVE_V", 0x028),
                      ("TOTAL", 0x02C), ("HSYNC", 0x030),
                      ("VSYNC", 0x034), ("VSYNC_VPOS", 0x038)):
        v = dsi.r(off)
        print("  VID_%-13s %#010x  start=%-5d end=%d"
              % (name, v, v & 0xFFFF, (v >> 16) & 0xFFFF))
    print("  TRIG_CTRL         %#010x" % dsi.r(0x084))
    ls = dsi.r(0x0A8)
    print("  LANE_STATUS       %#010x  %s" % (ls, bits(ls, [
        (0, "DLN0_STOP"), (1, "DLN1_STOP"), (2, "DLN2_STOP"),
        (3, "DLN3_STOP"), (4, "CLKLN_STOP")])))
    print("  LANE_CTRL         %#010x" % dsi.r(0x0AC))
    print("  CLK_CTRL          %#010x" % dsi.r(0x11C))
    print("  CLK_STATUS        %#010x" % dsi.r(0x120))
    print("  TPG_CTRL          %#010x" % dsi.r(0x15C))
    print("  TIMING_DB_MODE    %#010x" % dsi.r(0x1E8))
    print("  VERSION           %#010x" % dsi.r(0x1F4))

    print("== PHY CMN ==")
    for name, off in (("REVISION_ID0", 0x00), ("CLK_CFG0", 0x10),
                      ("CLK_CFG1", 0x14), ("GLBL_TEST_CTRL", 0x18),
                      ("CTRL_0", 0x1C), ("CTRL_1", 0x20),
                      ("PLL_CNTRL", 0x48), ("LDO_CNTRL", 0x4C)):
        print("  %-15s %#010x" % (name, dsi.r(PHY + off)))

    print("== PHY lanes (0-3 data, 4 clk) ==")
    for i in range(5):
        b = LN + i * LN_STRIDE
        cfg = [dsi.r(b + 0x00 + j * 4) for j in range(4)]
        tim = [dsi.r(b + 0x18 + j * 4) for j in range(8)]
        print("  LN%d cfg=%s vreg=%02x str=%02x,%02x tim=%s"
              % (i, " ".join("%02x" % c for c in cfg), dsi.r(b + 0x64),
                 dsi.r(b + 0x38), dsi.r(b + 0x3C),
                 " ".join("%02x" % t for t in tim)))

    print("== PHY PLL ==")
    print("  CLKBUFLR_EN       %#010x" % dsi.r(PLL + 0x1C))
    print("  RESETSM_CNTRL5    %#010x" % dsi.r(PLL + 0x3C))
    print("  PLL_BANDGAP       %#010x" % dsi.r(PLL + 0x108))

    print("== DPU INTF_1 (0x5e6b800) ==")
    for name, off in (("TIMING_ENGINE_EN", 0x000), ("CONFIG", 0x004),
                      ("HSYNC_CTL", 0x008), ("VSYNC_PERIOD_F0", 0x00C),
                      ("VSYNC_PULSE_W_F0", 0x014), ("DISPLAY_V_START", 0x01C),
                      ("DISPLAY_V_END", 0x024), ("DISPLAY_HCTL", 0x03C),
                      ("ACTIVE_HCTL", 0x040), ("POLARITY_CTL", 0x050),
                      ("TEST_CTL", 0x054), ("CONFIG2", 0x060),
                      ("DISPLAY_DATA_HCTL", 0x064), ("PANEL_FORMAT", 0x090),
                      ("FRAME_LINE_CNT_EN", 0x0A8), ("PROG_FETCH_START", 0x170),
                      ("MUX", 0x25C), ("STATUS", 0x26C)):
        print("  %-18s %#010x" % (name, intf.r(INTF_OFF + off)))

    print("== motion ==")
    f0, l0 = intf.r(INTF_OFF + 0x0AC), intf.r(INTF_OFF + 0x0B0)
    seen = {}
    for _ in range(4000):
        seen[dsi.r(0x0A8)] = seen.get(dsi.r(0x0A8), 0) + 1
    time.sleep(0.3)
    f1, l1 = intf.r(INTF_OFF + 0x0AC), intf.r(INTF_OFF + 0x0B0)
    print("  INTF frame %d -> %d   line %d -> %d" % (f0, f1, l0, l1))
    print("  LANE_STATUS samples %s"
          % {hex(k): v for k, v in sorted(seen.items())})


if __name__ == "__main__":
    sys.exit(main())
