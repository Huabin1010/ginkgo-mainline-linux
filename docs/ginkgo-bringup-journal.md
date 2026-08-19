**Language:** English | [简体中文](zh-CN/ginkgo-bringup-journal.md)

# Redmi Note 8 (ginkgo) mainline Linux bring-up journal

> Device serial: `<serial>`  
> Platform: Qualcomm SM6125 (Kryo 260)  
> Kernel: Linux 7.0.0 mainline + `sm6125-xiaomi-ginkgo.dts`  
> Document updated: 2026-08-08 (boot/init-stage journal, **frozen**)  
> **Display P3 done on 2026-08-17:** [ginkgo-display-complete-2026-08-17.md](ginkgo-display-complete-2026-08-17.md)  
> **Touch P4 done on 2026-08-17:** [ginkgo-touch-complete-2026-08-17.md](ginkgo-touch-complete-2026-08-17.md)  
> **Docker done on 2026-08-19:** [ginkgo-docker-2026-08-19.md](ginkgo-docker-2026-08-19.md)  
> UART log directory: `backup/ginkgo/logs/`

This document summarizes bring-up through 2026-08-07: from “flash and drop back to fastboot” to “kernel fully boots and reaches Android init”, with causes and fixes taken from the saved UART logs.

---

## 1. Project goal

Run a **mainline Linux kernel** on **Redmi Note 8 (codename `ginkgo`)**, eventually mount an Ubuntu rootfs and reach userspace (systemd) — not LineageOS/Android.

Primary debug path is **USB-TTL UART** (early boot / panic). Recovery adb is used to flash partitions.

---

## 2. Device and environment

| Item | Details |
|------|------|
| Model | Xiaomi Redmi Note 8 (ginkgo) |
| SoC | Qualcomm SM6125 |
| Storage | eMMC 58.2 GiB (`mmc1`, address `4744000.sdhci`) |
| Stock ROM | LineageOS 17.1 (Android 10), backed up in `backup/ginkgo/` |
| Display | NT36672A Tianma 1080×2340 DSI (**mainline DRM image on screen**, 2026-08-17) |
| Debug UART | TP0003=TX (GPIO16), TP0012=RX (GPIO17), **1.8V**, 115200 8N1 |
| Host serial device | `/dev/ttyACM1` |
| Kernel build | `out/kernel/`; artifacts `out/boot.img` / `out/boot-debug.img` |

### 2.1 Wiring and capture

