**Language:** English | [简体中文](zh-CN/ginkgo-dsi-err-status5-analysis.md)

# ginkgo display black screen and `dsi_err status=5` analysis notes

> Device: Redmi Note 8 (ginkgo) · SM6125 · Tianma NT36672A 1080×2340  
> Related log: `backup/ginkgo/logs/uart-20260808-225704.log` (build #45)  
> Last updated: 2026-08-08 (this document is frozen as historical analysis from the **build #45 stage**)  
>
> **Follow-up: display output worked on 2026-08-17.** The `status=5` boot transient no longer blocks a picture. Full history: [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md).

This document records decoding of `dsi_err_worker: status=5` during the **backlight on, no image** stage, plus community experience and Android downstream backup comparison. Overview: [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md).

---

## 1. Symptom at the time (build #45, outdated)

| Stage | Backlight | Picture | Notes |
|------|------|------|------|
| UEFI boot | — | ✅ logo | `DisplayDxe: tianma panel 1080x2340` |
| Instant of kernel hand-over | — | Flash | UEFI picture → DRM re-init |
| After entering the OS | ✅ | ❌ black | systemd `gpio-backlight` turns on PMI632 GPIO6 |

**Key log (`uart-20260808-225704.log`):**

```
panel-tianma-nt36672a: panel init complete
panel-tianma-nt36672a: Skipping enable of already enabled panel
fb0: sys_fillrect: framebuffer is not in virtual address space
dsi_err_worker: status=5          # ×5，仅启动时
msm_dpu: [drm] fb0: msmdrmfb frame buffer device
arm-smmu: Unhandled context fault: iova=0x5c003000   # ×11
```

**Confirmed:**

- DCS command path is OK (`panel init complete`)
- No `vblank timeout` (gone after build #43)
- Build #45 already injected the downstream PHY timing blob and changed PHY `vdda` to 0.9V (`vreg_l7a`); **`status=5` remains**

---

## 2. What `status=5` means

Defined in mainline `linux/drivers/gpu/drm/msm/dsi/dsi_host.c`:

| Bit | Macro | Meaning |
|----|-----|------|
| `0x1` | `DSI_ERR_STATE_TIMEOUT` | `REG_DSI_TIMEOUT_STATUS` is non-zero (HS/LP/BTA timeout) |
| `0x2` | `DSI_ERR_STATE_DLN0_PHY` | PHY lane 0 error (**did not appear** this time) |
| `0x4` | `DSI_ERR_STATE_FIFO` | `REG_DSI_FIFO_STATUS` is non-zero (lane FIFO overflow/underflow) |
| `0x8` | `DSI_ERR_STATE_MDP_FIFO_UNDERFLOW` | MDP-side FIFO underflow (**did not appear** this time) |

**`status=5` = `0x1 | 0x4` = TIMEOUT + FIFO**

IRQ path: `dsi_host_irq` → `dsi_error()` → read `FIFO_STATUS` / `TIMEOUT_STATUS` etc. → `dsi_err_worker`.

### Versus other common status values (community)

| status | Combination | Typical scene |
|--------|------|----------|
| `4` | FIFO only | hdisplay / pclk mismatch with the panel; DSC width miscalculated |
| `5` | TIMEOUT + FIFO | Data stream out of sync after the video engine starts (**this case**) |
| `c` | PHY + FIFO | command-mode re-entry; severe PHY timing error |

Matching concepts in downstream `reference/downstream/drivers/dsi-staging/dsi_ctrl_hw.h`:

- `DSI_HS_TX_TIMEOUT` — high-speed forward-transmit timeout
- `DSI_DLNx_HS_FIFO_UNDERFLOW/OVERFLOW` — per data-lane FIFO errors

---

## 3. Timeline reconstruction (UART)

```
UEFI DisplayDxe
  ├─ tianma 1080×2340 init
  └─ logo @ (388, 123)                    → user-visible “flash”

~1.44s  display-subsystem → IOMMU group 2
~1.46s  IOMMU fault @ 0x5c003000 ×11      → still accessing the UEFI framebuffer region

~2.50s  [drm] Initialized msm 1.13.0
~2.66s  low vbp+vfp may lead to perf issues
~2.75s  panel init complete               → DCS OK
~2.87s  dsi_err status=5 ×5               → video stream start failed
~3.15s  fb0: msmdrmfb registered

~10s+   systemd-backlight turns on backlight        → backlight on, no pixels
```

**Layered conclusions:**

1. **UEFI display path is OK** — panel hardware and bootloader init are fine
2. **DCS path is OK** — the panel is already `display on`
3. **Video HS pixel stream is abnormal** — the direct cause of `status=5`
4. **Backlight is independent of the image** — GPIO backlight is opened later by userspace, after DRM

---

## 4. Community experience and references

### 4.1 DCS must be sent after the PHY PLL locks

- Source: [sm8150-linux-mainline#1](https://github.com/sm8150-linux-mainline/linux/issues/1)
- Problem: sending DCS from `prepare()` → `-22` or fake init
- Fix: `prepare()` only powers/resets; `enable()` sends DCS
- **ginkgo status:** already fixed (build #44 adjusted bridge order); log has `panel init complete`

### 4.2 DSI soft reset needs enough delay

- Source: [LKML AUTOSEL 4.9 — drm/msm/dsi reset](https://lists.freedesktop.org/archives/dri-devel/2019-October/241751.html)
- Fix: `msleep(20)` instead of `wmb()` in `dsi_sw_reset`
- **ginkgo status:** mainline already has `DSI_RESET_TOGGLE_DELAY_MS 20`

### 4.3 MDP and DSI flooding together causes FIFO

- Source: [Freedreno — pp done timeout + status=c](https://lists.freedesktop.org/archives/dri-devel/2019-November/243717.html)
- Problem: command-mode autorefresh stacked on an explicit commit
- **ginkgo:** video mode, so this is a weak match, but it shows FIFO often tracks upstream frame timing

### 4.4 hdisplay / pclk / DSC width mismatch → status=4

- Source: [LKML — DPU DSC INTF timing](https://www.spinics.net/lists/kernel/msg6110040.html)
- **ginkgo:** no DSC, but DPU INTF and DSI register widths must still match; the log has a `low vbp+vfp` warning

### 4.5 SM6125 mainline display already works on other devices

- Source: [msm8953-mainline/linux](https://github.com/msm8953-mainline/linux) — SM6125 MDSS/DPU/DSI landed in mainline
- Devices such as Xperia 10 II (Seine) have complete panel configs
- **Meaning:** the framework works; ginkgo’s problem is **board / panel / UEFI hand-off**

---

## 5. How to use the Android backup material

### 5.1 Directory index

See `reference/README.md` in the repo:

```
reference/downstream/
├── dts/xiaomi/ginkgo/
│   ├── display/dsi-panel-nt36672a-tianma-fhd-video.dtsi  # panel timing + DCS init
│   └── ginkgo-trinket-display.dtsi                       # PHY timing blob
├── dts/qcom/trinket-sde.dtsi                             # PHY node, clamp, supplies
└── drivers/dsi-staging/                                  # full enable-chain logic
```

`backup/ginkgo/` is mainly boot images and UART logs. **Display reverse-engineering should follow `reference/downstream/`.**

### 5.2 Downstream full power-on sequence (`dsi_display.c`)

```
clocks / PHY power-on
  → phy reset
  → disable clamp (release UEFI PHY freeze)     ← missing on mainline
  → toggle resync FIFO (v3/v4 PHY; ginkgo v2.0 has none)
  → panel DCS enable
  → video engine enable (dsi_display_vid_engine_enable)
```

Mainline (`dsi_manager.c` build #44+):

```
pre_enable:  DSI host power on
atomic_enable: panel bridge enable (DCS) → msm_dsi_host_enable (video)
```

### 5.3 Downstream vs mainline differences

| Item | Downstream (trinket / ginkgo) | Mainline (current) | Status |
|------|-------------------------|---------------|------|
| Panel mode | `non_burst_sync_event` | no BURST/SYNC_PULSE flag → same | ✅ |
| `tx-eot-append` | Explicitly on in DT | EOT on by default (no `NO_EOT`) | ✅ |
| `t-clk-pre/post` | `0x37` / `0x0f` | `qcom,dsi-clk-pre/post` already set | ✅ |
| PHY timing blob | 40 bytes per-lane | build #45 DT `qcom,dsi-phy-lane-timings` | ✅ added; err remains |
| PHY `vdda-0p9` | VDD_MX (pm6125 L7) | build #45 `vreg_l7a` + regulator | ✅ added; err remains |
| strength / lane-config | `[ff 06]` / `[00 00 10 0f]` | 14nm hardcoded; may not match fully | ⚠️ pending alignment |
| **PHY clamp** | `phy_clamp_base @ 0x5e01400` | **not mapped, no driver** | ❌ high priority |
| DSI host `vdda-1p2` | L18A | `vreg_l18a` on `mdss_dsi0` | ✅ |
| `lp11-init` | Present in DT | not seen on mainline | ⚠️ to investigate |
| cont splash cleanup | `dsi_display_splash_res_cleanup` | none | ⚠️ related to IOMMU fault |
| resync FIFO | v3/v4 only | none on 14nm-2290 | N/A (v2.0 PHY) |

### 5.4 PHY clamp (high-priority gap)

Downstream `dsi_phy_hw_v2_0_clamp_ctrl()` (`dsi_phy_hw_v2_0.c`):

- Register base: `0x5e01400` (`DSI_MDP_ULPS_CLAMP_ENABLE_OFF`)
- Role: release lane freeze after UEFI `DisplayDxe` init
- Downstream calls this from `dsi_display_set_clamp(false)` before the HS clock is established

Mainline `sm6125.dtsi` `mdss_dsi0_phy` only maps:

- `0x5e94400` dsi_phy
- `0x5e94500` dsi_phy_lane
- `0x5e94800` dsi_pll

**Missing the `0x5e01400` clamp region** — strongly correlated with “flash then black” and the IOMMU fault at `0x5c003000`.

### 5.5 Key panel DT properties (`dsi-panel-nt36672a-tianma-fhd-video.dtsi`)

```dts
qcom,mdss-dsi-traffic-mode = "non_burst_sync_event";
qcom,mdss-dsi-tx-eot-append;
qcom,mdss-dsi-bllp-eof-power-mode;
qcom,mdss-dsi-bllp-power-mode;
qcom,mdss-dsi-lp11-init;
qcom,mdss-dsi-reset-sequence = <1 10>, <0 10>, <1 10>;
```

PHY timing (`ginkgo-trinket-display.dtsi`):

```
[26 21 09 0b 06 02 04 a0] ×4 data lanes
[26 20 0a 0b 06 02 04 a0]   clk lane
```

---

## 6. Secondary issues (not the black-screen root cause, but recorded)

### 6.1 `fb0: framebuffer is not in virtual address space`

- MSM GEM framebuffers are often DMA-only, with no CPU vmap
- The fbdev console cannot `fillrect` / `imageblit`
- **DPU hardware scanout does not depend on a CPU mapping**; if DSI is healthy there should still be a picture
- Impact: fbcon cannot draw text; not the root cause of `status=5`

### 6.2 `Skipping enable of already enabled panel`

- `drm_panel_enable()` is called twice; the second call is skipped
- Redundant bridge / atomic commit chain; not fatal

### 6.3 IOMMU fault @ `0x5c003000`

- Inside the UEFI framebuffer (`~0x5c000000`)
- After display-subsystem joins the IOMMU, something still accesses the old FB address
- May relate to leftover UEFI display state or an unreleased clamp

---

## 7. Fixes already tried (build timeline)

| Build | Change | `dsi_err` | Picture |
|-------|------|-----------|------|
| #42 | SM6125 DSI 6g v2.9 clocks, `clk-pre/post` | status=4/5 | black |
| #43 | DCS moved to `enable()` | improved; no vblank timeout | black |
| #44 | atomic_enable: panel first, then `host_enable` | status=5 | black |
| #45 | PHY timing blob, PHY 0.9V, `dsi_tpg` debugfs | **still status=5** | flash then black |

---

## 8. Suggested fix priority

| Priority | Direction | Basis |
|--------|------|------|
| **P0** | Implement **PHY clamp release** (DTS `0x5e01400` + `dsi_phy_14nm` clamp_ctrl) | Downstream enable chain; explains flash + black |
| **P0** | Verify with **DSI TPG** (below) | Separate DSI PHY vs DPU |
| **P1** | Read the specific `TIMEOUT_STATUS` / `FIFO_STATUS` bits | Pin HS_TX timeout vs FIFO underflow |
| **P1** | Align downstream **strength / lane-config** | `trinket-sde.dtsi` |
| **P2** | DPU tearcheck / `qcom,te-source`, byte_intf clock | `low vbp+vfp` warning |
| **P2** | Handle IOMMU @ `0x5c000000` / cont splash cleanup | Stop DPU pointing at the UEFI FB |

---

## 9. Debug commands

### 9.1 SSH over USB networking

```bash
# USB NIC name can change each time
sudo ip addr add 192.168.7.1/24 dev enxXXXXXXXXXXXX
ssh root@192.168.7.2
```

### 9.2 Errors and DRM state

```bash
dmesg | grep -E 'dsi_err|panel init|vblank|iommu.*5c00'
cat /sys/kernel/debug/dri/0/kms | head -100
cat /sys/kernel/debug/clk/clk_summary | grep -i dsi
```

### 9.3 DSI test pattern (build #45+)

After display is enabled:

```bash
echo 1 > /sys/kernel/debug/dri/0/dsi_tpg
```

| Result | Meaning |
|------|------|
| Checkerboard appears | DSI + panel OK → look at DPU / plane / CRTC |
| Still black | Keep looking at PHY / clocks / clamp |

### 9.4 UART capture

```bash
sudo python3 scripts/uart-monitor.py
# logs: backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

---

## 10. Related source files (mainline)

| Purpose | Path |
|------|------|
| Board DTS | `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| SoC DTS | `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` |
| Panel driver | `linux/drivers/gpu/drm/panel/panel-novatek-nt36672a.c` |
| DSI manager | `linux/drivers/gpu/drm/msm/dsi/dsi_manager.c` |
| DSI host / err | `linux/drivers/gpu/drm/msm/dsi/dsi_host.c` |
| 14nm PHY | `linux/drivers/gpu/drm/msm/dsi/phy/dsi_phy_14nm.c` |
| DISPCC | `linux/drivers/clk/qcom/dispcc-sm6125.c` |
| Downstream reference | `reference/downstream/drivers/dsi-staging/` |
| Downstream DTS | `reference/downstream/dts/xiaomi/ginkgo/` |

---

## 11. Changelog

| Date | Content |
|------|------|
| 2026-08-08 | build #46: PHY clamp release (`0x5e01400`), DSI FIFO/timeout register logging |
