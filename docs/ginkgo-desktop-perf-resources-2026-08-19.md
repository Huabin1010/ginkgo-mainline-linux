**Language:** English | [简体中文](zh-CN/ginkgo-desktop-perf-resources-2026-08-19.md)

# Redmi Note 8 (ginkgo) desktop stutter, Resources temperature and GPU graphs

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> System: mainline Linux 7.0 + Ubuntu 26.04 LTS arm64, rootfs on userdata  
> Acceptance: 2026-08-19 02:36 · user confirmed after CPUFreq “overall much better, but still very stuttery”; then TSENS / GPU sysfs / GSK were added, and the Resources side can read temperature and GPU utilization  
> Prerequisites: Adreno 610 desktop already visible. See [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md).

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md) | Native GPU up, desktop visible |
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | Install GNOME / black screen at the time |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device timeline |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

Still verify with `fastboot boot out/boot.img`. **Do not** `fastboot flash boot`.

---

## 0. One-sentence conclusion

The first cut on stutter was not “the GPU is idle,” but that **none of the eight cores had CPUFreq**: little/big cores sat on the bootloader vote, and the scheduler still placed interactive work on the big cores, which were slower at the time. After OSM was added, the user confirmed overall much better.

Resources showing no Sensors temperature and a flat GPU utilization line is **userspace only recognizing x86/AMD sysfs names**, not sensors or the GPU being dead. `cpu-thermal` and `gpu_busy_percent` / `freq1_input` were added to match its contract.

The first `fastboot boot` with BWMON hung USB: trinket’s CPU bwmon cannot copy SM6115’s `0x01b8e300`. That block is currently **disabled**.

---

## 1. Three things the user saw

| Symptom | Actual cause |
|---------|--------------|
| Tapping icons, entering the desktop, opening apps, and going back all stutter | No `cpufreq`; big cores were slower than little cores at the time |
| Resources Sensors has no CPU temperature | SM6125 originally had no TSENS; even with it, Resources only accepts type **`cpu-thermal`** |
| Resources GPU utilization/clocks do not move at all | It reads AMD paths: `card0/device/gpu_busy_percent` and `hwmon/*/freq1_input`; msm did not have those |

On-device evidence (before CPUFreq):

```
/sys/devices/system/cpu/cpu0/cpufreq   missing (none of the 8 cores)
same Python snippet: little ~2.6s, big ~5.7s
gnome-shell ~35–41% CPU, GSK_RENDERER=ngl, scale 2
GPU was actually scaling (320–950 MHz, trans_stat showed switches)
wa=0, not eMMC stalling the machine
```

GNOME Resources (nokyan/resources) accepts CPU temperature only from:

- hwmon names: `zenpower` / `coretemp` / `k10temp` / `ibmpowernv`
- thermal zone type: `cpu-thermal` / `x86_pkg_temp` / `acpitz`

GPU “Other” (msm) only reads:

```
/sys/class/drm/card0/device/gpu_busy_percent
/sys/class/drm/card0/device/hwmon/hwmon?/freq1_input
```

`card0/device` is the DPU platform device (`5e01000.display-controller`), not `5900000.gpu`. sysfs must hang off that device.

---

## 2. What was done

### 2.1 CPUFreq (first cut on stutter; user confirmed it helped)

SM6125 DTS originally had no OSM. Kryo 260 OSM uses the same registers as SM6115:

- `cpufreq@f521000`: `qcom,sm6125-cpufreq-hw`, `0xf521000` / `0xf523000`
- CPU nodes get `qcom,freq-domain` + `clocks`
- `CONFIG_ARM_QCOM_CPUFREQ_HW=y`
- yaml: `qcom,sm6125-cpufreq-hw`

Acceptance (after `fastboot boot`):

| Cluster | governor | Frequency then | Range |
|---------|----------|----------------|-------|
| Little cpu0–3 | schedutil | 1804 MHz | 300–1804 |
| Big cpu4–7 | schedutil | 2016 MHz | 300–2016 |

User feedback: **overall much better, but still very stuttery.**

### 2.2 TSENS + `cpu-thermal` (Resources Sensors)

Downstream `trinket.dtsi`: `tsens@4410000`, TM `0x4411000`, SROT `0x4410000`, IRQ 275 / 190. Mainline `init_common()` parses **index 0 = TM, index 1 = SROT**, the reverse of downstream `reg-names`; the addresses match.

Landed:

- `tsens0@4411000`: `qcom,sm6115-tsens`, `qcom,tsens-v2`, 16 sensors
- `qfprom@1b40000`: QFPROM required by `CONFIG_QCOM_TSENS`
- `CONFIG_QCOM_TSENS=y`, `CONFIG_NVMEM_QCOM_QFPROM=y`
- thermal-zones **must include one literally named `cpu-thermal`** (tsens0 sensor 6, trinket’s first Kryo gold)

Extra zones (Resources does not use them; for debug): `cpu0123-thermal`, `gpu-thermal`, `mapss-thermal`.

critical trips were changed to **`hot`** first, so uncalibrated readings do not trigger `orderly_poweroff`.

On-device readings (2026-08-19 02:35):

```
mapss-thermal     41600
cpu-thermal       43800
cpu0123-thermal   46100
gpu-thermal       40900
```

Units are millicelsius, about 41–46 °C, reasonable.

### 2.3 GPU utilization / clocks (Resources GPU graph)

`linux/drivers/gpu/drm/msm/`:

