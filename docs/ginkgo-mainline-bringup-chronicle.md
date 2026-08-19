**Language:** English | [简体中文](zh-CN/ginkgo-mainline-bringup-chronicle.md)

# Redmi Note 8 (ginkgo) mainline bring-up chronicle

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125** · serial `<serial>`  
> Purpose: standalone record of all work, pitfalls, and current status for bringing mainline Linux up on ginkgo in this repo.  
> Last updated: 2026-08-19 (**DRM image on screen + kernel fbcon + SPI touch + WCN3990 Wi-Fi + Ubuntu GNOME desktop visible (Adreno 610) + CPUFreq/Resources + Docker CE**; display [full record](./ginkgo-display-complete-2026-08-17.md), boot console [fbcon record](./ginkgo-fbcon-boot-2026-08-18.md), touch [events record](./ginkgo-touch-complete-2026-08-17.md), Wi-Fi [association record](./ginkgo-wifi-complete-2026-08-18.md), desktop software [Ubuntu desktop record](./ginkgo-ubuntu-desktop-2026-08-19.md), GPU [Adreno 610 desktop](./ginkgo-gpu-desktop-2026-08-19.md), stutter and Resources [desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md), containers [Docker bring-up and Tsinghua install](./ginkgo-docker-2026-08-19.md))

---

## 1. Project goals

Run a **mainline Linux kernel** plus a custom rootfs on **Redmi Note 8 (ginkgo)** and achieve:

| Phase | Goal | Status |
|------|------|------|
| P0 | UART boot log | Partial (`ttyMSM0` still deferred) |
| P1 | eMMC-mounted rootfs, systemd boot | **Done** |
| P2 | USB RNDIS + SSH remote debug | **Done** |
| P3 | Panel display (DRM/DSI + backlight) | **Done** (2026-08-17 magenta test pattern user-visible; 2026-08-18 kernel fbcon boot log on panel) |
| P4 | Touch (SPI Novatek NT36672A) | **Done** (2026-08-17 tap produces events) |
| P5 | Wi-Fi (WCN3990 / ath10k_snoc) | **Done** (2026-08-18 associated 5 GHz VHT80, rate 292.5 Mbps) |
| P6 | Full Ubuntu desktop (GNOME / gdm) | **Done** (2026-08-19 Adreno 610; overall better after OSM CPUFreq; Resources temperature/GPU graphs in [desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md)) |
| P7 | Docker (kernel + engine) | **Done** (2026-08-19: `check-config.sh` exit code 0; Tsinghua Docker CE 29.7.2. See [Docker record](./ginkgo-docker-2026-08-19.md)) |

**Current user-visible status:** the system boots; SSH works (USB + **Wi-Fi**); the mainline DRM magenta framebuffer is visible; **about 7s after kernel start, fbcon paints the log**; SPI touch accepts taps; **wlan0 can associate 2.4G/5G**; **Ubuntu 26.04 GNOME desktop is visible (Adreno 610)**; overall better after OSM CPUFreq; Resources temperature/GPU graphs in [desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md); **Docker Engine 29.7.2 is usable** (overlayfs + cgroup v2). Display: [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md). Boot console: [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md). Touch: [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md). Wi-Fi: [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md). Desktop software: [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md). GPU: [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md). Docker: [ginkgo-docker-2026-08-19.md](./ginkgo-docker-2026-08-19.md). Early `dsi_err status=5` analysis is kept as history: [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md).

---

## 2. Hardware and software baseline

### 2.1 Key hardware

