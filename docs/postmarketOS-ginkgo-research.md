**Language:** English | [简体中文](zh-CN/postmarketOS-ginkgo-research.md)

# postmarketOS porting research: Redmi Note 8 (ginkgo)

> Research dates: 2026-08-04 (first version), 2026-08-04 (mainline + Ubuntu focus)  
> Target device: Xiaomi Redmi Note 8 (codename: `ginkgo`)  
> Project: `xiaomi-ginkgo-mainline`  
> **Current goal:** mainline Linux → boot Ubuntu 26.04 LTS → usable touch + Wi-Fi

---

## 1. Executive summary

| Dimension | Status |
|------|------|
| **Mainline kernel** | Basic support exists; it can boot, but functionality is limited |
| **Official postmarketOS package** | **No** `device-xiaomi-ginkgo` yet; not in community/main |
| **Closest reference** | Sister device **willow (Redmi Note 8T)** has a full downstream port in pmaports `testing` |
| **Full-port difficulty** | **Medium–high** — same SoC already has community work, but display/touch/Wi-Fi/audio/modem still need a lot of work |
| **Current advantages** | Bootloader unlocked, root available, already running a custom `4.14.117-perf+` kernel |

**One-line summary:** a port can start now, but a “complete” port should be phased — reuse willow/downstream to get boot working first, then move to mainline and fill in subsystems.

> **Mainline-focused document:** see [mainline-ginkgo-porting-guide.md](./mainline-ginkgo-porting-guide.md) (hardware inventory, DTS gaps, touch SPI, Wi-Fi firmware, phased plan)

---

## 2. Target device hardware profile

The following was confirmed with adb and dmesg (device serial `<serial>`).

### 2.1 Basics

| Item | Value |
|------|-----|
| Model | Redmi Note 8 |
| Codename | `ginkgo` |
| SoC | Qualcomm SM6125 / trinket (Snapdragon 665) |
| CPU | 6-core Kryo 260 (4×A73 + 2×A53 effective cores) |
| GPU | Adreno 610 |
| Memory | ~5.5 GB (kernel limit about 4044 MB) |
| Storage | eMMC (`4744000.sdhci`), not UFS |
| Screen | 1080 × 2340, 420 dpi |
| Bootloader | Unlocked (`verifiedbootstate=orange`) |
| Current ROM | LineageOS 17.1 (`17.1-20220214-NIGHTLY-ginkgo`) |
| Current kernel | `4.14.117-perf+` (self-built 2026-06-01) |

### 2.2 Key hardware

| Component | Measured config | Notes |
|------|----------|------|
| Display panel | **NT36672A + Tianma** | cmdline: `msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display` |
| Touch | **Novatek NT36672A** | Same family as willow; downstream driver `ts_nt36xxx` |
| Fingerprint | **Goodix** | `androidboot.fpsensor=gdx` |
| Wi-Fi | **WCN3990 / ICNSS** | `qcom,icnss`; needs firmware + remoteproc |
| Audio codec | **max98927** | I2C audio codec |
| Audio amp | **tas2562** | I2C amp |
| PMIC | **PM6125 + PMI632** | Power management |
| Baseband | Qualcomm MPSS | Depends on modem firmware; hard to port |

### 2.3 SKU differences (watch this)

Redmi Note 8 has multiple hardware lots; components can differ between units:

| Component | Possible variants |
|------|----------------|
| Main camera | Samsung GM1 / GM2, or Sony IMX582 |
| Secondary cameras | GC02M1, OV13855, S5K4H7, and others |
| Display panel | Tianma NT36672A, or Huaxing FT8719, and others |
| Fingerprint | Goodix, or FPC1020 (other lots) |

A complete port needs multi-variant DT overlays or runtime detection.

---

## 3. postmarketOS ecosystem status

### 3.1 Related device packages in pmaports

Research based on pmaports upstream master (cloned 2026-08-04).

