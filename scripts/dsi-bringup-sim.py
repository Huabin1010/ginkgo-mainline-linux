#!/usr/bin/env python3
"""
Offline ginkgo DSI/DPU bring-up simulator.

Replays logged register snapshots and models mainline vs downstream behavior
without /dev/mem or a connected phone.  Scenarios are taken from thinking.md,
dsi-health-check runs, and UART/dmesg notes in this repo.

Usage:
  python3 scripts/dsi-bringup-sim.py
  python3 scripts/dsi-bringup-sim.py --scenario current_black
  python3 scripts/dsi-bringup-sim.py --fix bit24
"""

from __future__ import annotations

import argparse
import textwrap
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Callable

# ---------------------------------------------------------------------------
# Register model (simplified Qualcomm DSI 6G)
# ---------------------------------------------------------------------------

HS_REQ_SEL_PHY = 1 << 24
CLKLN_HS_FORCE = 1 << 28
DSI_CTRL_ENABLE = 1 << 0
DSI_CTRL_CLK_EN = 1 << 1
DSI_CTRL_VID_MODE = 1 << 3
DSI_CTRL_LANES = 0x1F0  # 4 data lanes enabled in healthy 0x1f3


@dataclass
class DsiState:
    """Minimal DSI + PHY + software state for simulation."""

    name: str = "boot"
    # PHY / clamp
    ldo: int = 0x1C
    clamp: bool = False
    # LANE_CTRL latched bits
    lane_ctrl: int = 0x0100_0000  # UEFI: bit24 often set
    # Derived lane link
    lane_status: int = 0x1F1F
    dsi_ctrl: int = 0x8
    fifo: int = 0xDDDD_1019
    # Panel / DPU
    panel_dcs_ok: bool = False
    power_mode: int = 0
    blank: int = 4
    dpms: str = "Off"
    vid_engine: bool = False
    frame_done: int = 0
    underrun: int = 0
    dsi_err: list[str] = field(default_factory=list)
    visible: str = "black"  # black | thin_line | image | bars

    def lane_ctrl_bits(self) -> tuple[bool, bool]:
        return bool(self.lane_ctrl & HS_REQ_SEL_PHY), bool(self.lane_ctrl & CLKLN_HS_FORCE)

    def recompute_lanes(self, non_continuous: bool = True) -> None:
        """Heuristic lane model calibrated against ginkgo /dev/mem experiments."""
        bit24, bit28 = self.lane_ctrl_bits()

        if self.clamp:
            self.lane_status = 0x1F1F
            return

        if self.ldo == 0x3C:
            self.lane_status = 0x1F0F  # data STOP, clk may run
            return

        if self.ldo != 0x1C:
            self.lane_status = 0x1F1F
            return

        # ginkgo: NON_CONTINUOUS + bit28 → data lanes stuck (thinking.md)
        if non_continuous and bit28:
            self.lane_status = 0x1F0F
            return

        # bit24=1 (UEFI) blocks data HS unless cleared (hs_req_sel true)
        if bit24:
            self.lane_status = 0x1F1F
            return

        # HS path open; clock lane may still show STOP in 0x1f00 pattern
        if self.vid_engine and self.panel_dcs_ok:
            self.lane_status = 0x0000
        elif self.vid_engine:
            self.lane_status = 0x1F00
        else:
            self.lane_status = 0x1F1F

    def recompute_dsi_ctrl(self) -> None:
        if not self.panel_dcs_ok:
            self.dsi_ctrl = 0x8
            return
        if self.vid_engine and self.lane_status == 0:
            self.dsi_ctrl = DSI_CTRL_ENABLE | DSI_CTRL_CLK_EN | DSI_CTRL_VID_MODE | DSI_CTRL_LANES
        elif self.vid_engine:
            self.dsi_ctrl = 0x8 | DSI_CTRL_VID_MODE
        else:
            self.dsi_ctrl = DSI_CTRL_ENABLE | DSI_CTRL_CLK_EN | DSI_CTRL_LANES & ~DSI_CTRL_VID_MODE

    def recompute_visible(self, tearcheck_wrong: bool = False) -> None:
        if self.lane_status != 0:
            self.visible = "black"
        elif tearcheck_wrong:
            self.visible = "thin_line"
        elif self.frame_done > 0 and self.blank == 0:
            self.visible = "image"
        elif self.lane_status == 0 and self.panel_dcs_ok:
            self.visible = "bars_or_black"  # pixels may flow but not confirmed
        else:
            self.visible = "black"

    def snapshot_line(self) -> str:
        b24, b28 = self.lane_ctrl_bits()
        return (
            f"  LANE_CTRL=0x{self.lane_ctrl:08x} (bit24={int(b24)} bit28={int(b28)})  "
            f"LANE_STATUS=0x{self.lane_status:04x}  DSI_CTRL=0x{self.dsi_ctrl:x}  "
            f"LDO=0x{self.ldo:x}  FIFO=0x{self.fifo:08x}  "
            f"panel_dcs={self.panel_dcs_ok} pm=0x{self.power_mode:x}  "
            f"vid={self.vid_engine} frame_done={self.frame_done}  "
            f"visible={self.visible}"
        )


