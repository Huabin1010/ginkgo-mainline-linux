#!/usr/bin/env python3
"""Read ginkgo DSI/PHY health registers via /dev/mem. Run on device over SSH."""
import mmap
import os
import struct
import sys
import time

DSI = 0x5E94000
PHY = 0x5E94400

REGS = [
    (DSI + 0x004, "DSI_CTRL"),
    (DSI + 0x008, "STATUS0"),
    (DSI + 0x00C, "FIFO_STATUS"),
    (DSI + 0x0A4, "LANE_STATUS"),
    (DSI + 0x0A8, "LANE_CTRL"),
    (PHY + 0x04C, "PHY_LDO_CNTRL"),
]


def rd(addr: int) -> int:
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=addr & ~0xFFF)
        try:
            return struct.unpack_from("<I", m, addr & 0xFFF)[0]
        finally:
            m.close()
    finally:
        os.close(fd)


def decode_lane_status(v: int) -> str:
    if v == 0:
        return "OK (all lanes active, no STOPSTATE)"
    stops = []
    if v & 0xF:
        stops.append(f"data STOP bits=0x{v & 0xF:x}")
    if v & 0x10:
        stops.append("clk STOP")
    return "; ".join(stops) if stops else f"raw=0x{v:x}"


def decode_lane_ctrl(v: int) -> str:
    flags = []
    if v & (1 << 24):
        flags.append("HS_REQ_SEL_PHY(bit24)=1")
    if v & (1 << 28):
        flags.append("CLKLN_HS_FORCE(bit28)=1")
    return ", ".join(flags) if flags else "bit24/28 clear (good for ginkgo)"


def main() -> int:
    print("=== ginkgo DSI health check ===")
    try:
        for addr, name in REGS:
            v = rd(addr)
            print(f"0x{addr:08x} {name:16s} = 0x{v:08x}", end="")
            if name == "LANE_STATUS":
                print(f"  # {decode_lane_status(v)}")
            elif name == "LANE_CTRL":
                print(f"  # {decode_lane_ctrl(v)}")
            elif name == "PHY_LDO_CNTRL":
                ok = "OK" if v == 0x1C else ("BAD (expect 0x1c)" if v == 0x3C else "check")
                print(f"  # {ok}")
            else:
                print()
    except OSError as e:
        print(f"error: cannot read /dev/mem: {e}", file=sys.stderr)
        return 1

    print("\n--- LANE_STATUS histogram (200 samples) ---")
    hist: dict[int, int] = {}
    for _ in range(200):
        v = rd(DSI + 0x0A4)
        hist[v] = hist.get(v, 0) + 1
        time.sleep(0.002)
    for k in sorted(hist):
        print(f"  0x{k:04x}: {hist[k]}")

  # sysfs hints
    print("\n--- sysfs ---")
    for path, label in [
        ("/sys/class/graphics/fb0/blank", "fb0 blank (0=on)"),
        ("/sys/class/drm/card0-DSI-1/dpms", "connector dpms"),
    ]:
        try:
            with open(path) as f:
                print(f"  {label}: {f.read().strip()}")
        except OSError:
            print(f"  {label}: (missing)")

    lane = rd(DSI + 0x0A4)
    ldo = rd(PHY + 0x04C)
    ctrl = rd(DSI + 0x0A8)
    if lane != 0:
        print("\n[!] LANE_STATUS not zero -> DSI data lanes may be stuck (check bit28, LDO)")
    if ldo == 0x3C:
        print("[!] PHY LDO 0x3c -> pll_db_commit may overwrite standalone 0x1c")
    if ctrl & (1 << 28):
        print("[!] LANE_CTRL bit28 set -> need MIPI_DSI_CLOCK_NON_CONTINUOUS on panel")

    return 0


if __name__ == "__main__":
    sys.exit(main())
