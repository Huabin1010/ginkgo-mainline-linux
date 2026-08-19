#!/usr/bin/env python3
"""Live DSI6G experiments: find a sequence that takes data lanes out of STOP."""
from __future__ import annotations

import mmap
import os
import struct
import time

DSI = 0x5E94000
OFF = 4  # DSI_6G_REG_SHIFT
PHY = 0x5E94400
INTF = 0x5E6B800  # INTF_1

# xml offsets
R_CTRL = 0x000
R_STATUS0 = 0x004
R_FIFO = 0x008
R_VID_CFG0 = 0x00C
R_VID_CFG1 = 0x01C
R_ACTIVE_H = 0x020
R_ACTIVE_V = 0x024
R_TOTAL = 0x028
R_HSYNC = 0x02C
R_VSYNC_H = 0x030
R_VSYNC_V = 0x034
R_TRIG = 0x080
R_LANE_STATUS = 0x0A4
R_LANE_CTRL = 0x0A8
R_RESET = 0x114
R_CLK_CTRL = 0x118
R_HS_TIMER = 0x0B8
R_TIMEOUT = 0x0BC
R_TPG = 0x158
R_CLKOUT = 0x0C0
R_EOT = 0x0C8
R_LANE_SWAP = 0x0AC

INTF_EN = 0x000
INTF_FRAME = 0x0AC
INTF_LINE = 0x0B0
INTF_MUX = 0x25C

PHY_CTRL0 = 0x01C
PHY_LDO = 0x04C
PHY_CLK_CFG1 = 0x014


def open_mem():
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    return fd


def rd(fd, addr):
    m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=addr & ~0xFFF)
    try:
        return struct.unpack_from("<I", m, addr & 0xFFF)[0]
    finally:
        m.close()


def wr(fd, addr, val):
    m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=addr & ~0xFFF)
    try:
        struct.pack_into("<I", m, addr & 0xFFF, val & 0xFFFFFFFF)
    finally:
        m.close()


def dsi(fd, off):
    return rd(fd, DSI + OFF + off)


def wdsi(fd, off, val):
    wr(fd, DSI + OFF + off, val)


def snap_lane(fd, n=20):
    hist = {}
    for _ in range(n):
        v = dsi(fd, R_LANE_STATUS)
        hist[v] = hist.get(v, 0) + 1
        time.sleep(0.002)
    return {hex(k): c for k, c in sorted(hist.items())}


def dump(fd, tag):
    print(f"\n==== {tag} ====")
    print(f"CTRL={dsi(fd,R_CTRL):#x} STATUS0={dsi(fd,R_STATUS0):#x} FIFO={dsi(fd,R_FIFO):#x}")
    print(f"LANE_STATUS={dsi(fd,R_LANE_STATUS):#x} LANE_CTRL={dsi(fd,R_LANE_CTRL):#x}")
    print(f"VID_CFG0={dsi(fd,R_VID_CFG0):#x} TOTAL={dsi(fd,R_TOTAL):#x}")
    print(f"ACTIVE_H={dsi(fd,R_ACTIVE_H):#x} ACTIVE_V={dsi(fd,R_ACTIVE_V):#x}")
    print(f"HSYNC={dsi(fd,R_HSYNC):#x} VSYNC_H={dsi(fd,R_VSYNC_H):#x} VSYNC_V={dsi(fd,R_VSYNC_V):#x}")
    print(f"TRIG={dsi(fd,R_TRIG):#x} CLK_CTRL={dsi(fd,R_CLK_CTRL):#x} HS_TIMER={dsi(fd,R_HS_TIMER):#x}")
    print(f"INTF_EN={rd(fd,INTF+INTF_EN):#x} FRAME={rd(fd,INTF+INTF_FRAME)} LINE={rd(fd,INTF+INTF_LINE)}")
    print(f"INTF_MUX={rd(fd,INTF+INTF_MUX):#x}")
    print(f"PHY_CTRL0={rd(fd,PHY+PHY_CTRL0):#x} LDO={rd(fd,PHY+PHY_LDO):#x} CLK_CFG1={rd(fd,PHY+PHY_CLK_CFG1):#x}")
    print(f"LANE hist20={snap_lane(fd)}")


