**Language:** English | [简体中文](zh-CN/mainline-ginkgo-porting-guide.md)

# Redmi Note 8 (ginkgo) mainline Linux porting guide

> Document version: 2026-08-04  
> Goal: **mainline Linux 7.0** → **Ubuntu 26.04 LTS** → **touch + Wi-Fi usable**  
> Device: Xiaomi Redmi Note 8 (`ginkgo`), serial `<serial>`  
> Strategy: **stay on mainline; do not use a downstream kernel**

---

## Contents

1. [Project goals and scope](#1-project-goals-and-scope)
2. [Device hardware inventory (measured)](#2-device-hardware-inventory-measured)
3. [Storage and partition layout](#3-storage-and-partition-layout)
4. [Buses and address map](#4-buses-and-address-map)
5. [Mainline kernel status and gaps](#5-mainline-kernel-status-and-gaps)
6. [Display subsystem (DRM/DSI)](#6-display-subsystem-drmdsi)
7. [Touch subsystem (SPI)](#7-touch-subsystem-spi)
8. [Wi-Fi subsystem (WCN3990/ICNSS)](#8-wi-fi-subsystem-wcn3990icnss)
9. [Reserved memory layout](#9-reserved-memory-layout)
10. [Firmware inventory and extraction](#10-firmware-inventory-and-extraction)
11. [Mainline DTS status and remaining items](#11-mainline-dts-status-and-remaining-items)
12. [Reference ports](#12-reference-ports)
13. [Ubuntu 26.04 integration](#13-ubuntu-2604-integration)
14. [Upstream patch list](#14-upstream-patch-list)
15. [Phased implementation plan](#15-phased-implementation-plan)
16. [Risk register](#16-risk-register)
17. [Appendix: raw collected data](#17-appendix-raw-collected-data)

---

## 1. Project goals and scope

### 1.1 Explicit goals

| Priority | Goal | Acceptance |
|--------|------|----------|
| P0 | Mainline kernel boot | Linux 7.0 + ginkgo DTB starts; serial console works |
| P1 | Ubuntu 26.04 runs | arm64 rootfs mounted; systemd starts; shell login |
| P2 | DRM display | 1080×2340 output; `/dev/dri/card0` exists |
| P3 | Touch | `/dev/input/event*` reports touch events; UI is usable |
| P4 | Wi-Fi | `wlan0` is up; can scan and join an AP |

### 1.2 Explicitly out of scope (early stages)

- Cellular modem / calls / SMS
- Camera
- Audio playback
- Bluetooth
- Fingerprint
- GPU 3D acceleration
- Accurate battery reporting

### 1.3 Difficulty overview

| Overall | **7.5 / 10** |
|------|-------------|
| Optimistic schedule | Full-time 3–4 months |
| Conservative schedule | 6–9 months |
| Largest bottlenecks | Wi-Fi (SM6125 SoC mainline Wi-Fi node missing) + touch (no mainline SPI driver) |

---

## 2. Device hardware inventory (measured)

Collected: 2026-08-04. Sources: adb + dmesg + `/proc/device-tree`

### 2.1 SoC and platform

| Item | Value |
|------|-----|
| SoC | Qualcomm SM6125 (internal codename trinket) |
| Marketing name | Snapdragon 665 |
| CPU | 6-core Kryo 260 (2×A73 @ 2.0GHz + 4×A53 @ 1.8GHz effective) |
| GPU | Adreno 610 |
| Memory | 5782272 kB (~5.5 GB; kernel limit ~4044 MB usable) |
| Storage | eMMC `3H6CAB`, 122142720 × 512B ≈ **58.3 GB** |
| Baseband | MPSS.AT.4.3.1 (Qualcomm Modem; not ported yet) |
| Bootloader | Unlocked (`verifiedbootstate=orange`) |
| HW Version | 1.9.0 (CN) |

### 2.2 Display

| Item | Value | Source |
|------|-----|------|
| Resolution | 1080 × 2340 | cmdline / wm size |
| Density | 420 dpi | ro.sf.lcd_density |
| Panel IC | Novatek **NT36672A** | dmesg |
| Panel vendor | **Tianma** | dmesg: `[nt36672a video mode dsi tianma panel]` |
| Downstream panel name | `dsi_nt36672a_tianma_vid_display` | cmdline `msm_drm.dsi_display0=` |
| DSI mode | Video mode | dmesg |
| MDSS base | `0x05e00000` | dmesg / mainline sm6125.dtsi |
| DSI Ctrl | `0x05e94000` | dmesg |
| DSI PHY | `0x05e94400` | dmesg |

### 2.3 Touch (important: SPI, not I2C)

| Item | Value | Source |
|------|-----|------|
| Touch IC | Novatek **NT36672A** | dmesg: `TP info: [Vendor]tianma [IC]nt36672a` |
| Bus | **SPI** (not I2C!) | dmesg: `spi_geni 4a88000.spi` |
| SPI controller | `spi@4a88000` (mainline: `&spi2`) | device-tree |
| Compatible | `focaltech,fts` + `novatek,NVT-ts-spi` | device-tree |
| SPI frequency | **8 MHz** (`0x007a1200`) | device-tree |
| Interrupt GPIO | **TLMM 88**, ACTIVE_LOW | device-tree / dmesg |
| Reset GPIO | **TLMM 87** | device-tree |
| SW Reset address | `0x03F0FE` | device-tree `novatek,swrst-n8-addr` |
| SPI Fast Read address | `0x03F310` | device-tree `novatek,spi-rd-fast-addr` |
| Firmware file | `novatek_ts_tianma_fw.bin` (118784 bytes) | dmesg / `/vendor/firmware/` |
| Input device name | `NVTCapacitiveTouchScreen` | `/sys/class/input/` |
| IRQ number | 232 | dmesg |

**Key conclusion:** the mainline `novatek-nvt-ts` driver is **I2C** and **cannot** be used on ginkgo as-is. An SPI driver for `novatek,NVT-ts-spi` is required (currently only downstream / out-of-tree).

### 2.4 Wi-Fi

| Item | Value | Source |
|------|-----|------|
| Chip | Qualcomm **WCN3990** | Device spec / ICNSS driver |
| Driver (Android) | `icnss` + `cnss` | dmesg |
| Driver (mainline target) | `ath10k_snoc` + `wcn36xx` | Mainline kernel |
| Base address | `0x0c800000` | device-tree `qcom,icnss@C800000` |
| MSA memory | `wlan_msa_region@53300000`, 2 MB | dmesg |
| Firmware DSP | `wlanmdsp.mbn` (3720220 bytes) | `/vendor/firmware_mnt/image/` |
| Board data | `bdwlan.bin` (26328 bytes) | `/vendor/firmware_mnt/image/` |
| FW Ready flag | `0xd87` | dmesg |

### 2.5 Other hardware (later stages)

| Component | Model | I2C address | Bus |
|------|------|----------|------|
| Audio codec | max98927 | 0x3a | i2c-0 |
| Audio amp | tas2562 | 0x4c | i2c-0 |
| Fingerprint | Goodix (`gdx`) | — | SPI/UART |
| LED driver | ktd3136 | 0x36 | i2c-0 |
| USB-C switch | fsa4480 | 0x43 | i2c-0 |
| PMIC | PM6125 + PMI632 | SPMI | — |

### 2.6 SKU differences

| Component | This unit | Other lots may have |
|------|------|-------------|
| Panel | Tianma NT36672A | Huaxing FT8719 and others |
| Main camera | TBD | Samsung GM1/GM2, Sony IMX582 |
| Touch firmware | `novatek_ts_tianma_fw.bin` | `novatek_ts_ebbg_fw.bin` |
| Fingerprint | Goodix | FPC1020 |

---

## 3. Storage and partition layout

### 3.1 Key partitions

| Partition | Use | Notes |
|------|------|------|
| `xbl` / `xbl_config` | Primary bootloader | Do not flash |
| `abl` | Android Bootloader | fastboot entry |
| `boot` | Kernel + ramdisk | **pmOS/Ubuntu flash target** |
| `dtbo` | Device Tree Overlay | Usually unused on mainline |
| `vbmeta` | Verified Boot metadata | Needs `--disable-verification` |
| `system` | System partition (current LineageOS) | Reuse or repartition |
| `vendor` | Vendor partition | Contains firmware |
| `userdata` | User data | Ubuntu rootfs can live here |
| `persist` | Persistent calibration | Keep |
| `modem` | Modem firmware | Do not modify yet |

### 3.2 Flash parameters (from willow pmOS / generic Qualcomm)

```bash
# boot.img format
deviceinfo_flash_pagesize="4096"
deviceinfo_flash_offset_base="0x00000000"
deviceinfo_flash_offset_kernel="0x00008000"
deviceinfo_flash_offset_ramdisk="0x01000000"
deviceinfo_flash_offset_second="0x00f00000"
deviceinfo_flash_offset_tags="0x00000100"
deviceinfo_generate_bootimg="true"
deviceinfo_bootimg_qcdt="false"
deviceinfo_append_dtb="true"
```

### 3.3 Bootloader state

```
verifiedbootstate=orange    # unlocked
slot_suffix=(empty)           # not A/B
secureboot=1                # secure boot on, but vbmeta can be disabled
```

---

## 4. Buses and address map

### 4.1 Mainline SM6125 bus map

| Downstream address | Mainline node | Function | ginkgo use |
|----------|--------------|------|-------------|
| `0x04a88000` | `&spi2` / `&i2c2` | QUP SE2 (muxed) | **Touch SPI** |
| `0x04a8c000` | `&i2c3` | QUP SE3 | TBD |
| `0x04a90000` | `&uart4` | Debug UART | Serial console |
| `0x04e00000` | `&usb3` | USB DWC3 | adb / network |
| `0x05e00000` | `&mdss` | Display subsystem | DRM/DSI |
| `0x05e94000` | `&mdss_dsi0` | DSI controller | Panel |
| `0x05e94400` | `&mdss_dsi0_phy` | DSI PHY 14nm | Panel |
| `0x0c800000` | **Missing** (to add) | WCN3990 Wi-Fi | Wi-Fi |
| `0x04744000` | `&sdhc_1` | eMMC | Storage |

### 4.2 I2C device map (measured on i2c-0)

| Address | Device | Driver |
|------|------|------|
| 0x08 | i2c-pmic | PMIC I2C |
| 0x09 | i2c-pmic | PMIC I2C |
| 0x36 | ktd3136 | LED |
| 0x3a | max98927L | Audio codec |
| 0x43 | fsa4480-i2c | USB-C switch |
| 0x4c | tas2562 | Audio amp |

### 4.3 Kernel cmdline reference

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
androidboot.hardware=qcom
androidboot.bootdevice=4744000.sdhci
androidboot.usbcontroller=4e00000.dwc3
msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display:
```

Suggested mainline cmdline (early):

```
console=ttyMSM0,115200n8
earlycon
root=/dev/mmcblk0pXX
rw
```

---

## 5. Mainline kernel status and gaps

### 5.1 Feature matrix

| Feature | Mainline ginkgo DTS | Mainline driver | Downstream Android | Gap |
|------|----------------|---------|-------------|------|
| Boot / Serial | ✅ | ✅ | ✅ | None |
| eMMC | ✅ | ✅ | ✅ | None |
| USB | ✅ | ✅ | ✅ | None |
| SD card | ✅ | ✅ | ✅ | None |
| Keys | ✅ | ✅ | ✅ | None |
| Simple FB | ✅ | ✅ | — | Temporary |
| **DRM/DSI display** | ❌ | ✅ driver exists | ✅ | **Need DTS + panel timing** |
| **Touch SPI** | ❌ | ❌ no SPI driver | ✅ ts_nt36xxx | **Need a new driver or a port** |
| **Wi-Fi** | ❌ | ✅ driver exists | ✅ icnss | **Need SoC wifi node + DTS + firmware** |
| Audio | ❌ | Partial | ✅ | Later |
| Modem | ❌ | Partial | ✅ | Later |

### 5.2 History of mainline ginkgo patches

| Date | Author | Content |
|------|------|------|
| 2025-03 | Gabriel Gonzales | Initial ginkgo DTS |
| 2026-01 | Barnabás Czémán | Reserved-memory fix, ginkgo-common.dtsi, framebuffer memory-region |
| 2026-01 | Biswapriyo Nath | Volume Up fix, RTC, Debug UART |
| 2026-03 | Biswapriyo Nath | Vibrator, IR, USB-C OTG |

### 5.3 SM6125 vs SM6115 (critical for Wi-Fi)

`sm6115.dtsi` **has** a `wifi@c800000` node; `sm6125.dtsi` **does not**.

sm6115 wifi node template (needs porting to sm6125.dtsi):

```dts
wifi: wifi@c800000 {
    compatible = "qcom,wcn3990-wifi";
    reg = <0x0 0x0c800000 0x0 0x800000>;
    reg-names = "membase";
    memory-region = <&wlan_msa_mem>;
    interrupts = <GIC_SPI 358 IRQ_TYPE_LEVEL_HIGH>,
                 /* ... 12 CE interrupts total ... */;
    iommus = <&apps_smmu 0x1a0 0x1>;
    qcom,msa-fixed-perm;
    status = "disabled";
};
```

---

## 6. Display subsystem (DRM/DSI)

### 6.1 Hardware connections

```
MDSS (0x5e00000)
  └── MDP/DPU (0x5e01000)
        └── DSI0 (0x5e94000) ── DSI lanes x4 ──► NT36672A (Tianma)
              └── PHY (0x5e94400, 14nm)
```

### 6.2 Downstream power (from device-tree)

| Supply | Notes |
|--------|------|
| `ibb-supply` | Panel IBB negative (LCDB NCP) |
| `lab-supply` | Panel LAB positive (LCDB LDO) |
| `vddio-supply` | Panel IO → `vreg_l9a` (1.8V) |

### 6.3 Mainline panel driver

- Driver: `drivers/gpu/drm/panel/panel-novatek-nt36672a.c`
- Existing compatible: `tianma,fhd-video` + `novatek,nt36672a`
- Existing resolution: **1080×2246** (Poco F1)
- **ginkgo needs: 1080×2340** — new timing + possibly a new init sequence

### 6.4 Remaining work

1. **Extract from downstream** the ginkgo Tianma NT36672A DSI init sequence (downstream `dsi_nt36672a_tianma_vid_display` panel driver source)
2. **Extend** `panel-novatek-nt36672a.c` or add a new `tianma,fhd-video-ginkgo` compatible
3. **Add to the ginkgo DTS** (see laurel):
   - `&mdss { status = "okay"; }`
   - `&mdss_dsi0 { ... panel@0 { ... } }`
   - `&mdss_dsi0_phy { status = "okay"; }`
   - Panel regulators (GPIO-controlled LDOs)
   - pinctrl for reset GPIO
4. **Verify** `modetest -M msm_drm -s <connector>:1080x2340`

### 6.5 Reference: laurel mainline MDSS DTS

```dts
&mdss { status = "okay"; };

&mdss_dsi0 {
    vdda-supply = <&vreg_l18a>;
    status = "okay";
    panel@0 {
        compatible = "samsung,s6e8fc0-m1906f9";
        reg = <0>;
        reset-gpios = <&tlmm 90 GPIO_ACTIVE_LOW>;
        vdd-supply = <&panel_vdd_1p8>;
        vci-supply = <&panel_vci_3p0>;
        port {
            panel_in: endpoint {
                remote-endpoint = <&mdss_dsi0_out>;
            };
        };
    };
};

&mdss_dsi0_out {
    data-lanes = <0 1 2 3>;
    remote-endpoint = <&panel_in>;
};

&mdss_dsi0_phy { status = "okay"; };
```

### 6.6 Panel variant note

`/vendor/etc/` contains several QDCM calibration files:
- `qdcm_calib_data_nt36672a_video_mode_dsi_tianma_panel.xml` (this unit)
- `qdcm_calib_data_nt36672a_video_mode_dsi_shenchao_panel.xml` (Shenchao variant)

---

## 7. Touch subsystem (SPI)

### 7.1 Key finding: SPI, not I2C

ginkgo touch is on **SPI**, compatible `novatek,NVT-ts-spi`.

Mainline `drivers/input/touchscreen/novatek-nvt-ts.c` is **I2C-only** and **cannot be used directly**.

### 7.2 Device-tree parameters (measured)

```dts
&spi2 {   /* downstream spi@4a88000, mainline &spi2 */
    status = "okay";

    touchscreen@0 {
        compatible = "focaltech,fts", "novatek,NVT-ts-spi";
        reg = <0>;

        spi-max-frequency = <8000000>;
        spi-cs-high;

        novatek,irq-gpio = <&tlmm 88 GPIO_ACTIVE_LOW>;
        novatek,reset-gpio = <&tlmm 87 GPIO_ACTIVE_HIGH>;

        novatek,swrst-n8-addr = <0x03f0fe>;
        novatek,spi-rd-fast-addr = <0x03f310>;

        touch_vddio-supply = <&...>;  /* vreg_l18a or similar */
        touch_lab-supply = <&...>;    /* LCDB LDO */
        touch_ibb-supply = <&...>;    /* LCDB NCP */
    };
};
```

### 7.3 Driver options

| Option | Description | Mainline-compliant | Difficulty |
|------|------|---------|------|
| A. Port downstream `nt36xxx_spi` | Port `nt36xxx_spi` from the Lineage/CAF kernel | ❌ out-of-tree | Medium |
| B. Write a new SPI driver upstream | Extend `novatek-nvt-ts` with SPI | ✅ | High |
| C. Community OOT module | Like willow pmOS `ts_nt36xxx` but SPI | ❌ | Medium |

**Recommended path:** short-term option A to prove function; long-term push option B upstream.

### 7.4 Firmware

| File | Size | Path |
|------|------|------|
| `novatek_ts_tianma_fw.bin` | 118784 B | `/vendor/firmware/` |
| `novatek_ts_tianma_mp.bin` | 118784 B | `/vendor/firmware/` (factory test) |
| `novatek_ts_ebbg_fw.bin` | 118784 B | Other panel lots |

The downstream driver loads firmware at boot (dmesg: `Update firmware success! <105436 us>`).

### 7.5 Trinket platform reference

realme5 `trinket-idp.dtsi` (same SM6125/trinket platform) uses the same GPIOs:
- IRQ: TLMM 88
- Reset: TLMM 87
- Compatible: `novatek,NVT-ts-spi`
- SW Reset: `0x03F0FE`

This confirms the ginkgo touch config matches the trinket reference board.

---

## 8. Wi-Fi subsystem (WCN3990/ICNSS)

### 8.1 Hardware

| Item | Value |
|------|-----|
| Chip | WCN3990 |
| Interface | SNOC (Shared Network-on-Chip) |
| Base address | `0x0c800000`, size 8 MB |
| MSA memory | `0x53300000`, 2 MB (`wlan_msa_mem`) |
| Interrupts | CE0–CE11 (GIC SPI 358–369; SM6125 numbers TBD) |

### 8.2 Downstream ICNSS power (device-tree)

| Supply property | Notes |
|-------------|------|
| `vdd-cx-mx-supply` | CX/MX 0.8V |
| `vdd-1.8-xo-supply` | XO 1.8V |
| `vdd-1.3-rfa-supply` | RFA 1.3V |
| `vdd-3.3-ch0-supply` | CH0 3.3V |

Corresponding sm6115 reference (PM6125 regulators):

```dts
&wifi {
    vdd-0.8-cx-mx-supply = <&vreg_l8a>;   /* or pm6125_l8a */
    vdd-1.8-xo-supply = <&vreg_l16a>;
    vdd-1.3-rfa-supply = <&vreg_l17a>;
    vdd-3.3-ch0-supply = <&vreg_l23a>;
    qcom,calibration-variant = "XXX_Ginkgo";  /* TBD */
    status = "okay";
};
```

### 8.3 Firmware

Already extracted from the device (reference copies in `firmware/ginkgo/`):

| File | Size | Mainline install path |
|------|------|-------------|
| `wlanmdsp.mbn` | 3,720,220 B | `/lib/firmware/qcom/` or an ath10k subdirectory |
| `bdwlan.bin` | 26,328 B | Convert to `board-2.bin` → `/lib/firmware/ath10k/WCN3990/hw1.0/` |
| `firmware-5.bin` | TBD | `/lib/firmware/ath10k/WCN3990/hw1.0/` |

**bdwlan → board-2.bin conversion:**

```bash
# Konrad Dybcio's method
# https://github.com/jhugo/linux/blob/5.5rc2_wifi/README
python3 bdwlan_to_board2.py bdwlan.bin > board-2.bin
```

**firmware-5.bin source:** match from the ath10k-firmware repo or further device extraction. `wlanmdsp.mbn` and `firmware-5.bin` versions must match, or ath10k reports errors such as `SINGLE_CHAN_INFO`.

### 8.4 Mainline driver chain

```
Device Tree (&wifi)
  → ath10k_snoc (platform driver)
    → wcn36xx (WCN3990 specific)
      → ath10k_core
        → mac80211/cfg80211
          → wlan0
```

Kernel config requirements:

```
CONFIG_ATH10K=y/m
CONFIG_ATH10K_SNOC=y/m
CONFIG_WCN36XX=y/m
CONFIG_QCOM_WCNSS_PIL=y/m
```

### 8.5 Known mainline issues

- WCN3990 A-MSDU fragmentation bug: throughput collapse under load (RFC patches in July–August 2026)
- `firmware-5.bin` vs `wlanmdsp.mbn` version mismatch (ath10k mailing list 2023-12)
- SM6125 still has no wifi node — **upstream a `sm6125.dtsi` wifi node first**

### 8.6 Remaining work

1. Add a `wifi@c800000` node to `sm6125.dtsi` (from sm6115; adjust interrupt numbers)
2. Add `&wifi { status = "okay"; ... }` in `sm6125-xiaomi-ginkgo-common.dtsi`
3. Determine the `qcom,calibration-variant` string (from bdwlan or downstream cnss config)
4. Extract and install the full firmware set
5. Create a `linux-firmware` or device-specific firmware package
6. Debug ath10k probe and association

---

## 9. Reserved memory layout

### 9.1 Mainline ginkgo-common.dtsi (current)

| Region | Address | Size | Use |
|------|------|------|------|
| adsp_pil_mem | 0x55300000 | 34 MB | ADSP PIL |
| ipa_fw_mem | 0x57500000 | 64 KB | IPA firmware |
| ipa_gsi_mem | 0x57510000 | 20 KB | IPA GSI |
| gpu_mem | 0x57515000 | 8 KB | GPU |
| framebuffer_mem | 0x5c000000 | ~9.9 MB | Simple FB / display |
| ramoops | 0x61600000 | 4 MB | Kernel log |

### 9.2 Extra downstream regions (reference)

| Region | Address | Size | Use |
|------|------|------|------|
| wlan_msa_mem | 0x53300000 | 2 MB | Wi-Fi MSA |
| modem_region | 0x4b000000 | 126 MB | Modem |
| camera_region | 0x4ab00000 | 5 MB | Camera |
| cdsp_regions | 0x53500000 | 30 MB | CDSP |

**Note:** a wrong reserved-memory definition can crash under load. Barnabás Czémán’s 2026 patch fixed the ginkgo layout. Verify with the `memtest=1` kernel parameter.

---

## 10. Firmware inventory and extraction

### 10.1 Already extracted from the device

| File | Size | Use | Local path |
|------|------|------|----------|
| `bdwlan.bin` | 26,328 B | Wi-Fi board data | `firmware/ginkgo/bdwlan.bin` |
| `wlanmdsp.mbn` | 3,720,220 B | Wi-Fi DSP firmware | `firmware/ginkgo/wlanmdsp.mbn` |
| `novatek_ts_tianma_fw.bin` | 118,784 B | Touch firmware | `firmware/ginkgo/novatek_ts_tianma_fw.bin` |

### 10.2 Still to extract

| File | Path | Use |
|------|------|------|
| `firmware-5.bin` | Match from bdwlan or ath10k-firmware | Wi-Fi MAC firmware |
| `bdwlan.bXX` | `/vendor/firmware_mnt/image/bdwlan.*` | Determine board ID |
| ADSP/Modem firmware | `/vendor/firmware_mnt/image/` | Later audio/modem |

### 10.3 Extraction commands

```bash
# Needs root
adb shell su -c 'cp /vendor/firmware/novatek_ts_tianma_fw.bin /data/local/tmp/'
adb shell su -c 'cp /vendor/firmware_mnt/image/wlanmdsp.mbn /data/local/tmp/'
adb shell su -c 'cp /vendor/firmware_mnt/image/bdwlan.bin /data/local/tmp/'
adb pull /data/local/tmp/novatek_ts_tianma_fw.bin firmware/ginkgo/
adb pull /data/local/tmp/wlanmdsp.mbn firmware/ginkgo/
adb pull /data/local/tmp/bdwlan.bin firmware/ginkgo/
```

---

## 11. Mainline DTS status and remaining items

### 11.1 Already in `sm6125-xiaomi-ginkgo-common.dtsi`

- [x] `qcom,msm-id`
- [x] Simple framebuffer (1080×2340)
- [x] Reserved memory (after the fix)
- [x] GPIO keys (Volume Up / Power / Volume Down)
- [x] Regulators (full RPM PM6125 set)
- [x] eMMC (sdhc_1) + SD card (sdhc_2)
- [x] USB (usb3, hsusb_phy1)
- [x] Debug UART (uart4)
- [x] ramoops

### 11.2 Still to add to the ginkgo DTS

- [ ] `&mdss` + `&mdss_dsi0` + panel node
- [ ] Panel regulators (GPIO LDO)
- [ ] Panel pinctrl (reset GPIO)
- [ ] `&spi2` + touchscreen SPI node
- [ ] Touch regulators
- [ ] `&wifi` node (depends on a wifi node in sm6125.dtsi first)
- [ ] Wi-Fi regulator config
- [ ] `qcom,calibration-variant`

### 11.3 Still to add to `sm6125.dtsi` (SoC-level; affects all SM6125 devices)

- [ ] `wifi@c800000` node (port from sm6115)

---

## 12. Reference ports

### 12.1 Same-platform SM6125 mainline devices

| Device | Display | Touch | Wi-Fi | Repo |
|------|------|------|------|------|
| laurel (Mi A3) | ✅ Samsung S6E8FC0 | ✅ FocalTech ft3518 (I2C) | ❌ | mainline DTS |
| seine (Xperia 10 II) | ✅ Samsung SOFEF01 | Partial | ❌ | mainline DTS |
| **ginkgo (this unit)** | ❌ | ❌ | ❌ | mainline DTS |

### 12.2 Same-platform downstream references

| Source | Use |
|------|------|
| `realme5-kernel trinket-idp.dtsi` | Touch SPI GPIO 88/87, NT36672 panel list |
| `rjeli/lineage_kernel_sm6125` | Full downstream DTS for ginkgo/willow |
| `willow pmOS (device/testing/)` | Downstream boot flow, touch module, firmware package |
| `Xiaomi-trinket-dev/vendor_xiaomi_sm6125-common` | Touch/panel firmware |

### 12.3 Wi-Fi references (SM6115, same PM6125)

| Device | calibration-variant | Repo |
|------|-------------------|------|
| Lenovo P11 (j606f) | `Lenovo_P11` | sm6115p-lenovo-j606f.dts |
| F(x)tec Pro1X | `Fxtec_QX1050` | sm6115-fxtec-pro1x.dts |

---

## 13. Ubuntu 26.04 integration

### 13.1 Version info

| Item | Value |
|------|-----|
| Ubuntu version | 26.04 LTS (Resolute Raccoon) |
| Default kernel | Linux 7.0 |
| Architecture | arm64 (in the main archive, not ports) |
| Official phone support | **None** |

### 13.2 Build flow overview

```
1. Get Linux 7.0 sources
2. Confirm the ginkgo DTS is in mainline (or apply a patch)
3. Configure the kernel (DRM_MSM, ATH10K_SNOC, etc.)
4. Cross-compile Image.gz + sm6125-xiaomi-ginkgo.dtb
5. Build initramfs (firmware, modules)
6. Make boot.img (mkbootimg)
7. debootstrap Ubuntu 26.04 arm64 rootfs
8. fastboot flash boot boot.img
9. Flash rootfs to the userdata partition
10. fastboot flash vbmeta --disable-verification vbmeta.img
```

### 13.3 Kernel config highlights

```
CONFIG_ARCH_QCOM=y
CONFIG_SERIAL_MSM=y
CONFIG_SERIAL_MSM_CONSOLE=y
CONFIG_DRM_MSM=y
CONFIG_DRM_MSM_DSI=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
CONFIG_ATH10K=m
CONFIG_ATH10K_SNOC=m
CONFIG_WCN36XX=m
CONFIG_MMC_SDHCI_MSM=y
CONFIG_USB_DWC3=y
CONFIG_USB_DWC3_QCOM=y
# Touch SPI driver (to add or OOT)
```

### 13.4 Rootfs options

| Option | Description |
|------|------|
| A. Ubuntu minimal | debootstrap + systemd, no desktop |
| B. Ubuntu Desktop | GNOME/Wayland; needs P2/P3 done |
| C. postmarketOS rootfs | Reuse the pmOS build system; switch to Ubuntu sources |

---

## 14. Upstream patch list

In priority order; submit to linux-arm-msm / ~postmarketos/upstreaming:

| # | Patch title | Target files | Priority | Status |
|---|-----------|---------|--------|------|
| 1 | arm64: dts: qcom: sm6125: Add WCN3990 WiFi node | `sm6125.dtsi` | P0 | TODO |
| 2 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Enable MDSS and Tianma panel | ginkgo DTS | P1 | TODO |
| 3 | drm: panel: novatek-nt36672a: Add ginkgo 1080x2340 Tianma panel | panel driver | P1 | TODO |
| 4 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Add NT36672A SPI touchscreen | ginkgo DTS | P2 | TODO |
| 5 | input: touchscreen: Add Novatek NVT SPI driver | New driver or extend existing | P2 | TODO |
| 6 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Enable WCN3990 WiFi | ginkgo DTS | P3 | TODO |
| 7 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Add WiFi calibration variant | ginkgo DTS | P3 | TODO |

---

## 15. Phased implementation plan

### Phase 1: Boot + Ubuntu minimal (2–4 weeks)

- [ ] Set up a cross-compile environment
- [ ] Build Linux 7.0 + ginkgo DTB
- [ ] Make boot.img + initramfs
- [ ] debootstrap Ubuntu 26.04 arm64
- [ ] fastboot flash; serial console login
- [ ] Verify eMMC / USB / keys

**Acceptance:** `uname -a` on serial shows Linux 7.0; `systemd` is running

### Phase 2: DRM display (4–8 weeks)

- [ ] Extract the Tianma NT36672A init sequence from downstream
- [ ] Extend panel-novatek-nt36672a for 1080×2340
- [ ] Write ginkgo MDSS/DSI/panel DTS
- [ ] Add panel regulators and pinctrl
- [ ] Verify `modetest` and simple graphics output
- [ ] Submit upstream patches

**Acceptance:** the panel shows content; no longer simplefb-only

### Phase 3: Touch SPI (3–6 weeks; partly in parallel with phase 2)

- [ ] Port or write an NT36672A SPI touch driver
- [ ] Write ginkgo SPI touchscreen DTS
- [ ] Integrate touch firmware load
- [ ] Verify `/dev/input/event*` touch events
- [ ] Test touch in Weston/GNOME

**Acceptance:** finger taps respond; UI is usable

### Phase 4: Wi-Fi (2–4 months)

- [ ] Add a wifi node to sm6125.dtsi (upstream)
- [ ] Write ginkgo &wifi DTS
- [ ] Extract/convert the full Wi-Fi firmware set
- [ ] Determine calibration-variant
- [ ] Debug ath10k probe
- [ ] Verify scan and AP join
- [ ] Handle the WCN3990 A-MSDU bug if needed

**Acceptance:** `iw dev wlan0 scan` returns results; Wi-Fi can connect

### Phase 5: Ubuntu Desktop integration (2–4 weeks)

- [ ] Install GNOME/Wayland or the chosen DE
- [ ] Configure NetworkManager + Wi-Fi
- [ ] Configure libinput + touch
- [ ] Overall stability testing

**Acceptance:** Ubuntu desktop fully usable; touch + Wi-Fi work

---

## 16. Risk register

| ID | Risk | Impact | Probability | Mitigation |
|----|------|------|------|----------|
| R1 | SM6125 wifi node rejected or delayed upstream | Wi-Fi cannot go mainline | Medium | Prove with an OOT patch first |
| R2 | Touch SPI driver needs a full rewrite | Touch slips 2–3 months | High | Short-term port of the downstream driver |
| R3 | 1080×2340 panel init sequence hard to extract | Panel stays dark | Medium | Use mdss-dsi-panel-generator |
| R4 | Wi-Fi firmware version mismatch | ath10k does not work | High | Strictly match wlanmdsp + firmware-5 |
| R5 | Panel/touch SKU differences | Some units incompatible | Medium | Multi-variant DT overlays |
| R6 | WCN3990 A-MSDU bug | Wi-Fi crash under load | Medium | Track the 2026 RFC patches |
| R7 | Wrong reserved memory | Random crashes | Low | Verify with memtest=1; follow the merged patch |

---

## 17. Appendix: raw collected data

### A. Key dmesg panel lines

```
mdss_pll_probe: MDSS pll label = MDSS DSI 0 PLL
msm-dsi-panel: [nt36672a video mode dsi tianma panel] fallback to default te-pin-select
msm-dsi-display: Successfully bind display panel 'dsi_nt36672a_tianma_vid_display'
[drm] Initialized msm_drm 1.2.0 for 5e00000.qcom,mdss_mdp on minor 0
```

### B. Key dmesg touch lines

```
[NVT-ts] TP info: [Vendor]tianma [IC]nt36672a
[NVT-ts] nvt_parse_dt: novatek,irq-gpio=88
spi_geni 4a88000.spi: proto 1
[NVT-ts] nvt_ts_probe: request irq 232 succeed
[NVT-ts] update_firmware_request: filename is novatek_ts_tianma_fw.bin
[NVT-ts] nvt_update_firmware: Update firmware success!
input: NVTCapacitiveTouchScreen
```

### C. Key dmesg Wi-Fi lines

```
icnss: Platform driver probed successfully
icnss: WLAN FW is ready: 0xd87
OF: reserved mem: initialized node wlan_msa_region@53300000
```

### D. Input device list

```
qpnp_pon                          # power key
NVTCapacitiveTouchScreen          # touch
uinput-goodix                     # fingerprint
gpio-keys                         # volume keys
```

### E. Key repository links

| Resource | URL |
|------|-----|
| Mainline ginkgo DTS | https://github.com/torvalds/linux/tree/master/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts |
| panel-novatek-nt36672a | https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/panel/panel-novatek-nt36672a.c |
| sm6125-mainline fork | https://gitlab.com/sm6125-mainline/linux |
| willow pmOS device | https://gitlab.com/postmarketOS/pmaports/-/tree/master/device/testing/device-xiaomi-willow |
| trinket-idp.dtsi reference | https://github.com/realme-kernel-opensource/realme5-kernel-source |
| ath10k-firmware WCN3990 | https://github.com/kvalo/ath10k-firmware/tree/master/WCN3990 |
| bdwlan conversion tool | https://github.com/jhugo/linux/blob/5.5rc2_wifi/README |
| mdss-dsi-panel-generator | https://github.com/msm8916-mainline/linux-mdss-dsi-panel-driver-generator |
| postmarketOS ginkgo wiki | https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo) |
| Ubuntu 26.04 arm64 | https://ubuntu.com/download/server/arm |

---

*This document is updated as the port proceeds. Related: [postmarketOS-ginkgo-research.md](./postmarketOS-ginkgo-research.md)*