| Component | Model / notes |
|-----------|---------------|
| SoC | Qualcomm SM6125 (Kryo 260) |
| Storage | eMMC, `root=/dev/disk/by-partlabel/userdata` |
| Display panel | Tianma NT36672A, 1080×2340, 4-lane DSI |
| Panel driver IC compatible | `tianma,ginkgo-fhd-video` (mainline `panel-novatek-nt36672a.c`) |
| Panel bias | PMI632 **LCDB** (LDO positive + NCP negative), not PMI8998 LAB/IBB |
| Backlight (downstream) | PM6125 PWM + PMI632 GPIO6 enable + DCS brightness |
| Backlight (current mainline) | **KTD3136** @ I2C `0x36` + PMI632 GPIO6 HWEN (no longer gpio-backlight only) |
| Touch | Novatek **NT36672A SPI** @ QUP SE2 (`&spi2`), IRQ GPIO88, RESET GPIO87 |
| Reset GPIO (panel) | TLMM GPIO90, active-low |
| USB debug | Configfs RNDIS gadget, `192.168.7.2` |

### 2.2 Software stack

- Kernel: mainline tree + `config/ginkgo.fragment`
- Board DTS: `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts`
- SoC DTS: `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` (includes several ginkgo-specific fixes)
- PMIC: `pm6125.dtsi`, `pmi632.dtsi`
- Packaging: `scripts/build-kernel.sh` → `scripts/build-bootimg.sh` → `out/boot.img`
- Flash: TWRP recovery + `FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh`
- Root password: `$GINKGO_ROOT_PASSWORD` (set by overlay)

---

## 3. Completed milestones

### 3.1 eMMC / system boot

**Symptom (early):** boot stuck at CQHCI / eMMC init.

**Root causes and fixes:**

| Problem | Root cause | Fix |
|---------|------------|-----|
| eMMC does not probe | Missing `CONFIG_ARM_SMMU` | Enable SMMU in `ginkgo.fragment` |
| SDHCI race | Async probe + clock dependency | Remove `PROBE_PREFER_ASYNCHRONOUS` from `sdhci-msm.c`; add `clk_ignore_unused` to cmdline |
| Wrong SDHCI controller | `sdhc_2` does not match the hardware | In ginkgo DTS: `&sdhc_2 { status = "disabled"; }` |
| Missing interconnect | SM6115 ICC nodes undefined | Add `bimc` / `system_noc` / `config_noc` in `sm6125.dtsi` |
| sync_state deadlock | devlink and uart wait on each other | cmdline: `fw_devlink.sync_state=disabled` |

**Result:** rootfs mounts; systemd can reach multi-user.

### 3.2 USB RNDIS + SSH

**Implementation:**

- Kernel: `CONFIG_USB_CONFIGFS_RNDIS` and related options
- rootfs: `usb-gadget-rndis.service` (Configfs gadget)
- Phone IP: `192.168.7.2`; host recommended `192.168.7.1`

**Host networking note (important):**

Do not make the USB NIC the default route, or you will lose the main network. Recommended:

```bash
# NetworkManager: never use as default gateway
nmcli connection modify ginkgo-usb-tmp \
  ipv4.method manual ipv4.addresses 192.168.7.1/24 \
  ipv4.never-default yes ipv4.route-metric 5000

nmcli connection up ginkgo-usb-tmp

# SSH bound to the source address
ssh -b 192.168.7.1 root@192.168.7.2
```

Or use `scripts/host-usb-connect.sh` (now only adds a `192.168.7.0/24` route; it does not change the default gateway).

**Result:** `ssh root@192.168.7.2` can ping and log in reliably (about 8–40s after USB enumerates).

### 3.3 Serial

- Hardware: USB-TTL on UART (`ttyMSM0` / `uart4` @ `0x4a90000`)
- Capture: `sudo python3 scripts/uart-monitor.py` → logs in `backup/ginkgo/logs/uart-*.log`
- **Leftover:** `4a90000.serial` is still `deferred probe pending` (interconnect + gcc sync_state). SSH is unaffected, but the serial console is incomplete.

### 3.4 Touch (SPI Novatek NT36672A)

**Events working on 2026-08-17.** Full write-up: [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)

- Bus: QUP SE2 `&spi2`, gpio6–9 `qup02`, IRQ 88, RESET 87, 8 MHz, `SPI_MODE_0`
- Driver: downstream `nt36xxx_spi_c3j` ported to `linux/drivers/input/touchscreen/nt36xxx/`
- Firmware: built-in `novatek_ts_tianma_fw.bin`, host-download, PID `591F`
- Decisive fixes: CS active-low (do not trust DT `spi-cs-high`); unify `#undef CONFIG_FB` in the header; `NVT_TRANSFER_LEN=32`
- Acceptance: `NVTCapacitiveTouchScreen`; user confirmed tap produces events