| Package | Location | Status | Notes |
|------|------|------|------|
| `device-xiaomi-ginkgo` | — | **Does not exist** | Not upstream yet; must be created |
| `device-xiaomi-willow` | `device/testing/` | Exists | Redmi Note 8T, **best reference** |
| `device-xiaomi-laurel` | `device/testing/` | Exists | Mi A3, same SM6125, uses a mainline kernel |
| `linux-postmarketos-qcom-sm6125` | `device/testing/` | Exists | SM6125 mainline 6.1 fork |
| `linux-xiaomi-willow` | `device/testing/` | Exists | Downstream kernel 4.14.117 |
| `firmware-xiaomi-willow` | `device/testing/` | Exists | Novatek touch firmware |
| `device-motorola-def` | `device/testing/` | Exists | Same trinket platform, downstream |

### 3.2 Official wiki

Device pages already exist (open in a browser):

- Device page: https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo)
- SoC page: https://wiki.postmarketos.org/wiki/Qualcomm_Snapdragon_665_(SM6125)

The wiki uses Anubis anti-bot protection; a browser + JavaScript is required.

### 3.3 Sister device willow

Redmi Note 8T (`willow`) shares `sm6125-xiaomi-ginkgo-common.dtsi` with ginkgo. The main difference is that willow has NFC.

willow’s pmOS strategy (`device/testing/device-xiaomi-willow`):

```
Downstream kernel 4.14.117 (linux-xiaomi-willow)
  ├── Source: rjeli/lineage_kernel_sm6125
  ├── Touch: ts_nt36xxx kernel module + novatek firmware
  ├── Display: Weston DRM backend + weston-fixes.sh
  ├── Screen: 1080×2340 (same as ginkgo)
  └── deviceinfo_no_framebuffer="true"
```

**Implication for ginkgo:** the fastest path is to fork the willow package and change the codename/DTB/defconfig, not to write a mainline port from scratch.

---

## 4. Mainline Linux progress

Mainline ginkgo support is actively driven by the postmarketOS community; patch CC lists include `~postmarketos/upstreaming`.

### 4.1 Key patch timeline

| Time | Author | Content |
|------|------|------|
| 2025-03 | Gabriel Gonzales (semfault) | Initial DTS: `sm6125-xiaomi-ginkgo.dts` |
| 2026-01 | Barnabás Czémán (barni2000) | Shared `ginkgo-common.dtsi` with willow; reserved memory, GPIO, framebuffer fixes |
| 2026-01 | Biswapriyo Nath | Volume Up key fix; enable RTC, Debug UART |
| 2026-03~07 | Biswapriyo Nath | Vibrator, IR emitter, USB-C OTG (Applied to mainline) |

### 4.2 What mainline already supports

| Feature | Status | Notes |
|------|------|------|
| Basic boot | ✅ | Can reach a shell |
| Simple framebuffer | ✅ | Inherits the bootloader-preconfigured frame |
| USB | ✅ | Basic USB |
| eMMC | ✅ | Internal storage |
| SD card | ✅ | External microSD |
| PMIC/RPM regulators | ✅ | Power rails |
| Power/volume keys | ✅ | gpio-keys |
| Debug UART | ✅ | ttyMSM0 @ 115200 |
| RTC | ✅ | pm6125 RTC |
| Vibrator | ✅ | PMI632 vibrator (new patch) |
| IR emit | ✅ | IR SPI LED (new patch) |
| USB-C OTG | ✅ | Type-C port (new patch) |

### 4.3 What mainline still lacks

| Feature | Status | Main difficulty |
|------|------|----------|
| DRM/DSI display | ❌ | simplefb only; no MSM DRM panel node; Tianma panel needs its own port |
| Touch | ❌ | No mainline NT36672A driver; willow uses downstream `ts_nt36xxx` + firmware |
| Wi-Fi | ❌ | ICNSS + WCN3990 needs firmware, remoteproc, and the driver chain |
| Bluetooth | ❌ | Depends on WCN3990 firmware |
| Audio | ❌ | max98927/tas2562 + QDSP/ADSP; no SM6125-specific ASoC driver |
| Modem | ❌ | IPA + MPSS firmware; very high complexity |
| Camera | ❌ | Multi-SKU sensors + CSIPHY/ISP |
| GPU acceleration | ❌ | No usable mainline 3D driver for Adreno 610 |
| Fingerprint | ❌ | No mainline Goodix support |
| Sensors | ❌ | Accel/gyro/proximity need IIO drivers |

