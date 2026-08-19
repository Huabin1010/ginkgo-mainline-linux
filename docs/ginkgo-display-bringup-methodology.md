**Language:** English | [简体中文](zh-CN/ginkgo-display-bringup-methodology.md)

# Redmi Note 8 (ginkgo) mainline display bring-up: full approach and fix record

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125**  
> Panel: Tianma **NT36672A** · 1080×2340 · 4-lane MIPI DSI Video Mode  
> Purpose: systematically summarize the **overall fix strategy** for “backlight on, no image”, the layered diagnosis method, verified root causes, and code changes.  
> Last updated: 2026-08-17 (**magenta framebuffer visible to the user; the full DRM/DPU/DSI link is up**)

**Full scanout story (FIFO / TPG / INTF prefetch / boot order):**  
[ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)

**Related docs:**

| Doc | Contents |
|-----|----------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | **2026-08-17 full scanout record and lessons** |
| [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md) | **2026-08-17 full touch record** |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Whole-device bring-up timeline |
| [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) | Early `dsi_err status=5` deep dive (obsolete stage) |
| [thinking.md](../thinking.md) | Register-level reverse-engineering and experiment notes (raw record) |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | **Agent debug skill** (SOP + decision tree) |
| [reference/downstream/](../reference/downstream/) | LineageOS / Qualcomm downstream display reference |

---

## Contents