---

## 4. Display subsystem bring-up record

From a black simple-framebuffer placeholder, the stack was moved step by step onto full DRM/MDSS/DSI. Changes, symptoms, and conclusions below are in **time order**.

### 4.1 Stage 1: enable the DRM stack and panel node

**Changes:**

- `config/ginkgo.fragment`: enable `CONFIG_DRM_MSM*`, `CONFIG_DRM_PANEL_NOVATEK_NT36672A`, `CONFIG_DRM_MIPI_DSI`, fbcon, and related options
- `sm6125-xiaomi-ginkgo.dts`: remove `simple-framebuffer` from `chosen`; enable `&mdss`, `&mdss_dsi0`, `&mdss_dsi0_phy`
- Panel: `compatible = "tianma,ginkgo-fhd-video"`; the 1080×2340 init sequence is already in `panel-novatek-nt36672a.c`
- Power placeholders: `panel_vddpos` / `panel_vddneg` (`regulator-fixed` always-on)
- Backlight: `gpio-leds` → later `gpio-backlight` (PMI632 GPIO6)

**Symptom:** panel still black; `/dev/dri/` missing; no driver bound to the panel.

---

### 4.2 Stage 2: panel driver not loaded (`-m` module trap)

**Symptom:** MIPI device `5e94000.dsi.0` exists, but no driver; `modprobe panel-novatek-nt36672a` fails (no modules in the rootfs).

**Root cause:**

1. `CONFIG_BACKLIGHT_CLASS_DEVICE=m` (defconfig) forces `CONFIG_DRM_PANEL_NOVATEK_NT36672A=m`
2. `build-kernel.sh` only merged the fragment when generating `.config` the first time; later builds could lose `=y`

**Fix:**

```kconfig
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
```

`build-kernel.sh` now **merges** `ginkgo.fragment` **on every build**.

**Result:** the panel driver is built into vmlinux, but `/dev/dri` is still missing (components not fully bound).

---

### 4.3 Stage 3: DSI PHY address parsed wrong

**Symptom:** `msm_dsi_phy 0.phy: Couldn't identify PHY index`

**Root cause:** MDSS `#address-cells/#size-cells` in `sm6125.dtsi` had been `<1>`, so child `phy@5e94400` was parsed as address `0`.

**Fix:**

- MDSS `#address-cells = <2>;` `#size-cells = <2>;`
- `mdss_dsi0_phy` compatible changed to `qcom,dsi-phy-14nm-2290`

**Result:** the PHY index error is gone; `msm_dsi_phy` probes normally.

---

### 4.4 Stage 4: pinctrl and reset GPIO

**Symptom A:** `deferred: wait for supplier mdss-te-active-state` (panel never probes)

**Fix:** remove `mdss_te_active` pinctrl from the panel node (TE pin caused a devlink deadlock); keep `mdss_dsi_active` (GPIO90 reset pin config).

**Symptom B:** `failed to get reset gpio from DT`

**Root cause:** `CONFIG_PINCTRL_MSM` was not enabled, so TLMM (`500000.pinctrl`) had no driver and GPIO90 was unavailable.

**Fix:**

```kconfig
CONFIG_PINCTRL_MSM=y
CONFIG_PINCTRL_SM6125=y
```

**Result:** `sm6125-tlmm` loads; `panel-tianma-nt36672a` can bind; `msm_drm` can initialize.

---

### 4.5 Stage 5: LCDB panel bias (latest at that time)

**Symptom:** `msm_drm` is initialized, `/dev/dri/card0` and `/dev/fb0` exist, but:

```
panel-tianma-nt36672a: failed to send DCS Init 1st Code: -22
disp_cc_mdss_pclk0_clk_src: rcg didn't update its configuration
[drm] vblank wait timed out on crtc 0
```