See [`docs/ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md), [`docs/uart-debug-ginkgo.md`](uart-debug-ginkgo.md).

```bash
# Continuous capture; logs saved automatically
sudo python3 scripts/uart-monitor.py
# Logs written to backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

**Rule: start the UART monitor before power-on / reboot.**

---

## 3. Timeline and milestones

| Date | Stage | Symptom | Outcome |
|------|------|------|------|
| 2026-08-05 | Early flash (no UART) | After flashing `boot.img`, back to fastboot in seconds; no adb | See [`mainline-boot-failure-analysis.md`](mainline-boot-failure-analysis.md); `boot-test-005614.txt` records 24 runs, all fastboot |
| 2026-08-07 22:40–23:08 | UART wiring debug | Several empty logs (not powered, or baud/wiring) | `uart-20260807-224051.log` and others empty |
| 2026-08-07 23:30 | First useful UART | ABL reports DTBO overlay failure | `uart-20260807-233001.log` |
| 2026-08-07 23:40 | Empty DTBO + mainline boot | Kernel starts; TLMM panic at ~5.76s | `uart-20260807-233836.log` |
| 2026-08-07 23:47–23:50 | Reflash after `soc@0` address-format fix | **Same TLMM panic** (root cause was not the address format) | `uart-20260807-234759.log` |
| 2026-08-07 23:52 | Reflash after `gpio-reserved-ranges` fix | **Kernel fully boots**; ~31s into Android init then reboot | `uart-20260807-235321.log` (~1.2 MB, includes reboot loops) |

---

## 4. Completed work

### 4.1 Build and flash infrastructure

| Script | Role |
|------|------|
| `scripts/build-kernel.sh` | Build `Image.gz` + `sm6125-xiaomi-ginkgo.dtb` |
| `scripts/build-bootimg.sh` | Pack Android boot image v2 (standalone DTB) |
| `scripts/build-debug-boot.sh` | Merge `ginkgo-debug.fragment`; produce `boot-debug.img` |
| `scripts/make-empty-dtbo.sh` | 24MB all-zero DTBO to bypass ABL overlay |
| `scripts/flash-mainline-test.sh` | One-shot: empty dtbo + boot-debug + reboot (recovery/fastboot) |
| `scripts/uart-monitor.py` | UART capture to disk |
| `scripts/build-rootfs.sh` and others | Ubuntu arm64 rootfs → `out/rootfs.ext4` (not yet flashed to userdata) |
| `scripts/restore-android.sh` | Restore stock boot/dtbo/vbmeta from `backup/ginkgo/` |

### 4.2 Kernel config (`config/ginkgo.fragment`)

Key items:

- `CONFIG_ARM64_VA_BITS=39` — Kryo 260 does not support 52-bit VA
- `CONFIG_SM_GCC_6125=y` — SM6125 global clocks; **without this driver, peripherals do not work**
- `CONFIG_SERIAL_QCOM_GENI_CONSOLE` / `CONFIG_SERIAL_MSM_CONSOLE` — serial console
- `CONFIG_PINCTRL_SM6125=y` — TLMM pinctrl
- `CONFIG_FB_SIMPLE` / `CONFIG_SYSFB_SIMPLEFB` — simple-framebuffer
- `CONFIG_MMC_SDHCI_MSM` / `CONFIG_EXT4_FS` — eMMC + rootfs

Debug extras (`config/ginkgo-debug.fragment`): `CONFIG_INITCALL_DEBUG`, `loglevel=8`, and similar.

### 4.3 Device-tree changes

**Board `sm6125-xiaomi-ginkgo.dts`:**

- `qcom,board-id = <0x22 0>` — matches this unit / stock DTBO (was a wrong `0x16`)
- `aliases { serial0 = &uart4; }` + `chosen.stdout-path`
- `&uart4 { status = "okay"; }` — debug UART
- `&qupv3_id_0 { status = "okay"; }`
- **`&tlmm { gpio-reserved-ranges = <0 4>, <30 4>; }`** — see §5.3

**SoC `sm6125.dtsi`:**

- `soc@0` bus changed to 2-cell addressing (same as sm6115):
  ```dts
  #address-cells = <2>;
  #size-cells = <2>;
  ranges = <0 0 0 0 0x10 0>;
  ```
- Device `reg` under it unified to the four-tuple `<0x0 addr 0x0 size>`
- Added `uart4` (`qcom,geni-debug-uart` @ `0x4a90000`) and `qup_uart4_default` pinctrl

### 4.4 Backup

`backup/ginkgo/` holds stock `boot.img`, `dtbo.img`, `vbmeta.img`, `cmdline.txt`, `partitions.txt`. Android can be restored at any time.

---

## 5. Full boot-problem record

### 5.1 Stage A: flash and drop back to fastboot (2026-08-05, no UART)

**Symptom:** 24 consecutive boots ended in fastboot; adb unavailable.

```
# backup/ginkgo/logs/boot-test-005614.txt (excerpt)
[1] adb=none fb=<serial>  fastboot
...
[24] adb=none fb=<serial>  fastboot
```

**Config issues found and fixed:**

| Problem | Fix |
|------|------|
| `CONFIG_ARM64_VA_BITS_52` | Changed to 39 |
| `qcom,board-id` mismatch | Changed to `0x22` |
| `CONFIG_SM_GCC_6125` off | Added to the fragment |
| boot.img pack format | header v2 + standalone DTB |

Still dropped back to fastboot after these fixes → UART is required to see ABL/kernel-internal errors (stage B).

---

### 5.2 Stage B: ABL intercept — DTBO overlay failure

**Log:** `backup/ginkgo/logs/uart-20260807-233001.log` (13 KB)

```
ApplyOverlay: ufdt apply overlay failed
Error: Dtb overlay failed
Launching fastboot
```

**Cause:** overlays in the stock `dtbo.img` are incompatible with the mainline `sm6125-xiaomi-ginkgo.dtb` structure. ABL fails before loading the kernel.

**Fix:** flash the 24MB all-zero `out/dtbo-empty.img` (`scripts/make-empty-dtbo.sh`). ABL skips overlay and uses only the DTB embedded in boot.img.

**Standard test command:**

```bash
./scripts/flash-mainline-test.sh
```

---

### 5.3 Stage C: TLMM pinctrl SError panic (~5.76s)

**Logs:**

- `uart-20260807-233836.log` (53 KB, full initcall trace)
- `uart-20260807-234759.log` (4 KB, panic fragment only)

**Typical panic stack:**

```
[    5.759820][    C5] SError Interrupt on CPU5, code 0x00000000bf000002
[    5.759913][    C5] pc : gpiochip_add_data_with_key+0x6a0/0xee0
[    5.760232][    C5] Kernel panic - not syncing: Asynchronous SError Interrupt
...
[    5.760596][    C5]  sm6125_tlmm_probe+0x18/0x40
[    5.760904][    C5]  of_platform_populate+0x84/0x170
```

**Analysis:**

1. Suspected wrong `#address-cells` on `soc@0` mapping TLMM to the wrong physical address; after the 2-cell fix the **panic remained**.
2. `fdtget` confirmed TLMM `reg` in the DTB is correct:
   ```
   0 5242880  0 4194304   → 0x500000, size 0x400000 (west)
   0 9437184  0 4194304   → 0x900000 (south)
   0 13631488 0 4194304   → 0xd00000 (east)
   ```
3. Disassembly showed the panic is an SError while `gpiochip_add` calls `get_direction` on **GPIO0** and reads a hardware register.
4. Compared with upstream patches and Xiaomi downstream: `gpio-reserved-ranges` was wrong.

**Root cause:** incorrect reserved GPIO ranges in `sm6125-xiaomi-ginkgo.dts`:

```dts
/* Wrong — kernel reads QUP0-dedicated pin registers */
gpio-reserved-ranges = <22 2>, <28 6>;
```

**Fix (already in the DTS):**

```dts
/* QUP0 (gpio0-3) and QUP6 (gpio30-33); reading these ctl regs faults on ginkgo */
gpio-reserved-ranges = <0 4>, <30 4>;
```

Reference: [linux-kernel mailing list — Fix reserved gpio ranges for ginkgo](https://lists.openwall.net/linux-kernel/2026/01/13/680)

---

### 5.4 Stage D: kernel boots; Android init fails (~31s)

**Log:** `backup/ginkgo/logs/uart-20260807-235321.log` (~1.2 MB; 5 full kernel boots, 8 reboots)

**Success markers (first appearance in the logs):**

```
[    4.954222][    T1] initcall sm6125_tlmm_init+0x0/0x28 returned 0 after 0 usecs
[   29.620907][   T56] probe of 1400000.clock-controller returned 0 after 96813 usecs
[   29.737701][   T93] mmc1: SDHCI controller on 4744000.mmc [4744000.mmc] using ADMA 64-bit
[   29.835387][   T62] mmcblk1: mmc1:0001 3H6CAB 58.2 GiB
[   29.834692][   T56] printk: legacy console [ttyMSM0] enabled
[   14.371169][    T1] simple-framebuffer 5c000000.framebuffer: fb0: simplefb registered!
[   31.089827][    T1] Freeing unused kernel memory: 3328K
[   31.105517][    T1] Run /init as init process
```

**Then Android init crashes:**

```
[   31.839031][    T1] init: mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", ...) failed No such file or directory
[   31.862718][   T69] mmc0: Card stuck being busy! __mmc_poll_for_busy
[   31.862738][    T1] init: Init encountered errors starting first stage, aborting
[   32.079547][    T1] init: ... InitFatalReboot()
[   32.780000][    T1] reboot: Restarting system with command 'bootloader'
```

**Why:**

| Factor | Notes |
|------|------|
| Bootloader appends cmdline | Trailing `init=/init`, `root=PARTUUID=...`, `skip_initramfs` override our `init=/sbin/init` |
| Empty ramdisk | `build-bootimg.sh` produces an empty `initramfs.cpio.gz`; the kernel has no ramdisk `/init` to run |
| No Ubuntu rootfs | `out/rootfs.ext4` is built, but **userdata is still an Android partition** and was not flashed |
| Android init dependencies | Needs SELinux, the SD slot (mmc0), and many Android-only drivers |

**Conclusion:** kernel-side “it can boot” is done. Userspace needs **initramfs + a flashed Ubuntu rootfs**; see §7.

---

## 6. Log file index

| File | Size | Summary |
|------|------|----------|
| `boot-test-005614.txt` | 831 B | 2026-08-05: 24 flashes, all fastboot |
| `uart-20260807-224051.log` | 0 | Empty (not powered while capturing) |
| `uart-20260807-225955.log` | 12 B | Almost no data |
| `uart-20260807-231308.log` | 0 | Empty |
| `uart-20260807-232950.log` | 0 | Empty |
| `uart-20260807-233001.log` | 13 KB | **DTBO overlay failed** → fastboot |
| `uart-20260807-233836.log` | 53 KB | **TLMM SError panic** (before gpio-reserved fix) |
| `uart-20260807-234759.log` | 4 KB | Same panic (panic fragment only) |
| `uart-20260807-235321.log` | **1.2 MB** | **Full boot after the fix** + Android init reboot loop |
| `uart-live-*.log` | Tiny | Early test fragments |

### 6.1 How to read `uart-20260807-235321.log`

This file is the most useful. Search by keyword:

```bash
# Kernel version and cmdline
strings backup/ginkgo/logs/uart-20260807-235321.log | grep "Linux version"

# Whether TLMM passed
strings ... | grep sm6125_tlmm

# eMMC
strings ... | grep mmcblk1

# Userspace
strings ... | grep -E "Run /init|Init encountered|reboot:"

# Each boot is separated by "Linux version" or bootloader output
strings ... | grep -c "Linux version"   # about 5 times in this file
```

---

## 7. Kernel cmdline merge behavior

The bootloader **appends** stock parameters after the cmdline inside boot.img. Measured merge (from the `235321` log):

```
console=ttyMSM0,115200n8 earlycon=qcom_geni,0x4a90000 ...
root=/dev/disk/by-partlabel/userdata rootwait rw init=/sbin/init
...
root=PARTUUID=54dc1022-3967-9c82-4fd9-bf8e9137d187
skip_initramfs rootwait ro init=/init
```

**Effective rules:**

- Later `root=` / `init=` override earlier ones → the system runs **Android `/init`**
- `init=/sbin/init` (our Ubuntu) is ignored
- With an empty ramdisk, the kernel cannot find a usable early `/init` on the rootfs and mounts the Android partition

This is why the next stage must use a **non-empty initramfs (with an `/init` script)**.

---

## 8. Current status (2026-08-08)

### 8.1 Done

- [x] USB-TTL UART debug (115200, `/dev/ttyACM1`)
- [x] ABL loads the mainline DTB (empty DTBO)
- [x] Kernel earlycon + full driver probe
- [x] TLMM / GCC / SDHCI / UART4 / simplefb
- [x] eMMC identified (`mmcblk1`, 58.2 GiB)
- [x] Kernel frees memory and starts a userspace process

### 8.2 Not done

- [ ] initramfs (intercept `init=/init`)
- [ ] Flash `out/rootfs.ext4` to userdata
- [x] systemd starts normally (done later; see chronicle)
- [x] Real DRM/DSI display (magenta on screen 2026-08-17)
- [x] SPI touch NT36672A (tap events 2026-08-17)
- [ ] Wi-Fi and others

### 8.3 Current flash artifacts

| File | Notes |
|------|------|
| `out/boot-debug.img` | Includes gpio-reserved fix + initcall_debug |
| `out/boot.img` | Production boot (needs a rebuild to include the latest DTS) |
| `out/dtbo-empty.img` | Empty DTBO |
| `out/rootfs.ext4` | Ubuntu rootfs image (~2GB, **not flashed**) |

---

## 9. Next: get Linux into a real userspace

By priority:

### 9.1 Implement initramfs (P0)

Provide an `/init` script in the `boot.img` ramdisk that runs before mounting the Android root:

1. `mount` proc/sysfs/devtmpfs
2. Wait for `/dev/disk/by-partlabel/userdata`
3. `mount` ext4 on `/newroot`
4. `switch_root /newroot /sbin/init`

Need a new `scripts/build-initramfs.sh`, and change `build-bootimg.sh` (stop generating an empty ramdisk).

### 9.2 Flash the Ubuntu rootfs (P0)

```bash
fastboot flash userdata out/rootfs.ext4   # ⚠️ wipes userdata
```

### 9.3 Full flash flow (P0)

```bash
./scripts/make-empty-dtbo.sh
./scripts/build-bootimg.sh                # after initramfs is included
fastboot flash dtbo out/dtbo-empty.img
fastboot flash boot out/boot.img
fastboot flash vbmeta --disable-verification out/vbmeta.img
fastboot reboot
```

UART should show **systemd** logs, not `init: Init encountered errors`.

### 9.4 After userspace (P1+)

| Item | Notes |
|------|------|
| Serial login | `ttyMSM0` 115200, root/ginkgo |
| USB network | RNDIS for SSH |
| Display | **Mainline DRM image on screen** (2026-08-17) |
| Wi-Fi | Port the `wifi@c800000` node; ath10k firmware already in the rootfs |

---

## 10. Restore Android

```bash
./scripts/restore-android.sh
# or
fastboot flash boot backup/ginkgo/boot.img
fastboot flash dtbo backup/ginkgo/dtbo.img
fastboot flash vbmeta backup/ginkgo/vbmeta.img
fastboot reboot
```

If userdata was overwritten by Ubuntu, wipe data in recovery or use `fastboot -w`.

---

## 11. Related documents

| Document | Content |
|------|------|
| [`ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md) | Wiring diagram and test points |
| [`uart-debug-ginkgo.md`](uart-debug-ginkgo.md) | UART log-capture flow |
| [`mainline-boot-failure-analysis.md`](mainline-boot-failure-analysis.md) | Early 2026-08-05 fastboot analysis (superseded by §5.1 here) |
| [`mainline-ginkgo-porting-guide.md`](mainline-ginkgo-porting-guide.md) | Long-term porting roadmap |
| [`backup/ginkgo/DEBUG.md`](../backup/ginkgo/DEBUG.md) | pstore / debug without UART |
| [`README.md`](../README.md) | Build and flash quick start |

---

## 12. One-line conclusion

**Evening of 2026-08-07: after USB-TTL located and fixed DTBO overlay and TLMM `gpio-reserved-ranges`, the mainline kernel fully boots on ginkgo. The remaining block is the missing initramfs and Ubuntu rootfs, so the bootloader still starts Android init and reboots.**