**Kernel config note:** `linux-postmarketos-qcom-sm6125` enables `CONFIG_DRM_MSM` but also sets `CONFIG_DRM_NOMODESET=y`, and it lacks SM6125-specific sound drivers.

---

## 5. Full-port gap analysis

### 5.1 Phased roadmap

```
Phase 0 (done)      Bootloader unlocked + custom kernel + basic mainline DTS
       ↓
Phase 1 (1-2 weeks) device-xiaomi-ginkgo package + boot.img + first flash
       ↓
Phase 2 (2-4 weeks) Display (DRM/DSI) + touch (NT36672A) + basic UI
       ↓
Phase 3 (1-3 months) Wi-Fi + audio + Bluetooth + battery
       ↓
Phase 4 (3-6 months+) Modem voice / mobile data
       ↓
Phase 5 (6 months+) Camera + sensors + fingerprint
```

### 5.2 Effort estimate by phase

| Phase | Goal | Estimated effort | Main dependencies |
|------|------|------------|----------|
| P0 | Fastboot flash pmOS; serial/shell login | 1–2 weeks | pmaports package, boot config |
| P1 | Display + touch + basic desktop (Weston/Phosh) | 2–4 weeks | Downstream kernel or mainline DRM |
| P2 | Wi-Fi + audio + Bluetooth | 1–3 months | Firmware package, ICNSS driver |
| P3 | Modem voice/data | 3–6 months+ | IPA, modem firmware, Ofono |
| P4 | Camera / sensors / fingerprint | 6 months+ | Multi-SKU handling, closed firmware |

---

## 6. Recommended technical paths

### Path A: downstream first (recommended start)

1. Fork `device-xiaomi-willow` → `device-xiaomi-ginkgo`
2. Fork `linux-xiaomi-willow` → `linux-xiaomi-ginkgo` (or reuse the existing `4.14.117-perf+` tree)
3. Change DTB/defconfig to ginkgo
4. Reuse willow touch firmware and the `ts_nt36xxx` module
5. Build with pmbootstrap and flash via fastboot

| Pros | Cons |
|------|------|
| Fastest path to a desktop | Long-term maintenance of a 4.14 downstream kernel |
| Matches existing kernel experience | Not fully aligned with the pmOS mainline direction |

### Path B: mainline first (long-term goal)

1. Base on `linux-postmarketos-qcom-sm6125` + latest mainline (including the ginkgo DTS)
2. Create `device-xiaomi-ginkgo`, following `device-xiaomi-laurel`
3. Upstream step by step: DSI panel → touch → Wi-Fi → audio
4. Submit patches to the `~postmarketos/upstreaming` list

| Pros | Cons |
|------|------|
| Matches the long-term pmOS direction | Short-term: serial console + simplefb only |
| Easier to merge upstream | Poor experience; not for daily use |

### Path C: hybrid (recommended for this project)

```
Phase 1–2:  Downstream kernel: boot + display + touch
Phase 3+:   Move each subsystem to a mainline driver
End goal:   All mainline, or “mainline kernel + a few downstream modules”
```

---

## 7. Partitions and flashing

### 7.1 Partition layout (measured)

The device has no A/B slots (`ro.boot.slot_suffix` is empty). Main partitions:

```
boot, bootbak, recovery, system, vendor, userdata, cache,
vbmeta, vbmetabak, dtbo, dtbobak, persist, ...
```

### 7.2 Flash notes

- **Method:** fastboot
- **vbmeta:** needs `--disable-verity --disable-verification`
- **Boot image format:** standard Qualcomm boot.img (pagesize=4096)
- **DTB:** needs append_dtb (`deviceinfo_append_dtb="true"`)
- **USB ID:** Vendor `0x2717` (Xiaomi)

### 7.3 Kernel cmdline reference (current LineageOS)

```
console=ttyMSM0,115200n8
androidboot.console=ttyMSM0
earlycon=msm_serial_dm,0x4a90000
androidboot.hardware=qcom
androidboot.bootdevice=4744000.sdhci
androidboot.usbcontroller=4e00000.dwc3
msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display
androidboot.fpsensor=gdx
```