The panel is still fully black; `gpio-backlight` reports `brightness=1` (on/off only, no PWM dimming).

**Root cause (bias):** placeholder `regulator-fixed` cannot drive PMI632 LCDB hardware; mainline originally had **no** LCDB driver.

**Changes in this step:**

1. **New driver** `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c`  
   - Ported from downstream; dropped the `qpnp-revid` dependency  
   - `CONFIG_REGULATOR_QPNP_LCDB=y`

2. **`pmi632.dtsi`** adds `qpnp-lcdb@ec00`:
   - `lcdb_ldo_vreg` (vddpos, 5.4V)
   - `lcdb_ncp_vreg` (vddneg, 5.4V)
   - `lcdb_bst_vreg` (boost)

3. **`sm6125-xiaomi-ginkgo.dts`:**
   - Remove `panel_vddpos` / `panel_vddneg` placeholders
   - `vddpos-supply = <&lcdb_ldo_vreg>`
   - `vddneg-supply = <&lcdb_ncp_vreg>`
   - `refgen-supply = <&refgen_fixed>` (downstream has no MMIO refgen node; use a fixed placeholder)

**After flash (measured in `uart-20260808-031320.log` / SSH):**

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
[drm] Initialized msm 1.13.0
panel-tianma-nt36672a: failed to send DCS Init 1st Code: -22
```

| Check | Result |
|--------|------|
| LCDB probe | Success |
| lcdb_ldo | enabled, 5.5V |
| lcdb_ncp | enabled |
| Panel driver bind | Success |
| DCS init | **Failed -22** |
| Backlight | GPIO enable ON, no PWM; user still sees full black |
| Screen | **Completely black** |

**Conclusion:** the power path (LCDB) works. The bottleneck is now **DSI clocks (disp_cc RCG) + the DCS command path**.

---

### 4.6 Stage 6: DCS / HS / DPMS / backlight (early August 2026)

Over the next few days: DCS order, 14nm `LANE_CTRL` bit24, PHY LDO `0x1c`, clamp, `byte_intf` clock, DPMS unblank, and KTD3136 backlight. At this point: `panel init complete`, `power mode 0x9c`, INTF 60fps, backlight can turn on; the user can still see “backlight on, image fully black”.

See [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md).

---

### 4.7 Stage 7: DSI TPG proves the panel works; FIFO `0xcccc` stalls DPU pixels

Decisive experiment: with the DSI internal TPG on (DPU bypassed), the user sees a checkerboard / full-screen green; `LANE=0x1f00` `FIFO=0x1010`. With TPG off and DPU pixels, FIFO sticks at `0xcccc1019` (VIDEO_MDP overflow and underflow at once).

Conclusion: **the panel / PHY / full-width 1080 are fine**; the problem is INTF → DSI MDP input.

---

### 4.8 Stage 8: image on screen (2026-08-17)

Real root cause and fix:

1. **INTF_1 programmable fetch:** catalog default 24 lines; panel VFP is only 10. The prefetch window landed on DSI BLLP; after the first frame’s VFP, FIFO became `0xcccc`. INTF_1 `prog_fetch_lines_worst_case = 0`.
2. **Kickoff order:** `host_enable` first (20ms HS cycle with INTF off), then `enable_timing(1)`. Resetting DSI while INTF is spraying permanently blows the FIFO.
3. **MDP clock:** use `mode->clock * 1000`; `clk_inefficiency_factor=218` (220 causes `MODE_CLOCK_HIGH` and drops every mode).
4. **HS cycle:** after the 20ms SOFT_RESET, restore the original `CLK_CTRL`; do not leave `DYNAMIC_FORCE_ON`.

Acceptance: `LANE=0x1f00`, `FIFO=0x1010`, INTF 60fps, no runtime `dsi_err`, magenta fb; user confirmed “a bit pink”.

Full story and notes: [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md).

---

## 5. Display-related file list

### 5.1 Kernel config

| File | Role |
|------|------|
| `config/ginkgo.fragment` | All ginkgo kernel option fragments |

Key display-related items:

- `CONFIG_DRM_MSM*`, `CONFIG_DRM_PANEL_NOVATEK_NT36672A=y`
- `CONFIG_BACKLIGHT_CLASS_DEVICE=y`, `CONFIG_BACKLIGHT_GPIO=y`
- `CONFIG_PINCTRL_MSM=y`, `CONFIG_PINCTRL_SM6125=y`
- `CONFIG_REGULATOR_QPNP_LCDB=y`
- `CONFIG_SM_DISPCC_6125=y`, `CONFIG_INTERCONNECT_QCOM_SM6115=y`

### 5.2 Device tree

| File | Display-related changes |
|------|----------------|
| `sm6125-xiaomi-ginkgo.dts` | Panel, backlight, MDSS/DSI enable, LCDB wiring, refgen |
| `sm6125.dtsi` | MDSS address cells, DSI PHY compatible, interconnect |
| `pmi632.dtsi` | LCDB node (added in this work) |

### 5.3 Driver sources

| File | Notes |
|------|------|
| `linux/drivers/gpu/drm/panel/panel-novatek-nt36672a.c` | ginkgo 1080×2340 init sequence |
| `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c` | **Added in this work**, PMI632 LCDB |
| `linux/drivers/regulator/Kconfig` | `REGULATOR_QPNP_LCDB` |
| `linux/drivers/regulator/Makefile` | Build the LCDB driver |

### 5.4 Scripts

| Script | Notes |
|------|------|
| `scripts/build-kernel.sh` | Merge the fragment on every build |
| `scripts/build-bootimg.sh` | Pack boot.img |
| `scripts/flash-linux-boot.sh` | Flash boot via recovery adb |
| `scripts/host-usb-connect.sh` | USB to the phone without stealing the default route |
| `scripts/uart-monitor.py` | UART log capture |

---

## 6. Build and flash flow

```bash
cd .