class StepResult(Enum):
    PASS = auto()
    FAIL = auto()
    WARN = auto()
    SKIP = auto()


@dataclass
class Step:
    id: str
    layer: str
    desc: str
    action: Callable[["Simulator"], None]
    check: Callable[["Simulator"], StepResult]
    note: str = ""


class Simulator:
    def __init__(self, non_continuous: bool = True, tearcheck_wrong: bool = False):
        self.non_continuous = non_continuous
        self.tearcheck_wrong = tearcheck_wrong
        self.s = DsiState()
        self.log: list[str] = []

    def msg(self, line: str) -> None:
        self.log.append(line)

    def sync(self) -> None:
        self.s.recompute_lanes(self.non_continuous)
        self.s.recompute_dsi_ctrl()
        self.s.recompute_visible(self.tearcheck_wrong)

    def apply_mainline_ctrl_enable(self) -> None:
        """Model dsi_ctrl_enable() lane_ctrl path in dsi_host.c."""
        if not self.non_continuous:
            lc = self.s.lane_ctrl
            lc &= ~HS_REQ_SEL_PHY  # set_continuous_clock true on 14nm returns false — kept
            lc |= CLKLN_HS_FORCE
            self.s.lane_ctrl = lc
        # NON_CONTINUOUS: mainline currently does NOT touch LANE_CTRL

    def apply_downstream_hs_req_sel(self) -> None:
        self.s.lane_ctrl &= ~HS_REQ_SEL_PHY

    def apply_fix_bit24(self) -> None:
        self.apply_downstream_hs_req_sel()

    def apply_fix_bit28_non_cont(self) -> None:
        self.s.lane_ctrl &= ~CLKLN_HS_FORCE

    def apply_fix_ldo(self) -> None:
        self.s.ldo = 0x1C

    def apply_clamp_release(self) -> None:
        self.s.clamp = False


# Logged snapshots (thinking.md + SSH sessions)
SNAPSHOTS: dict[str, dict] = {
    "uefi_reset": {
        "lane_ctrl": 0x0100_0000,
        "ldo": 0x1C,
        "lane_status": 0x1F1F,
        "note": "UEFI 遗留 bit24=1",
    },
    "mainline_bit28_bad": {
        "lane_ctrl": 0x1100_0000,
        "lane_status": 0x1F0F,
        "note": "mainline 设 bit28，data lane 全 STOP (thinking.md)",
    },
    "after_clear_bit28": {
        "lane_ctrl": 0x0100_0000,
        "lane_status": 0x1F00,
        "note": "清 bit28 后 data HS，clk 仍 STOP (实验)",
    },
    "healthy_hs": {
        "lane_ctrl": 0x0100_0000,
        "lane_status": 0x0000,
        "dsi_ctrl": 0x1F3,
        "fifo": 0x0000_1010,
        "note": "thinking.md 最健康快照",
    },
    "ssh_black_l2": {
        "lane_ctrl": 0x0100_0000,
        "lane_status": 0x1F1F,
        "dsi_ctrl": 0x8,
        "fifo": 0x0000_9130,
        "frame_done": 0,
        "underrun": 6,
        "note": "2026-08-09 SSH 黑屏采样",
    },
    "ssh_brief_ok": {
        "lane_ctrl": 0x001F_001F,
        "lane_status": 0x0000,
        "dsi_ctrl": 0x1F3,
        "fifo": 0xDDDD_1011,
        "note": "刷机后短暂 LANE_STATUS=0 仍黑屏",
    },
}