---

## 8. Key resource links

| Resource | URL |
|------|-----|
| pmaports repo | https://gitlab.com/postmarketOS/pmaports |
| SM6125 mainline kernel fork | https://gitlab.com/sm6125-mainline/linux |
| ginkgo mainline DTS (common) | `arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo-common.dtsi` |
| ginkgo mainline DTS (device) | `arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| willow pmOS device package | `pmaports/device/testing/device-xiaomi-willow/` |
| willow downstream kernel package | `pmaports/device/testing/linux-xiaomi-willow/` |
| SM6125 mainline kernel package | `pmaports/device/testing/linux-postmarketos-qcom-sm6125/` |
| Initial ginkgo patch | Gabriel Gonzales, LKML 2025-03 |
| Community mailing list | linux-arm-msm@vger.kernel.org |
| pmOS upstream list | ~postmarketos/upstreaming@lists.sr.ht |
| Android reference DT | Lineage `android_kernel_xiaomi_sm6125` |
| Android device tree | `device_xiaomi_ginkgo` (various ROM projects) |
| Touch/firmware source | `Xiaomi-trinket-dev/vendor_xiaomi_sm6125-common` |
| postmarketOS Wiki (device) | https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo) |
| postmarketOS Wiki (SoC) | https://wiki.postmarketos.org/wiki/Qualcomm_Snapdragon_665_(SM6125) |

---

## 9. Main contributors and community

| Person | Contribution |
|------|------|
| Gabriel Gonzales (semfault) | Initial mainline ginkgo DTS |
| Barnabás Czémán (barni2000) | Shared ginkgo/willow dtsi; memory/GPIO fixes; pmOS contributor |
| Biswapriyo Nath | Vibrator / IR / USB-C / RTC / UART patches |
| Eli Riggs (rjeli) | willow downstream pmOS port |
| Konrad Dybcio | Qualcomm mainline maintainer |
| Lux Aliaga | laurel (Mi A3) pmOS port; sm6125 kernel package |

---

## 10. Risks and notes

1. **Memory layout:** early mainline reserved-memory definitions were wrong and can crash under load. Verify with the `memtest=1` kernel parameter.

2. **Panel variants:** this unit is Tianma NT36672A; other lots may be Huaxing FT8719 and similar. Prepare multiple DT overlays.

3. **Camera SKU:** main camera mixes GM1/GM2/IMX582; branch on hardware revision.

4. **vbmeta verification:** flashing pmOS usually requires disabling verity/verification, or a custom kernel will not boot.

5. **No A/B slot:** a failed flash can recover via fastboot/edl, but treat the boot partition carefully.

6. **Modem:** full cellular support is the hardest piece; many pmOS devices go a long time without modem support.

7. **Closed firmware:** Wi-Fi/BT/camera/modem all depend on Qualcomm/Xiaomi proprietary firmware, extracted from Android or a vendor tree.

---

## 11. Suggested next actions

1. **Set up pmbootstrap** and clone pmaports locally
2. **Fork the willow device package** and create a first `device-xiaomi-ginkgo`
3. **Hook up the existing 4.14.117 tree**, or try `linux-xiaomi-willow` first to prove boot
4. **Extract touch firmware** (`novatek_ts_tianma_fw.bin`) and test the `ts_nt36xxx` module
5. **Register/update the wiki** ginkgo feature table and stay aligned with upstream
6. **Track mainline in parallel** and send display/touch patches upstream

---

## 12. Focused research: mainline Linux + Ubuntu 26.04 + touch + Wi-Fi

> This section estimates difficulty against the updated project goal.  
> Note: “Ubuntu 26.4” is read as **Ubuntu 26.04 LTS (Resolute Raccoon)**, which ships **Linux 7.0**.

### 12.1 Goal breakdown

| Sub-goal | Meaning | Existing baseline? |
|--------|------|-------------|
| **Mainline Linux boot** | Linux 7.0 + `sm6125-xiaomi-ginkgo.dtb` starts | ✅ Yes (simplefb + USB + eMMC) |
| **Ubuntu 26.04 runs** | arm64 rootfs + systemd + desktop/network management | ⚠️ No official phone image; must package it yourself |
| **Touch usable** | Finger operates the UI (Wayland/X11 input device) | ❌ Mainline ginkgo DTS has no touch node |
| **Wi-Fi usable** | Join a wireless network | ❌ SM6125 mainline SoC DTS still has no Wi-Fi node |

### 12.2 Overall difficulty

| Combined rating | **High (7.5 / 10)** |
|----------|------------------------|
| Optimistic | A skilled developer, **full-time 3–4 months**, can reach “Ubuntu + display + touch + Wi-Fi basically usable” |
| Conservative | **6–9 months** (including upstream wait, firmware debug, panel timing) |
| Largest risk | **Wi-Fi** — SM6125 mainline Wi-Fi infrastructure is not complete |

**Key conclusion:** touch and Wi-Fi cannot be done in isolation. A usable Ubuntu desktop also needs **DRM display** first. The real dependency chain is:

```
Mainline boot → DRM display → touch input → Wi-Fi → Ubuntu desktop experience
                ↑                      ↑
           Current largest gap     Platform-level missing