1. [Problem definition and goals](#1-problem-definition-and-goals)
2. [Display subsystem architecture](#2-display-subsystem-architecture)
3. [Symptom timeline](#3-symptom-timeline)
4. [Overall fix strategy (layered model)](#4-overall-fix-strategy-layered-model)
5. [Phase 1: get the DRM stack running](#5-phase-1-get-the-drm-stack-running)
6. [Phase 2: power and DCS command path](#6-phase-2-power-and-dcs-command-path)
7. [Phase 3: DSI high-speed video link (the hard part)](#7-phase-3-dsi-high-speed-video-link-the-hard-part)
8. [Phase 4: software DPMS / fbdev black screen](#8-phase-4-software-dpms--fbdev-black-screen)
9. [Register-level diagnostics (/dev/mem)](#9-register-level-diagnostics-devmem)
10. [Implemented code change list](#10-implemented-code-change-list)
11. [Downstream vs mainline comparison](#11-downstream-vs-mainline-comparison)
12. [Current status and known leftovers](#12-current-status-and-known-leftovers)
13. [Recommended debug SOP](#13-recommended-debug-sop)
14. [Lessons and principles](#14-lessons-and-principles)

---

## 1. Problem definition and goals

### 1.1 User-visible symptoms

| Stage | UEFI Logo | After kernel takes over | Backlight | Image |
|-------|-----------|-------------------------|-----------|-------|
| Early | ✅ | fully black | ❌ | ❌ |
| Mid | ✅ flashes | black screen | ✅ | ❌ |
| After PHY / DCS fix | ✅ flashes | black or white (depends on DPMS) | ✅ | link not stable |
| **2026-08-17** | ✅ | **magenta test pattern visible** | ✅ KTD3136 | **pixels on screen** |

**Goal:** get pixels on screen via the full **DRM/MDSS/DPU/DSI** path on mainline Linux, **without falling back to simple-framebuffer**.

### 1.2 Non-goals (deliberately not done)

- Do not keep the whole Android downstream `dsi-staging` framework in the kernel
- Do not use `simple-framebuffer` as a placeholder (it cannot validate the real DSI link)
- Do not depend on the panel for day-to-day debug (SSH + UART + `/dev/mem` first)

### 1.3 Core pass criteria

Display is “really fixed” only when all of these hold:

1. **DCS bidirectional comms work** — `power mode readback: 0x9c` (matches downstream ESD check)
2. **DSI data lanes enter HS** — `LANE_STATUS` STOPSTATE bits are 0
3. **No sustained `dsi_err`** — start-up transient FIFO errors are acceptable; runtime must not spam
4. **DPMS / CRTC on** — `fb0/blank = 0`, connector `dpms = On`
5. **Pixels visible to the user** — fbcon, all-white test, or desktop

---

## 2. Display subsystem architecture

### 2.1 Hardware datapath

```
┌─────────┐    ┌─────┐    ┌─────────┐    ┌──────────┐    ┌─────────────┐
│  DPU    │───▶│ CTL │───▶│ INTF_1  │───▶│ DSI Host │───▶│ 14nm PHY    │───▶ panel NT36672A
│ 5e01000 │    │     │    │ (video) │    │ 5e94000  │    │ 5e94400     │     Tianma 1080×2340
└─────────┘    └─────┘    └─────────┘    └──────────┘    └─────────────┘
     ▲                                        │
     │                                        ├── ESC: DCS commands (LP mode)
  disp_cc                                   └── HS: RGB pixel stream (Video mode)
  (pclk/byte/esc)
```

### 2.2 Software stack

```
fbcon / userspace
       │
       ▼
  drm_fb_helper (fb0)          ← may default to DPMS OFF (see §8)
       │
       ▼
  msm_drm (DPU KMS)            ← dpu_kms.c, dpu_encoder_phys_vid.c
       │
       ├── drm_bridge: dsi_mgr  ← dsi_manager.c (panel DCS → host enable order)
       └── drm_panel: nt36672a  ← panel-novatek-nt36672a.c
                │
                ▼
           msm_dsi_host          ← dsi_host.c (LANE_CTRL bit28 is critical)
                │
                ▼
           msm_dsi_phy_14nm      ← dsi_phy_14nm.c (LDO_CNTRL is critical)
```

### 2.3 Handoff from UEFI

- UEFI `DisplayDxe` already initialized the Tianma panel and showed the logo → **hardware and panel themselves are fine**
- Residual UEFI display state when the kernel takes over:
  - PHY may be in **clamp (freeze)**
  - `LANE_CTRL` may still have `HS_REQ_SEL_PHY` (bit24)
  - IOMMU fault @ `0x5c003000` (access leftover UEFI framebuffer address)

**Conclusion:** the kernel must **release clamp → reconfigure PHY → re-run panel init → start the video engine** in the correct order. Do not assume UEFI state is inheritable.

### 2.4 Backlight is independent of the image

ginkgo backlight is decided by **two paths** together:

| Layer | Mechanism | Mainline implementation |
|-------|-----------|-------------------------|
| Boost enable | PMI632 **GPIO6** | `gpio-backlight` (`default-on`) |
| Brightness | panel DCS **`0x51`** + **`0x53`** | `ginkgo_tianma_on_cmds_2` `0x51,0xB8` / `0x53,0x2C` |

So you can see:

- **Backlight on, no image** — GPIO boost is on, but DSI has no pixels / panel is not display-on
- **No backlight, image present** — theoretically possible (DCS brightness 0); uncommon on ginkgo

---

## 3. Symptom timeline

| Build / stage | Key observation | Conclusion at the time |
|---------------|-----------------|------------------------|
| Early | no `/dev/dri`, panel `-22` | driver not built in, wrong PHY address, missing pinctrl |
| After LCDB driver | `panel init complete` but still black | DCS LP path works, video HS does not |
| build #43+ | `panel init complete`, no vblank timeout | clocks broadly OK |
| build #45 | `dsi_err status=5` ×8, backlight no image | TIMEOUT + FIFO, data lanes stuck LP-11 |
| After register experiments | clear bit28 + LDO `0x1c` → `LANE_STATUS=0` | **root cause locked to PHY/DSI config** |
| After fb diagnosis | `fb0/blank=4` (POWERDOWN) | **software layer also blanking the panel** |

---

## 4. Overall fix strategy (layered model)

We split the black-screen problem into **five layers** and ruled them out bottom-up:

```
┌────────────────────────────────────────────────────────────┐
│ L5 userspace    fbcon DPMS, systemd backlight, desktop compositor │
├────────────────────────────────────────────────────────────┤
│ L4 software fb  fbdev deferred, blank=4, GEM no CPU mapping │
├────────────────────────────────────────────────────────────┤
│ L3 display pipe DPU modeset, INTF timing, CRTC enable       │
├────────────────────────────────────────────────────────────┤
│ L2 DSI video    HS data, FIFO, LANE_CTRL, traffic mode      │
├────────────────────────────────────────────────────────────┤
│ L1 electrical   LCDB bias, PHY LDO, clamp, PLL, panel reset │
└────────────────────────────────────────────────────────────┘
```

**Principles:**

1. **Prove the lower layer OK before debugging the upper one** — do not tune DPU timing without `panel init complete`
2. **Verify hypotheses with read-only experiments** — reading `LANE_STATUS` via `/dev/mem` is faster than guessing driver logic
3. **Compare against downstream DTS + registers** — ginkgo has a complete LineageOS reference
4. **Change one variable at a time** — so it correlates with the UART log
5. **Distinguish “link up” from “there is a picture”** — after the link is up, DPMS can still turn it off

---

## 5. Phase 1: get the DRM stack running

### 5.1 Problem and fix summary

| Problem | Root cause | Fix |
|---------|------------|-----|
| no `/dev/dri` | panel driver `=m` not loaded; fragment not merged every time | `CONFIG_DRM_PANEL_NOVATEK_NT36672A=y`; `build-kernel.sh` force-merge |
| `msm_dsi_phy 0.phy` | MDSS `#address-cells=1` parsed PHY address as 0 | `#address-cells/#size-cells = <2>` |
| PHY does not probe | wrong compatible | `qcom,dsi-phy-14nm-2290` |
| `failed to get reset gpio` | no `PINCTRL_MSM` | enable `CONFIG_PINCTRL_SM6125` |
| TE pinctrl deadlock | `mdss_te_active` blocked panel probe | remove TE pinctrl from the panel node |
| no SMMU / ICC | display cannot finish devlink | `CONFIG_ARM_SMMU`, SM6125 interconnect |

### 5.2 Config highlights (`config/ginkgo.fragment`)

```kconfig
CONFIG_DRM_MSM=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y
CONFIG_PINCTRL_MSM=y
CONFIG_PINCTRL_SM6125=y
CONFIG_REGULATOR_QPNP_LCDB=y
CONFIG_SM_DISPCC_6125=y
```

### 5.3 Phase acceptance

```bash
ls /dev/dri/card0 /dev/fb0
dmesg | grep "Initialized msm"
ls /sys/bus/mipi-dsi/devices/5e94000.dsi.0/driver
```

---

## 6. Phase 2: power and DCS command path

### 6.1 LCDB panel bias (PMI632)

Tianma NT36672A needs **positive bias VSP + negative bias VSN**, produced by PMI632 internal **LCDB**, not a simple `regulator-fixed`.

**Changes:**

- Added `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c`
- `pmi632.dtsi`: `qpnp-lcdb@ec00` → `lcdb_ldo_vreg` / `lcdb_ncp_vreg`
- `sm6125-xiaomi-ginkgo.dts`:
  - `vddpos-supply = <&lcdb_ldo_vreg>`
  - `vddneg-supply = <&lcdb_ncp_vreg>`

**Acceptance:**

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
panel-tianma-nt36672a: power mode readback: 0x9c
panel-tianma-nt36672a: panel init complete
```

`0x9c` matches downstream `qcom,mdss-dsi-panel-status-value` → **DCS command path (LP mode) is OK**.

### 6.2 Panel init order (DCS must wait until DSI host is ready)

Downstream and community experience ([sm8150-linux-mainline#1](https://github.com/sm8150-linux-mainline/linux/issues/1)):

- `prepare()`: power on, reset
- `enable()`: send DCS init (needs DSI link already established)

**Mainline fix (`dsi_manager.c`):**

In `dsi_mgr_bridge_atomic_enable()`, **first** `panel_bridge` enable (DCS), **then** `msm_dsi_host_enable()` (video engine).

**Panel driver (`panel-novatek-nt36672a.c`) ginkgo sequence:**

1. `on_cmds_1` — vendor register init
2. `mipi_dsi_dcs_exit_sleep_mode()` + 80ms
3. `on_cmds_2` — includes `0x51` brightness, `0x53` BL enable
4. `mipi_dsi_dcs_set_display_on()`
5. power mode readback check

### 6.3 Timing and mode flags (aligned with downstream)

Downstream `dsi-panel-nt36672a-tianma-fhd-video.dtsi`:

```dts
qcom,mdss-dsi-traffic-mode = "non_burst_sync_event";
qcom,mdss-dsi-h-sync-pulse = <0>;
```

Mainline equivalent:

```c
.mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
        | MIPI_DSI_CLOCK_NON_CONTINUOUS,
```

- No `MIPI_DSI_MODE_VIDEO_BURST` → `dsi_get_traffic_mode()` returns `NON_BURST_SYNCH_EVENT` ✅
- `MIPI_DSI_CLOCK_NON_CONTINUOUS` → **forbid** `LANE_CTRL` bit28 (see §7.2)

**Display mode clock:**

```
htotal = 1080 + 90 + 2 + 120 = 1292
vtotal = 2340 + 10 + 3 + 8  = 2361
clock  = 1292 × 2361 × 60 / 1000 ≈ 183012 kHz
```

---

## 7. Phase 3: DSI high-speed video link (the hard part)

### 7.1 Symptom

After DCS was fixed, still **backlight on, no image**. `dmesg` showed:

```
dsi_err_worker: status=5
msm_dsi: DSI FIFO status: 0xdddd1019
msm_dsi: DSI timeout status: 0x1
```

**`status=5` = `0x1 | 0x4` = TIMEOUT + FIFO** — after the video engine starts, data lanes cannot transfer HS pixels normally.

### 7.2 Root cause A: `LANE_CTRL` bit28 (`CLKLN_HS_FORCE_REQUEST`)

**Mechanism:**

In `dsi_host.c` `dsi_ctrl_enable()`:

```c
if (!(flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)) {
    ...
    dsi_write(REG_DSI_LANE_CTRL,
        lane_ctrl | DSI_LANE_CTRL_CLKLN_HS_FORCE_REQUEST);  // bit28
}
```

- Mainline default: if `NON_CONTINUOUS` is not declared, **force clock lane continuous HS**
- Downstream ginkgo: **no** `qcom,mdss-dsi-force-clock-lane-hs`, never sets bit28

**Measured (`/dev/mem`):**

| `LANE_CTRL` bit28 | `LANE_STATUS` | Meaning |
|-------------------|---------------|---------|
| set (before fix) | `0x1f0f` | 4 data lanes permanently STOPSTATE (LP-11) |
| clear + soft reset | `0x1f00` → `0x0` | data lanes enter HS, FIFO recovers |

**Fix:**

Add `MIPI_DSI_CLOCK_NON_CONTINUOUS` to `ginkgo_tianma_panel_desc.mode_flags`.

### 7.3 Root cause B: `PHY LDO_CNTRL` overwritten to `0x3c`

**Mechanism:**

`dsi_14nm_phy_enable()` correctly writes `0x1c` for a standalone PHY:

```c
static u32 dsi_14nm_phy_ldo_cntrl(struct msm_dsi_phy *phy)
{
    u32 data = 0x1c;
    if (phy->usecase != MSM_DSI_PHY_STANDALONE)
        data |= DSI_14nm_PHY_CMN_LDO_CNTRL_VREG_CTRL(32);  // → 0x3c
    return data;
}
```

But `pll_db_commit_14nm()` **used to hard-code** `writel(0x3c, LDO_CNTRL)`, overwriting the correct value.

- `0x3c` is the **bonded dual-DSI** config; it overdrives a standalone PHY LDO
- Data lanes **cannot leave LP-11**

**Fix:**

- Extract `dsi_14nm_phy_ldo_cntrl()`; `pll_db_commit_14nm()` and `dsi_14nm_phy_enable()` both call it

### 7.4 Root cause C: PHY clamp not released (UEFI leftover)

Downstream calls `dsi_display_set_clamp(false)` before HS is established, writing the `0x5e01400` region.

**Fix:**

- `sm6125.dtsi`: `mdss_dsi0_phy` adds `dsi_phy_clamp` register `0x5e01400`
- `dsi_phy.c`: `msm_dsi_phy_clamp_ctrl(phy, false)` before PHY enable

### 7.5 Supporting fixes: PHY timing and supplies

| Item | Downstream | Mainline fix |
|------|------------|--------------|
| Lane timing blob | 40 bytes × 5 lanes | `qcom,dsi-phy-lane-timings` in ginkgo DTS |
| PHY `vdda` 0.9V | VDD_MX (`vreg_l7a`) | `mdss_dsi0_phy` `vdda-supply` |
| `t-clk-pre/post` | `0x37` / `0x0f` | `qcom,dsi-clk-pre/post` on `mdss_dsi0` |
| Lane CFG0/CFG1 | downstream 0 | blob path also writes `CFG0=0, CFG1=0` |
| `byte0_div` clock | SM6115 has `@0x20d4` | add div in `dispcc-sm6125.c`, hang `byte0_intf` on div output |

### 7.6 Diagnostic logs and tools

- `dsi_host.c`: detailed `drm_err` for FIFO / timeout
- `dpu_kms.c`: debugfs `dsi_tpg` (DSI test pattern, write-only)
- Used to separate “DPU not sending pixels” vs “DSI cannot send them out”

### 7.7 Phase acceptance

```python
# Read on device via /dev/mem (see §9)
LANE_STATUS (0x5e940a4) == 0x00000000   # all STOPSTATE bits 0
PHY LDO_CNTRL (0x5e9444c) == 0x0000001c
LANE_CTRL bit28 == 0
```

```bash
dmesg | grep -E 'panel init|dsi_err'   # init OK; dsi_err only as boot transient
```

---

## 8. Phase 4: software DPMS / fbdev black screen

### 8.1 Problem

After the DSI link was already fixed (`LANE_STATUS=0`), there could still be **no image**, because:

```
/sys/class/graphics/fb0/blank = 4   # FB_BLANK_POWERDOWN
→ drm_fb_helper_dpms(DRM_MODE_DPMS_OFF)
→ CRTC / encoder off
```

**Cause:** fbdev registered with `FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS` stays blanked on a **headless boot** (SSH only, no one opens fb).

### 8.2 Fix approach

| Option | Notes | Status |
|--------|-------|--------|
| **userspace** `display-unblank.service` | `echo 0 > fb0/blank` at boot | ✅ already in `rootfs-overlay` |
| Kernel `drm_fb_helper_restore_fbdev_mode()` in probe | calling too early can hang boot | ❌ reverted |
| Manual test | `echo 0 > /sys/class/graphics/fb0/blank` + write white to fb0 | ✅ verified it can trigger DPMS On |

### 8.3 Relation to “backlight is independent”

Even with DPMS OFF, **gpio-backlight** may already have been lit by systemd → user sees “backlight on, no image”.

Easy to misread as “DSI still down” when the link may already be fine.

### 8.4 Phase acceptance

```bash
cat /sys/class/graphics/fb0/blank      # 0
cat /sys/class/drm/card0-DSI-1/dpms    # On
```

---

## 9. Register-level diagnostics (/dev/mem)

### 9.1 Prerequisites

- Kernel must allow `/dev/mem` MMIO access (works on this ginkgo)
- DSI controller physical base: `0x05e94000`
- PHY common register base: `0x05e94400`
- Mainline `dsi_read()` is **+4** from the resource base (HW_VERSION at offset 0); `/dev/mem` reads by **physical address**

### 9.2 Key registers

| Physical address | Name | Healthy value (ginkgo) | Notes |
|------------------|------|------------------------|-------|
| `0x5e94004` | `DSI_CTRL` | `0x1f3` | enable + 4 lane |
| `0x5e94008` | `STATUS0` | bit3 `VIDEO_MODE_ENGINE_BUSY` | video engine busy |
| `0x5e9400c` | `FIFO_STATUS` | not `0x55551019` | FIFO error bits |
| `0x5e940a4` | `LANE_STATUS` | **`0x0`** | all STOPSTATE 0 = lanes in HS |
| `0x5e940a8` | `LANE_CTRL` | bit28=0, bit24=0 | no forced continuous clock HS |
| `0x5e9444c` | `PHY LDO_CNTRL` | **`0x1c`** | correct standalone LDO |
| `0x5e94158` | `TEST_PATTERN_GEN_CTRL` | for TPG experiments | written via debugfs `dsi_tpg` |

### 9.3 Quick sampling script (run on device over SSH)

```python
import mmap, os, struct, time

def rd(addr):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=addr & ~0xfff)
    v = struct.unpack_from("<I", m, addr & 0xfff)[0]
    m.close(); os.close(fd)
    return v

DSI = 0x5e94000
PHY = 0x5e94400

print("LANE_STATUS", hex(rd(DSI + 0xa4)))
print("LANE_CTRL  ", hex(rd(DSI + 0xa8)))
print("FIFO       ", hex(rd(DSI + 0x0c)))
print("LDO_CNTRL  ", hex(rd(PHY + 0x4c)))

# continuous LANE_STATUS samples
hist = {}
for _ in range(100):
    v = rd(DSI + 0xa4)
    hist[v] = hist.get(v, 0) + 1
    time.sleep(0.002)
print("LANE_STATUS hist", {hex(k): v for k, v in hist.items()})
```

### 9.4 Experiment: verify the bit28 hypothesis

1. Read `LANE_CTRL` @ `0x5e940a8`
2. Write `LANE_CTRL & ~BIT(28)` (needs `O_RDWR`)
3. DSI soft reset
4. Sample `LANE_STATUS` again

If STOPSTATE goes from all-1 to 0 → bit28 is confirmed as the root cause.

---

## 10. Implemented code change list

### 10.1 Device tree

| File | Change |
|------|--------|
| `sm6125-xiaomi-ginkgo.dts` | panel node, LCDB supplies, PHY timing blob, `vdda`, `dsi-clk-pre/post` |
| `sm6125.dtsi` | MDSS address cells, PHY compatible, **`dsi_phy_clamp`** |
| `pmi632.dtsi` | LCDB regulator node |

### 10.2 Panel driver

| File | Change |
|------|--------|
| `panel-novatek-nt36672a.c` | ginkgo-specific init sequence, `MIPI_DSI_CLOCK_NON_CONTINUOUS`, `prepare_prev_first`, DCS order in enable |

### 10.3 DSI / PHY

| File | Change |
|------|--------|
| `dsi_host.c` | FIFO/timeout logs; do not set bit28 when `NON_CONTINUOUS` |
| `dsi_phy_14nm.c` | `dsi_14nm_phy_ldo_cntrl()`; PLL commit no longer hard-codes `0x3c`; CFG0/CFG1=0 |
| `dsi_phy.c` / `dsi.h` | `msm_dsi_phy_clamp_ctrl()` |
| `dsi_manager.c` | atomic_enable: panel DCS first, then host_enable; clamp off before enable |

### 10.4 Clocks

| File | Change |
|------|--------|
| `dispcc-sm6125.c` | add `byte0_div_clk_src` @ `0x20d4`, hang `byte0_intf` on the div |
| `qcom,dispcc-sm6125.h` | `DISP_CC_MDSS_BYTE0_DIV_CLK_SRC` index |

### 10.5 Regulator

| File | Change |
|------|--------|
| `qcom-qpnp-lcdb-regulator.c` | **new**, PMI632 LCDB |

### 10.6 Userspace

| File | Change |
|------|--------|
| `rootfs-overlay/usr/local/sbin/display-unblank.sh` | unblank fb0 at boot |
| `rootfs-overlay/.../display-unblank.service` | systemd unit |

### 10.7 Debug

| File | Change |
|------|--------|
| `dpu_kms.c` | debugfs `dsi_tpg` |

---

## 11. Downstream vs mainline comparison

| Item | Downstream ginkgo | Mainline (after fix) | Status |
|------|-------------------|----------------------|--------|
| Traffic mode | `non_burst_sync_event` | default NON_BURST_SYNCH_EVENT | ✅ |
| Clock lane HS force | none | `NON_CONTINUOUS` does not set bit28 | ✅ |
| PHY LDO | fixed `0x1c` | `dsi_14nm_phy_ldo_cntrl()` | ✅ |
| PHY clamp | `0x5e01400` | `dsi_phy_clamp` + driver | ✅ |
| PHY timing blob | 40B × 5 | DTS `qcom,dsi-phy-lane-timings` | ✅ |
| PHY vdda 0.9V | VDD_MX | `vreg_l7a` | ✅ |
| Panel DCS order | sleep out → init → display on | `panel_enable()` equivalent | ✅ |
| Backlight | DCS + PWM + GPIO6 | GPIO6 only (DCS in init) | ⚠️ no PWM dimming |
| `byte0_div` | present | added in `dispcc-sm6125` | ✅ |
| TE pin | has pinctrl | removed (avoid deadlock) | ✅ |
| fbdev DPMS | Android SurfaceFlinger | needs `display-unblank` | ⚠️ userspace |

---

## 12. Current status and known leftovers

### 12.1 Solved (including 2026-08-17 scanout)

- DRM/DSI driver probe and bind
- LCDB bias, panel DCS init, power mode readback
- DSI data lane HS (stable `LANE_STATUS=0x1f00` when pixels are on screen; earlier docs wrote `0x0`, which was a misread / blanking sample)
- PHY LDO `0x1c`, clamp release, timing blob
- Located DPMS OFF blanking and userspace mitigation
- KTD3136 backlight (I2C `0x36`, HWEN = PMI632 GPIO6)
- DSI TPG proved full 1080 panel width works
- INTF_1 `prog_fetch_lines_worst_case=0` (24-line prefetch with VFP=10 blows FIFO to `0xcccc1019`)
- MDP clock uses `mode->clock * 1000`, `clk_inefficiency_factor=218` (**do not use 220**; MODE_CLOCK_HIGH drops every mode)
- kickoff: `host_enable` first (20ms HS cycle with INTF off), then `enable_timing(1)`
- **User confirmed magenta framebuffer visible**

Details and lessons: [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md).

### 12.2 Leftovers (do not block scanout)

| Item | Notes |
|------|-------|
| encoder vsync ~11 Hz | INTF `FRAME_COUNT` still 60fps; IRQ gated dynamically |
| Boot-transient `dsi_err` / `tries=9 LANE_ST=0x1f1f` | INTF off during host_enable; expected |
| `lcdb_ncp` sysfs reads 12.4V | may be a get_voltage reporting bug |
| IOMMU fault `0x5c00xxxx` | leftover UEFI FB access |
| `fb0: not in virtual address space` | affects fbcon text, not hardware scanout |
| PWM backlight | KTD3136 is current source of truth; downstream PWM stack not replicated |
| restore fbdev inside kernel probe | hung boot; reverted |

### 12.3 Not recommended

- Force `drm_fb_helper_restore_fbdev_mode_unlocked()` during probe (hung on ginkgo)
- Fall back to simple-framebuffer (hides DSI problems)
- Unconditionally set `LANE_CTRL` bit28 (does not apply to ginkgo Tianma)
- `clk_inefficiency_factor=220` (exceeds 400 MHz cap; connector modes go empty)
- 20ms `SOFT_RESET` of DSI while INTF is already enabled
- Leave `DYNAMIC_FORCE_ON` on `CLK_CTRL`
- mmap past the INTF_1 block (reading `INTF+0x6a8` hangs SoC/USB)
- Ask the user to look while FIFO is still `0xcccc`/`0x5555`

---

## 13. Recommended debug SOP

### 13.1 First check after boot (SSH)

```bash
# 1. panel and DRM
dmesg | grep -iE 'panel init|power mode|dsi_err|msm_drm|fb0'

# 2. DPMS
cat /sys/class/graphics/fb0/blank
cat /sys/class/drm/card0-DSI-1/dpms

# 3. if blank != 0
echo 0 > /sys/class/graphics/fb0/blank

# 4. write test color only after FIFO is healthy (stride 4352, magenta BGRA)
# first confirm LANE=0x1f00 FIFO=0x1010, otherwise do not ask the user to look
python3 -c "
import os
w,h,stride=1080,2340,4352
row=b'\\xff\\x00\\xff\\x00'*w
f=open('/dev/fb0','r+b',0)
for y in range(h): f.seek(y*stride); f.write(row)
print('done')
"
```

### 13.2 Still black: register layer

Run the §9.3 script. Focus on `LANE_STATUS` (healthy=`0x1f00`), `FIFO_STATUS` (healthy=`0x1010`), `INTF_CONFIG` bit31 (prefetch should be off), `LDO_CNTRL`.

### 13.3 If `dsi_err` persists

```bash
dmesg | grep -iE 'FIFO|timeout|dsi_err'
cat /sys/kernel/debug/clk/clk_summary | grep -iE 'dsi|byte|pclk'
```

Compare against the downstream timing blob and `reference/downstream/dts/xiaomi/ginkgo/display/`.

### 13.4 UART capture

```bash
sudo python3 scripts/uart-monitor.py
# logs: backup/ginkgo/logs/uart-*.log
```

---

## 14. Lessons and principles

### 14.1 Key insights

1. **“Backlight on” ≠ “display link up”** — backlight GPIO / KTD3136 is independent of DSI pixels
2. **“panel init complete” ≠ “there is an image”** — DCS is LP, video is HS; both must work
3. **“LANE_STATUS all STOP” is hardware hard evidence** — video-mode healthy value is `0x1f00`, not the earlier misread `0x0`
4. **One-bit mainline vs downstream differences can be fatal** — bit28, LDO `0x3c` were exactly that
5. **Software DPMS can manufacture a “fake black screen”** — after the link is fixed, still check `fb0/blank`
6. **DSI TPG up ≠ DPU up** — TPG bypasses MDP; `FIFO=0xcccc1019` is INTF→DSI mismatch
7. **INTF programmable fetch fights DSI BLLP** — do not use catalog default 24-line prefetch when VFP=10
8. **`dpu_crtc_mode_valid` can silently drop the mode** — factor 220 → no fb0, looks like display is fully dead
9. **Do not 20ms DSI SOFT_RESET while INTF is running** — HS settles, FIFO is permanently blown
10. **FIFO sticky bits need W1C then resample** — reading `0xcccc` only means overflow/underflow happened

### 14.2 Fix priority (rule of thumb)

```
power/LCDB → driver probe → DCS → PHY/LANE → clocks → DPU INTF/FIFO → DPMS/fbdev → user looks at panel
```

Skipping layers causes misdiagnosis (e.g. tuning DPU timing before DSI is in HS; asking the user to look while FIFO is unhealthy).

### 14.3 Documentation and experiment records

- Record **build number + UART log filename** after every flash
- Write register experiments into `thinking.md` or an appendix here
- Downstream diffs are authoritative from `reference/downstream/`, not from memory

---

## Appendix A: build and flash

```bash
cd .
./scripts/build-kernel.sh
./scripts/build-bootimg.sh
# phone in TWRP recovery
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

## Appendix B: related kernel log snippets (healthy boot)

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
[drm] Initialized msm 1.13.0 for 5e01000.display-controller on minor 0
panel-tianma-nt36672a: power mode readback: 0x9c
panel-tianma-nt36672a: panel init complete
msm_dpu: [drm] fb0: msmdrmfb frame buffer device
# dsi_err status=5 only a few times within ~3.1s after boot, none after
```

## Appendix C: causal chain

```
UEFI logo OK
       │
       ▼
kernel DRM init
       │
       ├── LCDB / reset / clamp ──fail──▶ no panel init
       │
       ▼
panel init complete (DCS LP OK)
       │
       ├── LDO 0x3c or bit28 ──fail──▶ data lane LP-11, dsi_err status=5
       │
       ▼
LANE_STATUS = 0x1f00 (HS OK)
       │
       ├── fb0 blank=4 (DPMS OFF) ──fail──▶ backlight on, no image
       │
       ▼
INTF timing + DPU pixels
       │
       ├── INTF PROG_FETCH + short VFP ──fail──▶ FIFO 0xcccc, backlight on, picture black
       │
       ├── kickoff DSI 20ms reset with INTF on ──fail──▶ FIFO permanently 0xcccc
       │
       ▼
FIFO=0x1010 + magenta/fbcon ──▶ image visible to user
```

---

*This document is updated as bring-up progresses. If the fix strategy or root-cause conclusions change, also update the display status table in [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md).*