# Build kernel + DTB
./scripts/build-kernel.sh

# Pack boot.img
./scripts/build-bootimg.sh

# Phone in TWRP recovery, USB connected:
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

Use `FLASH_ROOTFS=0` for kernel-only updates; drop the variable or set `FLASH_ROOTFS=1` when overlay changes are needed.

---

## 7. Debug command cheat sheet

### 7.1 SSH (do not steal the default route)

```bash
nmcli connection up ginkgo-usb-tmp   # must be preconfigured never-default
ssh -b 192.168.7.1 root@192.168.7.2
```

### 7.2 Display path

```bash
# Drivers and device nodes
ls -la /dev/dri/card0 /dev/fb0
readlink /sys/bus/mipi-dsi/devices/5e94000.dsi.0/driver
readlink /sys/bus/platform/devices/500000.pinctrl/driver

# _regulator_
grep -l lcdb /sys/class/regulator/regulator.*/name | while read f; do
  d=$(dirname "$f"); echo "$(cat $f): $(cat $d/state)"
done

# Kernel log
dmesg | grep -iE 'LCDB|panel|DCS|msm_drm|disp_cc|dsi|vblank|deferred'
```

### 7.3 Serial

```bash
sudo python3 scripts/uart-monitor.py
# Logs: backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

---

## 8. Symptom → root cause cheat sheet

| Log / symptom | Root cause | Fix status |
|-----------|------|----------|
| eMMC / CQHCI hang | clk race, missing SMMU | Fixed |
| USB extcon deferred | Missing interconnect | Fixed (drop extcon + ICC) |
| `msm_dsi_phy 0.phy` | MDSS `#address-cells=1` | Fixed |
| No `/dev/dri`, no panel driver | Panel driver `=m` not loaded | Fixed |
| `wait for supplier mdss-te-active-state` | TE pinctrl devlink | Fixed (removed TE pinctrl) |
| `failed to get reset gpio` | Missing `PINCTRL_MSM` | Fixed |
| `failed to send DCS Init -22` | DSI clock/link not ready | **Fixed** (host order + clocks) |
| `disp_cc pclk0 rcg didn't update` | disp_cc clock config | **Fixed** (pclk=183 MHz at first image) |
| vblank timeout | Panel not producing a frame | **Fixed** (INTF 60fps) |
| Backlight on, image fully black | INTF PROG_FETCH × DSI BLLP → FIFO `0xcccc` | **Fixed** (INTF_1 prefetch=0 + kickoff order) |
| No backlight at all | KTD3136 not driven | **Fixed** (`ktd3136.c` @ I2C 0x36) |
| `ttyMSM0` deferred | uart interconnect | Not fixed (low priority) |
| SSH intermittent | USB enumerate timing / NM config | Partially mitigated (run `ginkgo-usb-ssh` after every reboot) |