- `gpu_busy_percent`: `DEVICE_ATTR_RO`, attached to the DRM parent device, i.e. the path Resources wants: `card0/device/gpu_busy_percent`
- hwmon name `adreno`, custom `freq1_input` (mainline `hwmon` has no `hwmon_freq` type; handwritten attribute, same as amdgpu)
- Frequency from `msm_gpu_get_freq()` (uses shadow `idle_freq` when idle)

**Do not read GMU `POWER_COUNTER` directly from sysfs.** The first implementation called `msm_devfreq_get_dev_status()` → `a6xx_gpu_busy()` → `gmu_read64()`, raced with IFPC, hung the bus, and `cat gpu_busy_percent` blocked. It now returns the governor’s last sample (`devfreq.last_busy_percent`, once per 50 ms).

Near-0 utilization and **320 MHz** when idle is normal. Sliding the desktop sampled 3%–44%, clocks up to 745 MHz.

### 2.4 GPU memory bandwidth + GSK

- GPU node: `interconnects` `MASTER_GRAPHICS_3D → SLAVE_EBI_CH0`, `interconnect-names = "gfx-mem"`
- OPPs gained `opp-peak-kBps` (adreno already calls `dev_pm_opp_of_find_icc_paths`)
- Overlay: `rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` changed from `GSK_RENDERER=ngl` to **`gl`** (ngl is written for llvmpipe)

Current session: `gnome-shell` environment is already `GSK_RENDERER=gl`.

### 2.5 CPU BWMON: leave it off for now

Downstream trinket:

```
reg = <0x01b8e200 0x100>, <0x01b8e100 0x100>;  /* base, global_base */
IRQ 421
```

After copying SM6115 `pmu@1b8e300` + `qcom,sdm845-bwmon`, `bwmon_start()` wrote ENABLE@0x2a0, **the kernel did not come up and USB RNDIS never appeared**. The user had to key back into fastboot.

Current DTS: `pmu@1b8e200`, `reg = 0x01b8e200 / 0x600`, **`status = "disabled"`**. `CONFIG_QCOM_ICC_BWMON=y` stays; the node will not probe while disabled.

---

## 3. Changed files (for regression)

| File | Purpose |
|------|---------|
| `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` | OSM, TSENS, qfprom, thermal-zones, GPU ICC/OPP, bwmon disabled |
| `config/ginkgo.fragment` | `CPUFREQ_HW`, `TSENS`, `QFPROM`, `ICC_BWMON` |
| `linux/drivers/gpu/drm/msm/msm_drv.c` | `gpu_busy_percent` + adreno hwmon |
| `linux/drivers/gpu/drm/msm/msm_gpu_devfreq.c` | Cache utilization; `msm_gpu_get_freq()` |
| `linux/drivers/gpu/drm/msm/msm_gpu.h` / `msm_drv.h` | Declarations and `hwmon` pointer |
| `rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` | `GSK_RENDERER=gl` |
| yaml `cpufreq-qcom-hw.yaml` | `qcom,sm6125-cpufreq-hw` |

Do not touch: DSI prefetch, touch CS/`CONFIG_FB`, WiFi MSA, simplefb, HBB=14, zap `0x57515000`, SQE from linux-firmware.

---

## 4. Not finished / remaining stutter

1. **CPU bwmon is not enabled** — needs downstream `0x01b8e200` + `0x01b8e100` matched to mainline `sdm845-bwmon` or `msm8998-bwmon` (two regions) before turning it on. Until then, `fastboot boot` only.  
2. **Remaining stutter** — 1080×2340 + scale 2 + GNOME; GPU bandwidth was just added, so feel needs a few more swipes to judge.  
3. Resources must be **closed and reopened** to rescan sysfs.  
4. The GPU graph is almost flat when idle; it jumps only when **swiping / opening windows**.

---

## 5. Do not

| Do not | Why |
|--------|-----|
| Re-enable `pmu@1b8e300` | Confirmed to hang boot |
| Call `gpu_busy()` / GMU counters from sysfs again | IFPC race; reading the register hangs |
| Name the thermal zone `cpu4-thermal` or similar | Resources will not accept it; Sensors stays empty |
| Attach `gpu_busy_percent` to `5900000.gpu` | Resources only looks at `card0/device/` |
| Change DSI / HBB / zap to “feel smoother” | Pixels and GPU composition already align |
| `fastboot flash boot` | This repo is `fastboot boot` only |

First `fastboot boot` Sending failure: **do not usbreset**. `killall -9 fastboot` and retry. After boot the USB interface name changes; rerun `connect.sh`.

---

## 6. Regression commands

```bash
# CPUFreq
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq

# Temperature name Resources wants
grep . /sys/class/thermal/thermal_zone*/type
cat /sys/class/thermal/thermal_zone1/temp   # cpu-thermal, about 40xxx

# Resources GPU
cat /sys/class/drm/card0/device/gpu_busy_percent
cat /sys/class/drm/card0/device/hwmon/hwmon*/freq1_input
# busy should be non-zero while swiping the desktop; idle freq about 320000000

# GSK
tr '\0' '\n' < /proc/$(pgrep -n -u ginkgo gnome-shell)/environ | grep GSK
# expect GSK_RENDERER=gl

# Should not appear again
dmesg | grep -i bwmon
```

Expect: little/big both have `schedutil`; type `cpu-thermal` exists and temperature is millicelsius; `gpu_busy_percent` can be read 12 times in a row without hanging; gnome-shell is `GSK_RENDERER=gl`.
