#!/usr/bin/env python3
"""Validate PHY CMN_CTRL_0=0x7f: real HS vs idle-off false positive."""
from __future__ import annotations

import mmap
import os
import struct
import subprocess
import time

DSI, OFF, PHY, INTF = 0x5E94000, 4, 0x5E94400, 0x5E6B800
R_CTRL, R_STATUS0, R_FIFO = 0x000, 0x004, 0x008
R_LANE_STATUS, R_LANE_CTRL = 0x0A4, 0x0A8
R_RESET, R_CLK_CTRL, R_VID_CFG0 = 0x114, 0x118, 0x00C
R_TPG = 0x158
PHY_CTRL0 = 0x01C
INTF_EN, INTF_FRAME = 0x000, 0x0AC


def open_mem():
    return os.open("/dev/mem", os.O_RDWR | os.O_SYNC)


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


def enc_vsync():
    try:
        raw = open("/sys/kernel/debug/dri/0/encoder-0/status").read()
    except OSError:
        return None
    for tok in raw.replace("\n", " ").split():
        if tok.startswith("vsync:") or tok == "vsync:":
            pass
    import re
    m = re.search(r"vsync:\s*(\d+)", raw)
    return int(m.group(1)) if m else None


def dcs_power():
    try:
        out = subprocess.check_output(["dmesg"], text=True, errors="replace")
    except Exception:
        return "dmesg-fail"
    lines = [ln for ln in out.splitlines() if "power mode" in ln.lower()]
    return lines[-1] if lines else "no-power-mode-log"


def hist(fd, n=100, interval=0.005):
    h = {}
    st = {}
    fifos = {}
    for _ in range(n):
        v = dsi(fd, R_LANE_STATUS)
        s = dsi(fd, R_STATUS0)
        f = dsi(fd, R_FIFO)
        h[v] = h.get(v, 0) + 1
        st[s] = st.get(s, 0) + 1
        fifos[f] = fifos.get(f, 0) + 1
        time.sleep(interval)
    return h, st, fifos


def show(tag, h, st, fifos):
    print(f"\n==== {tag} ====")
    print("LANE", {hex(k): c for k, c in sorted(h.items())})
    print("STATUS0", {hex(k): c for k, c in sorted(st.items())})
    print("FIFO", {hex(k): c for k, c in sorted(fifos.items())})
    data_hs = sum(c for v, c in h.items() if (v & 0xF) == 0)
    data_stop = sum(c for v, c in h.items() if (v & 0xF) == 0xF)
    print(f"data_hs={data_hs} data_all_stop={data_stop} n={sum(h.values())}")


def main():
    fd = open_mem()
    try:
        print("CTRL0", hex(rd(fd, PHY + PHY_CTRL0)), "CTRL", hex(dsi(fd, R_CTRL)))
        print("blank", open("/sys/class/graphics/fb0/blank").read().strip())
        print("dpms", open("/sys/class/drm/card0-DSI-1/dpms").read().strip())

        # 1) poke 0x7f on already-running (wedged) engine, no reset
        wr(fd, PHY + PHY_CTRL0, 0x7F)
        time.sleep(0.05)
        h, st, fifos = hist(fd, 80)
        show("POKE_7F_NO_RESET", h, st, fifos)
        print("CTRL0 now", hex(rd(fd, PHY + PHY_CTRL0)), "INTF_EN", rd(fd, INTF + INTF_EN))

        fa = rd(fd, INTF + INTF_FRAME)
        ea = enc_vsync()
        time.sleep(1.0)
        fb = rd(fd, INTF + INTF_FRAME)
        eb = enc_vsync()
        print(f"INTF 1s {fb-fa} encoder {None if ea is None or eb is None else eb-ea}")

        # 2) back to 0xff
        wr(fd, PHY + PHY_CTRL0, 0xFF)
        time.sleep(0.05)
        h, st, fifos = hist(fd, 40)
        show("BACK_TO_FF", h, st, fifos)

        # 3) 0x7f again + TPG
        wr(fd, PHY + PHY_CTRL0, 0x7F)
        wdsi(fd, R_TPG, 0x35)
        time.sleep(0.05)
        h, st, fifos = hist(fd, 40)
        show("7F_PLUS_TPG", h, st, fifos)
        wdsi(fd, R_TPG, 0x4)

        # 4) 0x7f + try DCS brightness poke via sysfs if present
        for p in (
            "/sys/class/backlight/nt36672a-backlight/brightness",
            "/sys/class/backlight/dsi_backlight/brightness",
        ):
            if os.path.exists(p):
                cur = open(p).read().strip()
                print("bl", p, cur)
                try:
                    open(p, "w").write(cur)
                    print("bl rewrite ok")
                except OSError as e:
                    print("bl rewrite fail", e)

        print("last power mode log:", dcs_power())
        print("final CTRL0", hex(rd(fd, PHY + PHY_CTRL0)))
        print("final LANE", hex(dsi(fd, R_LANE_STATUS)), "FIFO", hex(dsi(fd, R_FIFO)), "STATUS0", hex(dsi(fd, R_STATUS0)))
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
