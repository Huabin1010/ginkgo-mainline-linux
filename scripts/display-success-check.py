#!/usr/bin/env python3
"""
Honest Level B display readiness check for ginkgo (SM6125).
Exit 0 only when hardware pixel path is healthy — not just timing engine ticking.

Key insight (black screen with backlight):
  INTF_FRAME_COUNT may run at 60/s while encoder vsync stays ~0 and FIFO shows
  0x5555xxxx. fb0 white fill does NOT prove panel visibility.
"""
from __future__ import annotations

import mmap
import os
import re
import struct
import subprocess
import sys
import time
from dataclasses import dataclass

# SM6125 DSI 6G: driver uses ctrl_base + DSI_6G_REG_SHIFT (4).
# Physical address = 0x5e94000 + 4 + xml_offset.  Reading xml_offset
# alone samples the wrong register (LANE_STATUS was read as LANE_CTRL).
DSI = 0x5E94000
DSI6G = 4
PHY = 0x5E94400
DPU_INTF_FRAME_COUNT = 0x5E6B8AC

FIFO_BAD = {0x55551019, 0x55551018, 0xdddd1018, 0xdddd1011}
LANE_STATUS_BAD = {0x1F0F, 0x1F1F}  # data/all lanes STOP — black screen signatures


@dataclass
class Check:
    name: str
    passed: bool
    detail: str


def rd_mem(addr: int) -> int:
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=addr & ~0xFFF)
        try:
            return struct.unpack_from("<I", m, addr & 0xFFF)[0]
        finally:
            m.close()
    finally:
        os.close(fd)


def read_text(path: str, default: str = "") -> str:
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def dmesg_has_panel_init() -> bool:
    try:
        out = subprocess.check_output(["dmesg"], text=True, errors="replace")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return "panel init complete" in out and "power mode readback: 0x9c" in out


def dmesg_dsi_errors() -> list[str]:
    try:
        lines = subprocess.check_output(["dmesg"], text=True, errors="replace").splitlines()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    return [ln for ln in lines if "dsi_err" in ln.lower() or "DSI FIFO status" in ln]


def parse_encoder_vsync() -> int | None:
    raw = read_text("/sys/kernel/debug/dri/0/encoder-0/status")
    m = re.search(r"vsync:\s*(\d+)", raw.replace("\n", " "))
    return int(m.group(1)) if m else None


def parse_encoder_underrun() -> int | None:
    raw = read_text("/sys/kernel/debug/dri/0/encoder-0/status")
    m = re.search(r"underrun:\s*(\d+)", raw.replace("\n", " "))
    return int(m.group(1)) if m else None


def parse_frame_done_timeout() -> int | None:
    raw = read_text("/sys/kernel/debug/dri/0/encoder-0/status")
    m = re.search(r"frame_done_cnt:(\d+)", raw.replace("\n", " "))
    return int(m.group(1)) if m else None


def sample_lane_status(n: int = 200, interval: float = 0.005) -> dict[int, int]:
    hist: dict[int, int] = {}
    for _ in range(n):
        v = rd_mem(DSI + DSI6G + 0x0A4)
        hist[v] = hist.get(v, 0) + 1
        time.sleep(interval)
    return hist


def fifo_is_error(fifo: int) -> bool:
    if fifo in FIFO_BAD:
        return True
    hi = (fifo >> 16) & 0xFFFF
    if hi in (0x5555, 0xDDDD, 0xdddd):
        return True
    if fifo & 0x9:  # bit0 overflow, bit3 underflow (dsi_host FIFO_VIDEO_MDP_ERR)
        return True
    return False


def pclk_rate() -> int | None:
    try:
        out = subprocess.check_output(
            ["grep", "disp_cc_mdss_pclk0_clk", "/sys/kernel/debug/clk/clk_summary"],
            text=True,
        )
    except subprocess.CalledProcessError:
        return None
    m = re.search(r"(\d{8,})\s", out)
    return int(m.group(1)) if m else None