```

### 12.3 Per-item difficulty

#### A. Mainline Linux boot + Ubuntu 26.04 — difficulty: ★★★☆☆ (medium)

**Status:**
- ginkgo already has `sm6125-xiaomi-ginkgo.dts` in mainline and can boot to a serial console
- Ubuntu 26.04 LTS puts arm64 in the main archive and ships Linux 7.0
- Official Ubuntu arm64 targets servers / Snapdragon laptops / edge devices and **does not** ship a Redmi Note 8 image

**What to do:**
1. Build/package Linux 7.0 + ginkgo DTB (or patch Ubuntu kernel sources)
2. Build an arm64 rootfs (`debootstrap` / `mmdebstrap` + Ubuntu 26.04 archives)
3. Make boot.img (kernel + initramfs) and flash userdata/root
4. Handle vbmeta disable, fstab, and partition mounts

**Estimate:** 2–4 weeks (with embedded Linux experience)

**Risk:** low–medium. Boot itself is not the hard part; Ubuntu desktop can run on simplefb but the experience is poor (no GPU acceleration).

---

#### B. DRM display (prerequisite for touch and desktop) — difficulty: ★★★★☆ (high)

**This device:** Tianma **NT36672A** 1080×2340 DSI panel (confirmed in cmdline)

**Mainline status:**

| Item | Status |
|------|------|
| SM6125 MDSS/DSI controller | ✅ `sm6125.dtsi` already has `mdss_dsi0` |
| Novatek NT36672A panel driver | ⚠️ `panel-novatek-nt36672a.c` exists, but only **1080×2246** (Poco F1 Tianma), **not** ginkgo’s 1080×2340 |
| MDSS/panel in ginkgo DTS | ❌ `ginkgo-common.dtsi` has only simple-framebuffer; no `&mdss_dsi0` |
| Reference ports | laurel (Samsung S6E8FC0), seine (Samsung SOFEF01) already have full MDSS config |

**What to do:**
1. Extract ginkgo panel power sequence, GPIO, pinctrl, and regulators from downstream DTS / kernel
2. Add a new panel compatible for 1080×2340, or extend `panel-novatek-nt36672a` timing/init
3. Enable `&mdss_dsi0`, `&mdss_dsi0_phy`, and the panel child in `sm6125-xiaomi-ginkgo-common.dtsi`
4. Verify DRM/KMS output (`modetest`, Weston/GNOME)

**Estimate:** 4–8 weeks (including upstream patch rounds)

**Risk:** medium–high. A wrong panel init sequence causes snow or a dark panel; different lots (Tianma vs Huaxing) may need multiple DT overlays.

---

#### C. Touch — difficulty: ★★★★☆ (high)

**This device:** Novatek **NT36672A** touch (same IC family as the panel; independent I2C communication)

**Mainline status:**

| Item | Status |
|------|------|
| Mainline driver `novatek-nvt-ts` | ✅ Exists; supports `novatek,nt36672a-ts` and variants |
| ginkgo DTS touch node | ❌ Not added |
| willow pmOS approach | Uses **downstream** `ts_nt36xxx` + `novatek_ts_tianma_fw.bin`; **not mainline** |
| laurel mainline reference | Uses `focaltech,ft3518` (completely different hardware) |

**What to do:**
1. From downstream kernel/DTS, determine I2C bus, address (downstream FW address `0x01`), interrupt GPIO, reset GPIO
2. Add a touchscreen node to the ginkgo DTS (`novatek,nt36672a-ts` or a variant)
3. Confirm whether touch firmware is required (downstream has a bin; mainline `novatek-nvt-ts` usually does not)
4. Debug wake_type / chip_id variants (NT36672A has e7t and others; a mismatch fails probe with -5)
5. On Ubuntu, verify `/dev/input/event*` + libinput

**Estimate:** 3–6 weeks (can overlap display, but needs the panel lit first)

**Risk:** medium–high. Downstream `ts_nt36xxx` and mainline `novatek-nvt-ts` are different stacks; expect several iterations of compatible / GPIO / power supply.

---

#### D. Wi-Fi — difficulty: ★★★★★ (very high)

**This device:** Qualcomm **WCN3990** (`qcom,icnss` / `ath10k_snoc`)

**Mainline status (key finding):**

| Item | Status |
|------|------|
| Wi-Fi node in `sm6125.dtsi` | ❌ **Linux 7.0 still has no `wifi` node** (`sm6115.dtsi` has one; sm6125 was not ported) |
| ginkgo / laurel / seine DTS | ❌ None have `&wifi { status = "okay"; }` |
| ath10k SNOC driver | ✅ Mainline has `ath10k_snoc` + `wcn36xx` |
| Public SM6125 Wi-Fi success | ❌ **No public success story** |
| References | sm6115 j606f and fxtec-pro1x have Wi-Fi enabled; qcm2290 has a `firmware-name` override |

**What to do (large amount of work):**
1. **SoC level:** add `wifi@c800000` to `sm6125.dtsi` (from sm6115: memory-region, interrupts, iommus)
2. **Device level:** add `&wifi` on ginkgo (4 regulators + `qcom,calibration-variant`)
3. **Firmware:** extract from the phone `/vendor/firmware_mnt/image/`:
   - `wlanmdsp.mbn`
   - `bdwlan.bin` → convert to `board-2.bin`
   - `firmware-5.bin` (must match the wlanmdsp version)
4. Install under `/lib/firmware/ath10k/WCN3990/hw1.0/`
5. Debug ath10k probe, calibration-variant naming, and a possible WCN3990 A-MSDU fragmentation bug (still being fixed on mainline in 2026; throughput may be poor)
6. Submit upstream patches and wait for merge

**Estimate:** 2–4 months (including upstreaming the sm6125 SoC Wi-Fi node)

**Risk:** very high. This is the largest technical risk in the project. Even if the driver loads, throughput and stability may need extra patches (for example the ath10k in-order rx amsdu persistence patch, still RFC in July–August 2026).

### 12.4 Difficulty matrix

| Subsystem | Mainline readiness | Development effort | Technical risk | Combined difficulty |
|--------|-----------|-----------|---------|---------|
| Boot + serial | 90% | Low | Low | ★★☆☆☆ |
| Ubuntu rootfs | N/A (packaging) | Medium | Low | ★★★☆☆ |
| DRM display | 40% | High | Medium–high | ★★★★☆ |
| Touch | 30% | High | Medium–high | ★★★★☆ |
| Wi-Fi | **10%** | **Very high** | **Very high** | ★★★★★ |

### 12.5 Recommended implementation path (mainline-only)

Unlike the earlier “downstream first” path, the goal here is explicitly **mainline Linux**:

```
Phase 1 (2–4 weeks)  Mainline boot + Ubuntu minimal rootfs + serial login
Phase 2 (4–8 weeks)  DRM display (NT36672A 1080×2340 panel driver + ginkgo DTS)
Phase 3 (3–6 weeks)  Touch (novatek-nvt-ts + ginkgo DTS; partly in parallel with phase 2)
Phase 4 (2–4 months) Wi-Fi (sm6125.dtsi wifi node upstream + firmware + ginkgo calibration)
Phase 5 (optional)   Ubuntu Desktop (GNOME/Wayland) integration
```

**Phase 1 already proves “Ubuntu can boot”**, but the user experience is serial-only.  
**Phases 2+3 reach “touch desktop usable”.**  
**Phase 4 completes the full goal.**

### 12.6 Gap vs reference devices

| Device | SoC | Mainline display | Mainline touch | Mainline Wi-Fi | Notes |
|------|-----|---------|---------|----------|------|
| **ginkgo (yours)** | SM6125 | ❌ | ❌ | ❌ | simplefb only |
| laurel (Mi A3) | SM6125 | ✅ in progress | ✅ ft3518 | ❌ | Different panel/touch than ginkgo |
| seine (Xperia 10 II) | SM6125 | ✅ | Partial | ❌ | Different panel/touch |
| j606f (Lenovo P11) | SM6115 | — | — | ✅ | Wi-Fi reference; same PM6125 |
| willow (8T) pmOS | SM6125 | ✅ downstream | ✅ downstream | ❌ | Not a mainline approach |

ginkgo Wi-Fi/touch/display cannot be copied from laurel or willow; each must be ported to ginkgo hardware.

### 12.7 Ubuntu 26.04 notes

- Ubuntu 26.04 LTS default kernel is **Linux 7.0**, roughly in sync with ginkgo mainline DTS progress
- arm64 is in the main archive, but there is **no** official SM6125 phone support
- Snapdragon X Elite laptops running Ubuntu 26.04 still hit firmware extraction and GPU regressions (Phoronix 2026 review); a phone platform is only harder
- Practical path: a self-maintained kernel package + custom rootfs, not waiting for Canonical official support
- Consider proving drivers on **postmarketOS** or a minimal Ubuntu rootfs first, then move to full Ubuntu Desktop

### 12.8 Final judgment

| Question | Answer |
|------|------|
| Is the goal feasible? | **Yes**, but it is a long project, not a short hack |
| Largest bottleneck? | **Wi-Fi** (missing SM6125 SoC mainline Wi-Fi node + firmware + no precedent) |
| Can touch be done first, alone? | Yes, but without display you can only verify input events on serial; a full experience needs DRM |
| Harder than a downstream postmarketOS port? | **Significantly harder.** willow downstream can show a desktop in 1–2 months; mainline is ×2–3 |
| Should we stay on mainline? | If the goal is long-term maintenance + upstream contribution → **worth it**; if the goal is a quick result → downstream is faster |

---

## Appendix A: mainline ginkgo DTS structure

Current mainline device-tree files:

```
sm6125-xiaomi-ginkgo.dts          # Device entry (12 lines, includes common)
sm6125-xiaomi-ginkgo-common.dtsi  # Shared part (ginkgo + willow)
sm6125-xiaomi-willow.dts          # willow-specific (NFC, etc.)
sm6125.dtsi                       # SoC-level definitions
pm6125.dtsi                       # PMIC definitions
```

## Appendix B: willow deviceinfo key fields

```bash
deviceinfo_codename="xiaomi-willow"
deviceinfo_dtb="qcom/sm6125-xiaomi-willow"   # ginkgo should be sm6125-xiaomi-ginkgo
deviceinfo_screen_width="1080"
deviceinfo_screen_height="2340"
deviceinfo_no_framebuffer="true"
deviceinfo_flash_method="fastboot"
deviceinfo_generate_bootimg="true"
deviceinfo_bootimg_qcdt="false"
deviceinfo_append_dtb="true"                 # laurel uses this; ginkgo may need it too
deviceinfo_flash_pagesize="4096"
```

## Appendix C: project repo status

- Path: `.`
- Status: empty directory; port work has not started
- Existing assets: self-built kernel `4.14.117-perf+` running on the device (builder `local-builder@host`)

---

*This document was generated from postmarketOS ginkgo porting research. Update it, or a separate changelog, as the port proceeds.*