def build_boot_steps() -> list[Step]:
    def s0_uefi(sim: Simulator) -> None:
        sim.s = DsiState(name="after_uefi", lane_ctrl=0x0100_0000, clamp=True)
        sim.sync()

    def s1_lcdb(sim: Simulator) -> None:
        pass  # LCDB OK in logs

    def s2_clamp(sim: Simulator) -> None:
        sim.apply_clamp_release()
        sim.sync()

    def s3_phy(sim: Simulator) -> None:
        sim.apply_fix_ldo()
        sim.sync()

    def s4_reset_timing(sim: Simulator) -> None:
        pass  # timing registers OK in logs

    def s5_ctrl_enable(sim: Simulator) -> None:
        sim.apply_mainline_ctrl_enable()
        sim.sync()

    def s6_panel(sim: Simulator) -> None:
        sim.s.panel_dcs_ok = True
        sim.s.power_mode = 0x9C
        sim.sync()

    def s7_dpu_modeset(sim: Simulator) -> None:
        sim.s.blank = 0
        sim.s.dpms = "On"

    def s8_host_enable(sim: Simulator) -> None:
        sim.s.vid_engine = True
        if sim.s.lane_status != 0:
            sim.s.fifo = 0xDDDD_1019
            sim.s.dsi_err.append("status=c (FIFO+underflow)")
            sim.s.underrun = 6
        sim.sync()

    def s9_kickoff(sim: Simulator) -> None:
        if sim.s.lane_status == 0:
            sim.s.frame_done = 2212
            sim.s.fifo = 0x0000_1010
        else:
            sim.s.frame_done = 0
        sim.sync()

    def s10_tearcheck(sim: Simulator) -> None:
        if sim.tearcheck_wrong and sim.s.lane_status == 0:
            sim.s.visible = "thin_line"

    checks = {
        "S1": lambda sim: StepResult.PASS,
        "S2": lambda sim: StepResult.PASS if not sim.s.clamp else StepResult.FAIL,
        "S3": lambda sim: StepResult.PASS if sim.s.ldo == 0x1C else StepResult.FAIL,
        "S5": lambda sim: (
            StepResult.PASS
            if sim.non_continuous and not (sim.s.lane_ctrl & CLKLN_HS_FORCE)
            else StepResult.WARN
            if sim.non_continuous
            else StepResult.FAIL
        ),
        "S5b": lambda sim: (
            StepResult.FAIL if (sim.s.lane_ctrl & HS_REQ_SEL_PHY) else StepResult.PASS
        ),
        "S6": lambda sim: StepResult.PASS if sim.s.power_mode == 0x9C else StepResult.FAIL,
        "S7": lambda sim: StepResult.PASS if sim.s.blank == 0 else StepResult.FAIL,
        "S8": lambda sim: StepResult.PASS if sim.s.dsi_ctrl & DSI_CTRL_VID_MODE else StepResult.FAIL,
        "L2": lambda sim: StepResult.PASS if sim.s.lane_status == 0 else StepResult.FAIL,
        "L2ctrl": lambda sim: StepResult.PASS if sim.s.dsi_ctrl == 0x1F3 else StepResult.FAIL,
        "L3": lambda sim: StepResult.PASS if sim.s.frame_done > 0 else StepResult.FAIL,
        "VIS": lambda sim: (
            StepResult.PASS
            if sim.s.visible in ("image", "bars_or_black", "thin_line")
            else StepResult.FAIL
        ),
    }

    return [
        Step("S0", "boot", "UEFI/ABL 留下 DSI 初态", s0_uefi, lambda _: StepResult.WARN, SNAPSHOTS["uefi_reset"]["note"]),
        Step("S1", "L1", "LCDB + 面板供电", s1_lcdb, checks["S1"]),
        Step("S2", "L1", "释放 PHY clamp", s2_clamp, checks["S2"]),
        Step("S3", "L1", "PHY enable, LDO=0x1c", s3_phy, checks["S3"]),
        Step("S4", "L2", "sw_reset → timing_setup", s4_reset_timing, lambda _: StepResult.PASS),
        Step("S5", "L2", "dsi_ctrl_enable (NON_CONTINUOUS 不设 bit28)", s5_ctrl_enable, checks["S5"]),
        Step("S5b", "L2", "LANE_CTRL bit24 应为 0 (hs_req_sel)", lambda _: None, checks["S5b"]),
        Step("S6", "L2", "panel DCS init + display on", s6_panel, checks["S6"]),
        Step("S7", "L4", "DRM modeset, blank=0", s7_dpu_modeset, checks["S7"]),
        Step("S8", "L2", "msm_dsi_host_enable (video engine)", s8_host_enable, checks["S8"]),
        Step("L2", "L2", "LANE_STATUS == 0", lambda _: None, checks["L2"]),
        Step("L2ctrl", "L2", "DSI_CTRL == 0x1f3", lambda _: None, checks["L2ctrl"]),
        Step("S9", "L3", "DPU kickoff / frame_done", s9_kickoff, checks["L3"]),
        Step("S10", "L3", "tearcheck / vsync 路由", s10_tearcheck, checks["VIS"]),
    ]