def save_timing(fd):
    keys = [
        R_VID_CFG0, R_VID_CFG1, R_ACTIVE_H, R_ACTIVE_V, R_TOTAL,
        R_HSYNC, R_VSYNC_H, R_VSYNC_V, R_TRIG, R_CLK_CTRL, R_HS_TIMER,
        R_CLKOUT, R_EOT, R_LANE_SWAP, R_LANE_CTRL,
    ]
    return {k: dsi(fd, k) for k in keys}


def restore_timing(fd, t, trig=None):
    for k, v in t.items():
        if k == R_TRIG and trig is not None:
            wdsi(fd, k, trig)
        else:
            wdsi(fd, k, v)


def soft_reset(fd):
    ctrl = dsi(fd, R_CTRL)
    wdsi(fd, R_CTRL, ctrl & ~0x7)  # clear EN/VID/CMD
    wdsi(fd, R_CLK_CTRL, 0x23F)
    wdsi(fd, R_RESET, 1)
    time.sleep(0.02)
    wdsi(fd, R_RESET, 0)
    time.sleep(0.002)


def set_intf(fd, on):
    wr(fd, INTF + INTF_EN, 1 if on else 0)


def main():
    fd = open_mem()
    try:
        dump(fd, "BOOT")
        timing = save_timing(fd)
        print("saved timing:", {hex(k): hex(v) for k, v in timing.items()})

        # A: clear TE only
        wdsi(fd, R_TRIG, timing[R_TRIG] & ~0x80000000)
        time.sleep(0.05)
        dump(fd, "A_clear_TE_only")

        # B: reset + restore + no TE + ENABLE then VID then INTF
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        wdsi(fd, R_CTRL, 0x1F1)  # ENABLE + lanes + clk, no VID
        time.sleep(0.02)
        print("B after 0x1f1", hex(dsi(fd, R_LANE_STATUS)), hex(dsi(fd, R_FIFO)))
        set_intf(fd, 1)
        time.sleep(0.05)
        print("B after INTF", hex(dsi(fd, R_LANE_STATUS)), hex(dsi(fd, R_FIFO)), hex(dsi(fd, R_STATUS0)))
        wdsi(fd, R_CTRL, 0x1F3)  # + VID
        time.sleep(0.05)
        dump(fd, "B_enable_then_vid_noTE")

        # C: reset + restore + no TE + INTF first + VID+EN together
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.03)
        wdsi(fd, R_CTRL, 0x1F3)
        time.sleep(0.05)
        dump(fd, "C_intf_then_1f3_noTE")

        # D: CMD+VID 0x1f7
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F7)
        time.sleep(0.05)
        dump(fd, "D_cmd_plus_vid_0x1f7")

        # E: TPG after clean reset, INTF on
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        wdsi(fd, R_TPG, 0x35)  # video TPG incremental
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F3)
        time.sleep(0.05)
        dump(fd, "E_tpg_1f3_noTE")
        wdsi(fd, R_TPG, 0x4)

        # F: PHY CTRL0 0x7f (downstream idle/off uses 0x7f, mainline 0xff)
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wr(fd, PHY + PHY_CTRL0, 0x7F)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F3)
        time.sleep(0.05)
        dump(fd, "F_phy_ctrl0_7f")
        wr(fd, PHY + PHY_CTRL0, 0xFF)

        # G: VID_CFG0 without BLLP stop (0x9130 -> 0x0030 RGB888 + sync_event)
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_VID_CFG0, 0x0130)  # RGB888 + non_burst_sync_event, no BLLP stop
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F3)
        time.sleep(0.05)
        dump(fd, "G_no_bllp_stop")

        # H: burst traffic mode
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing, trig=timing[R_TRIG] & ~0x80000000)
        wdsi(fd, R_VID_CFG0, 0x9230)  # burst + BLLP stop + RGB888
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F3)
        time.sleep(0.05)
        dump(fd, "H_burst")

        # restore original video config so we don't leave a worse poke
        set_intf(fd, 0)
        soft_reset(fd)
        restore_timing(fd, timing)
        wdsi(fd, R_FIFO, dsi(fd, R_FIFO))
        set_intf(fd, 1)
        time.sleep(0.02)
        wdsi(fd, R_CTRL, 0x1F3)
        dump(fd, "RESTORE")
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