def run_checks() -> list[Check]:
    checks: list[Check] = []

    # --- Level A: bring-up prerequisites ---
    checks.append(Check(
        "panel_init",
        dmesg_has_panel_init(),
        "dmesg: panel init complete + power mode 0x9c",
    ))

    blank = read_text("/sys/class/graphics/fb0/blank", "?")
    checks.append(Check("fb0_blank", blank == "0", f"fb0/blank={blank} (want 0)"))

    dpms = read_text("/sys/class/drm/card0-DSI-1/dpms", "?")
    checks.append(Check("dpms_on", dpms == "On", f"dpms={dpms} (want On)"))

    lane_hist = sample_lane_status(200)
    # Real LANE_STATUS (xml 0xa4 + DSI6G +4): STOP=bits0-3, ULPS_ACTIVE_NOT=bits8-12.
    # Healthy HS is 0x1f00 (STOP=0, not in ULPS), not 0x0000 (that is ULPS).
    # 0x1f0f during BLLP is expected; fail only if data never leaves STOP.
    n = sum(lane_hist.values()) or 1
    data_hs = sum(c for v, c in lane_hist.items() if (v & 0xF) == 0)
    all_stop = sum(c for v, c in lane_hist.items() if v in LANE_STATUS_BAD)
    lane_ok = data_hs * 2 >= n and all_stop < n
    checks.append(Check(
        "lane_status_no_stop",
        lane_ok,
        f"200 samples hist={ {hex(k): v for k, v in sorted(lane_hist.items())} } "
        f"data_hs={data_hs}/{n} (need >=50% data not STOP)",
    ))

    dsi_ctrl = rd_mem(DSI + DSI6G + 0x000)
    checks.append(Check(
        "dsi_ctrl_video",
        dsi_ctrl == 0x1F3,
        f"DSI_CTRL=0x{dsi_ctrl:x} (want 0x1f3)",
    ))

    ldo = rd_mem(PHY + 0x04C)
    checks.append(Check("phy_ldo", ldo == 0x1C, f"PHY_LDO=0x{ldo:x} (want 0x1c)"))

    lane_ctrl = rd_mem(DSI + DSI6G + 0x0A8)
    bit24 = bool(lane_ctrl & (1 << 24))
    bit28 = bool(lane_ctrl & (1 << 28))
    low16 = lane_ctrl & 0xFFFF
    checks.append(Check(
        "lane_ctrl_hs_sel",
        bit24 and not bit28 and low16 == 0,
        f"LANE_CTRL=0x{lane_ctrl:x} bit24={bit24} bit28={bit28} low16=0x{low16:x} (want bit24=1 bit28=0)",
    ))

    dsi_errs = dmesg_dsi_errors()
    checks.append(Check(
        "no_dsi_fifo_errors",
        len(dsi_errs) == 0,
        f"dsi_err/FIFO dmesg lines: {len(dsi_errs)}"
        + (f" e.g. {dsi_errs[-1][:70]}" if dsi_errs else ""),
    ))

    pclk = pclk_rate()
    pclk_ok = pclk is not None and 175_000_000 <= pclk <= 190_000_000
    checks.append(Check("pclk_rate", pclk_ok, f"pclk0={pclk} Hz (want ~183012000)"))

    # --- Level B: end-to-end pixel path (not just INTF timing counter) ---
    intf_a = rd_mem(DPU_INTF_FRAME_COUNT)
    enc_a = parse_encoder_vsync()
    time.sleep(1.0)
    intf_b = rd_mem(DPU_INTF_FRAME_COUNT)
    enc_b = parse_encoder_vsync()

    intf_delta = intf_b - intf_a
    enc_delta = (enc_b - enc_a) if (enc_a is not None and enc_b is not None) else None

    intf_ok = 45 <= intf_delta <= 75
    checks.append(Check(
        "intf_frame_rate_60hz",
        intf_ok,
        f"INTF_FRAME_COUNT delta 1s={intf_delta} (want 45-75)",
    ))

    enc_ok = enc_delta is not None and 45 <= enc_delta <= 75
    checks.append(Check(
        "encoder_vsync_rate_60hz",
        enc_ok,
        f"encoder vsync delta 1s={enc_delta} (want 45-75; black screen often 0-10)",
    ))

    if enc_delta is not None and intf_ok:
        corr_ok = abs(intf_delta - enc_delta) <= 15
        checks.append(Check(
            "intf_encoder_vsync_correlated",
            corr_ok,
            f"INTF delta={intf_delta} vs encoder delta={enc_delta} (want within 15)",
        ))
    else:
        checks.append(Check(
            "intf_encoder_vsync_correlated",
            False,
            "cannot correlate (encoder vsync missing)",
        ))

    fifo = rd_mem(DSI + DSI6G + 0x008)
    checks.append(Check(
        "fifo_status_clean",
        not fifo_is_error(fifo),
        f"FIFO_STATUS=0x{fifo:x} (reject 0x5555/0xdddd patterns and overflow bits)",
    ))

    status0 = rd_mem(DSI + DSI6G + 0x004)
    engine_busy = bool(status0 & (1 << 3))
    checks.append(Check(
        "video_engine_busy",
        engine_busy,
        f"STATUS0=0x{status0:x} VIDEO_MODE_ENGINE_BUSY(bit3)={engine_busy}",
    ))

    u0 = parse_encoder_underrun()
    time.sleep(2.0)
    u1 = parse_encoder_underrun()
    if u0 is not None and u1 is not None:
        checks.append(Check(
            "underrun_stable",
            (u1 - u0) <= 2,
            f"underrun {u0} -> {u1} over 2s",
        ))
    else:
        checks.append(Check("underrun_stable", False, "encoder underrun missing"))

    fdt = parse_frame_done_timeout()
    checks.append(Check(
        "frame_done_no_timeout",
        fdt == 0,
        f"frame_done_timeout_cnt={fdt} (want 0)",
    ))

    # Informational only — does NOT prove panel shows pixels
    try:
        with open("/dev/fb0", "rb", buffering=0) as f:
            pix = f.read(4).hex()
        checks.append(Check(
            "sw_fb_pixel_sample",
            True,
            f"first fb0 pixel={pix} (SW only, not panel visibility)",
        ))
    except OSError as e:
        checks.append(Check("sw_fb_pixel_sample", True, f"skipped: {e}"))

    return checks


def main() -> int:
    print("=== ginkgo display Level B check (strict pixel path) ===\n")
    try:
        checks = run_checks()
    except OSError as e:
        print(f"FATAL: {e}", file=sys.stderr)
        return 2

    # sw_fb_pixel_sample is informational — exclude from pass gate
    gate = [c for c in checks if c.name != "sw_fb_pixel_sample"]
    passed = sum(1 for c in gate if c.passed)
    total = len(gate)

    for c in checks:
        mark = "PASS" if c.passed else "FAIL"
        if c.name == "sw_fb_pixel_sample":
            mark = "INFO"
        print(f"[{mark}] {c.name}: {c.detail}")

    print(f"\nSCORE: {passed}/{total}")
    if passed == total:
        print("VERDICT: LEVEL_B_PASS")
        return 0
    print("VERDICT: LEVEL_B_FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