def run_scenario(name: str, fix_bit24: bool = False, fix_bit28: bool = False,
                 tearcheck_wrong: bool = False) -> None:
    print(f"\n{'=' * 72}")
    print(f"场景: {name}")
    print(f"{'=' * 72}")

    sim = Simulator(non_continuous=True, tearcheck_wrong=tearcheck_wrong)
    steps = build_boot_steps()

    for step in steps:
        if step.action:
            step.action(sim)
        if fix_bit24 and step.id == "S5":
            sim.apply_fix_bit24()
            sim.sync()
        if fix_bit28 and step.id == "S5":
            sim.apply_fix_bit28_non_cont()
            sim.sync()

        result = step.check(sim)
        icon = {StepResult.PASS: "✅", StepResult.FAIL: "❌", StepResult.WARN: "⚠️", StepResult.SKIP: "·"}[result]
        print(f"{icon} [{step.layer}] {step.id}: {step.desc}")
        if step.id in ("S5b", "L2", "L2ctrl", "S9", "S10") or result != StepResult.PASS:
            print(sim.s.snapshot_line())
        if step.note and step.id == "S0":
            print(f"     ({step.note})")
        if sim.s.dsi_err and step.id == "S8":
            print(f"     dmesg: {', '.join(sim.s.dsi_err)}")

    print(f"\n>>> 最终: 屏幕={sim.s.visible}  LANE_STATUS=0x{sim.s.lane_status:04x}  "
          f"frame_done={sim.s.frame_done}")


def replay_snapshot(key: str) -> None:
    snap = SNAPSHOTS[key]
    print(f"\n--- 历史快照 replay: {key} ---")
    print(f"    {snap.get('note', '')}")
    sim = Simulator()
    for k, v in snap.items():
        if k == "note":
            continue
        if hasattr(sim.s, k):
            setattr(sim.s, k, v)
    if snap.get("lane_status") == 0:
        sim.s.panel_dcs_ok = True
        sim.s.vid_engine = True
    sim.sync()
    print(sim.s.snapshot_line())


def main() -> None:
    parser = argparse.ArgumentParser(description="ginkgo DSI bring-up offline simulator")
    parser.add_argument("--scenario", choices=["boot", "current_black", "fix_bit24", "tearcheck", "all"],
                        default="all")
    args = parser.parse_args()

    print(textwrap.dedent("""
        ginkgo 显示上电流程 — 本机模拟器
        数据来源: thinking.md, SSH health-check, dmesg 笔记
        模型: 简化寄存器因果（非周期精确），用于对照测试清单
    """).strip())

    for key in SNAPSHOTS:
        replay_snapshot(key)

    scenarios = {
        "boot": [("mainline_当前行为 (未清 bit24)", False, False, False)],
        "current_black": [("当前黑屏路径 (SSH 2026-08-09)", False, False, False)],
        "fix_bit24": [("假设: S5 后清 bit24 (hs_req_sel)", True, False, False)],
        "tearcheck": [
            ("细线实验: L2 OK + 错误 tearcheck", True, False, True),
        ],
        "all": [
            ("A. 主线现状 — NON_CONTINUOUS 但不清 bit24", False, False, False),
            ("B. 修复 bit24 (下游 hs_req_sel true)", True, False, False),
            ("C. 错误设 bit28 (旧 mainline bug)", False, True, False),
            ("D. 细线: B + 错误 tearcheck", True, False, True),
        ],
    }

    for title, fix24, fix28, tc in scenarios[args.scenario]:
        run_scenario(title, fix_bit24=fix24, fix_bit28=fix28, tearcheck_wrong=tc)

    print(textwrap.dedent("""

        ------------------------------------------------------------------
        建议真机测试顺序 (与模拟结论一致):
          1. connect.sh + dsi-health-check.py          → 对照快照 ssh_black_l2
          2. python3 scripts/dsi-clear-hsreqsel.py   → 应对场景 B
          3. 再 health-check                         → 期望 LANE_STATUS=0
          4. 若 OK → 把清 bit24 写进 dsi_ctrl_enable → fastboot boot 验证
        ------------------------------------------------------------------
    """))


if __name__ == "__main__":
    main()