---

## 9. Downstream vs mainline display power

| Downstream (`dsi_panel_pwr_supply`) | Mainline (`panel-novatek-nt36672a`) | Current wiring |
|------------------------------|----------------------------------|----------|
| `vddio` → L9A 1.8V | `vddio-supply` | `vreg_l9a` ✓ |
| `lab` → `lcdb_ldo` | `vddpos-supply` | `lcdb_ldo_vreg` ✓ |
| `ibb` → `lcdb_ncp` | `vddneg-supply` | `lcdb_ncp_vreg` ✓ |
| `vdda` 1.2V | DSI/PHY `vdda-supply` | `vreg_l18a` ✓ |
| `refgen` (internal supply) | `refgen-supply` | `refgen_fixed` placeholder |
| PWM + GPIO backlight | `backlight` | **KTD3136** (I2C) + GPIO6 HWEN ✓ |
| DCS brightness | Panel driver on_cmds | Usable after the link is up; brightness is now from KTD3136 |

---

## 10. Next steps (display + fbcon + touch + Wi-Fi + GNOME already working)

Display P3, kernel fbcon, touch P4, Wi-Fi P5, and Ubuntu GNOME (Adreno 610) are done. The desktop is visible but a bit stuttery; see [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md). Remaining work, by priority:

### 10.0 Desktop smoothness (non-blocking)

- After idle, confirm whether gnome-shell CPU drops as the shader cache fills
- GPU OPP / interconnect; a lighter session if needed (still native GPU; do not fall back to swrast)

### 10.1 Other peripherals

- Bluetooth (same WCN3990; depends on MPSS / firmware that is already up; not separately accepted yet)
- Audio, camera, and similar

### 10.2 Display leftovers (non-blocking)

- Encoder vsync IRQ ~11 Hz vs INTF hardware 60fps (does not affect the visible image)
- Downstream PWM backlight not replicated (KTD3136 is enough)
- GPIO89 TE: reassess only if a devlink deadlock can be avoided
- Persist `fastboot boot` into the boot partition (**only if the user explicitly asks**)

### 10.3 Real refgen (low priority)

- Downstream has no standalone DT node; the current fixed placeholder can stay

---

## 11. Log archive

| Log | Summary |
|------|----------|
| `uart-20260808-001517.log` | Early eMMC/boot |
| `uart-20260808-023855.log` | After DSI PHY fix; no panel driver |
| `uart-20260808-024802.log` | reset GPIO failed (no PINCTRL_MSM) |
| `uart-20260808-025327.log` | After PINCTRL fix, msm_drm init, DCS -22 |
| `uart-20260808-031320.log` | **After LCDB success**, DCS -22 + disp_cc RCG warning |
| `uart-20260808-225704.log` | build #45: panel init OK, `dsi_err status=5`, IOMMU @ 0x5c003000 |

---

## 12. Version timeline

| Time (approx.) | Kernel change | Display progress |
|------------|----------|----------|
| 2026-08-08 00:15 | DRM stack first enabled | No dri |
| 02:38 | PHY address-cells fix | DSI PHY OK |
| 02:43 | Panel built-in (BACKLIGHT_CLASS) | Still deferred |
| 02:49 | Remove TE pinctrl | reset GPIO still fails |
| 02:52 | PINCTRL_MSM enabled | msm_drm init, DCS -22 |
| 03:12 | LCDB driver + real bias | LCDB OK, DCS still -22, panel fully black |
| 22:51 | build #44 bridge order | DCS OK, `dsi_err status=5` |
| 22:57 | build #45 PHY timing + 0.9V | Still `status=5`; flash then black with backlight |
| Early–mid August 2026 | HS / clamp / LDO / DPMS / KTD3136 | Backlight on; DSI TPG shows checkerboard/green |
| **2026-08-17** | Disable INTF_1 prefetch + DSI then INTF + MDP 400 MHz | **Magenta on screen, P3 done** |
| **2026-08-17 evening** | NT36672A SPI: MODE_0 + CONFIG_FB struct alignment + 32B FIFO download | **Touch events, P4 done** |
| **2026-08-18** | WCN3990: 1MB MSA, empty CAL protocol, delayed BSS-peer vdev-start, data MCS cache | **Wi-Fi associates 5 GHz VHT80, P5 done** |
| **2026-08-18 evening** | Add `console=tty0` to boot.img / DTS cmdline (fbcon) | **Kernel boot log on panel** |
| **2026-08-19 early morning** | TUNA mirror + `ubuntu-desktop` + finish gdm3 config | **GNOME session up; screen still black (no GPU)** |

---

## 13. Summary

This project has brought **Redmi Note 8** to **mainline Linux boots, USB/Wi-Fi SSH, real DRM output on screen, kernel fbcon boot log on the panel, SPI touch accepts taps, Wi-Fi associates, Ubuntu GNOME desktop visible on Adreno 610, OSM CPUFreq / Resources, and Docker CE**.

- **Software stack:** DRM/MDSS/DPU/DSI/PHY/panel/LCDB/KTD3136 all work; fbcon + `console=tty0` puts the kernel log on the panel; NT36672A SPI touch + Tianma firmware host-download works; WCN3990 `ath10k_snoc` + on-device `WLAN.HL.3.0.2` works
- **Hardware output:** `LANE=0x1f00`, `FIFO=0x1010`, INTF 60fps; user confirmed the magenta test pattern; penguin + kernel log about 7s after boot; `NVTCapacitiveTouchScreen` tap produces events; `<test-ap-5g>` VHT80 association at 292.5 Mbps
- **Display key:** INTF programmable fetch conflicts with DSI BLLP; do not 20ms-reset DSI while INTF is running
- **Boot console key:** `CONFIG_FRAMEBUFFER_CONSOLE` is not enough; boot.img cmdline must include `console=tty0`; do not fall back to simplefb
- **Touch key:** CS must be `SPI_MODE_0`; unify `#undef CONFIG_FB` in `nt36xxx.h`; `NVT_TRANSFER_LEN=32` when FIFO-only
- **Wi-Fi key:** MSA 1MB and must hyp_assign; empty CAL answers `CAL_DOWNLOAD` then `CAL_REPORT`; STA creates the BSS peer before vdev-start (not self-peer); rate cache uses the last data MCS
- **Desktop key:** TUNA `ubuntu-ports`; `gdm3` must be fully configured (`gdm` user + PAM); if the screen is black, check Mesa/`msm_dri` first — do not treat it as a DSI regression. GNOME is now visible on Adreno 610; after OSM CPUFreq the desktop is generally better; Resources temperature/GPU graphs are in [desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md)
- **Docker:** `check-config.sh` exit 0; Tsinghua Docker CE 29.7.2; overlayfs + cgroup v2

Next priority is desktop smoothness (non-blocking) and remaining peripherals (Bluetooth on the same WCN3990, audio, camera). No new display or GPU hardware work is required for the current visible stack.

Full display story: [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md).  
Full boot fbcon story: [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md).  
Full touch story: [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md).  
Full Wi-Fi story: [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md).  
Ubuntu desktop story: [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md).  
GPU desktop: [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md).  
Docker: [ginkgo-docker-2026-08-19.md](./ginkgo-docker-2026-08-19.md).

---

*This document is a standalone full record. Append later milestones at the end, or update sections 4, 10, and 12.*
