**Language:** English | [简体中文](zh-CN/ginkgo-journey-to-remote-ssh.md)

# Redmi Note 8 (ginkgo) mainline Linux porting journey

> Device: Xiaomi Redmi Note 8 (`ginkgo`, serial `<serial>`)  
> Platform: Qualcomm SM6125  
> Goal: mainline Linux + Ubuntu 26.04, remote SSH debug over USB  
> Document date: 2026-08-08  
> Milestone: **USB RNDIS + SSH is working** (`ssh root@192.168.7.2`; password is in `root-password.md` / `$GINKGO_ROOT_PASSWORD`)  
> Later: **display showed an image 2026-08-17**, **touch produced points 2026-08-17** — see [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md), [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)

This is not a technical manual. It is a complete record of the path **from “nothing visible” to “SSH into the phone from a PC”**: the pits we hit, what kept us going, and what we were thinking at each stage.

Finer kernel-panic analysis is in [`ginkgo-bringup-journal.md`](./ginkgo-bringup-journal.md); wiring and parts are in [`ginkgo-usb-ttl-uart.md`](./ginkgo-usb-ttl-uart.md).

---

## 0. What we were actually doing

One sentence: **run mainline Linux on a Redmi Note 8, not Lineage/Android.**

This is not flashing a Magisk module. It is building a chain from zero:

```
mainline kernel boot.img
    → initramfs mounts the Ubuntu rootfs on userdata
    → systemd starts normally
    → USB RNDIS virtual NIC
    → SSH login from the PC
```

Screen black, no WiFi, no touch — before any of that worked, we needed a **debug channel that did not depend on the display**. The end of that channel is the `ssh root@192.168.7.2` verified in the early hours of 2026-08-08.

---

## 1. Stage one: groping in the dark (2026-08-05)

### 1.1 Flash and you are in fastboot, with no feedback

The earliest state was extremely frustrating: flash the built `boot.img`, the phone lights up briefly, then a few seconds later it is back in fastboot. No adb, no picture, no readable output.

The only suspects we could pin down were at the config layer:

| Problem | Consequence |
|---------|-------------|
| `CONFIG_ARM64_VA_BITS_52` | Kryo 260 does not support 52-bit VA |
| `qcom,board-id` written as `0x16` | this unit is actually `0x22`; ABL may refuse the image |
| `CONFIG_SM_GCC_6125` not enabled | global clocks never come up; peripherals all dead |
| boot.img pack format wrong | header v2 + standalone DTB must match stock |

We fixed these one by one and **still returned to fastboot**. After 24 consecutive tests, the log had only one cold line: `adb=none fb=<serial> fastboot`.

The strongest feeling then: **we were punching blind.** Did the kernel run at all? At which step did ABL refuse? Completely unknown.

### 1.2 Realization: no serial console means no eyes

The project cmdline had long contained:

```
console=ttyMSM0,115200n8
earlycon=qcom_geni,0x4a90000
```

Without a physical UART cable those parameters might as well not exist. The screen usually stays dark in early boot, and adb is even less likely to come up.

So we started seriously researching a **USB-TTL plan** — the turning point from “guessing” to “observable”.

---

## 2. Stage two: soldering on eyes (2026-08-05 ~ 08-07)

### 2.1 Finding test points: nearly soldered the wrong pads

Many online tutorials put a red box around the **EDL test points** (short them to enter 9008 flashing). Those are **not** debug UART. If you solder the EDL pads as “left TX, right RX”, you can enter EDL and you will never see a boot log.

The real UART is in the LLDM516 schematic:

| Test point | Signal | Notes |
|------------|--------|-------|
| **TP0003** | phone TX (GPIO16) | to module RX |
| **TP0012** | phone RX (GPIO17) | to module TX |
| **GND** | ground | screw hole or a large ground pour |

The pads are only **0.3–0.8 mm**; Dupont wires held by hand will not stay put. We finally soldered magnet wire:

- yellow → TP0003 (phone TX)
- green → TP0012 (phone RX)
- orange → GND

Level must be **1.8V**. If the module is switched to 3.3V/5V, at best you read nothing; at worst you damage the SoC.

### 2.2 Serial finally had characters

Plug in the CH340 (`1a86:55d3`), run `python3 scripts/uart-monitor.py`, power on — **first time we saw ABL and kernel printk**.

Empty logs, garbage, wrong baud — we hit all of that. After 2026-08-07 23:00 the log finally landed stably in `backup/ginkgo/logs/`.

**What this moment meant:** from then on every panic, every fastboot, every systemd line was on record. Every later fix stands on that serial console.

---

## 3. Stage three: kernel clears the bar (late night 2026-08-07)

### 3.1 DTBO overlay: ABL stopped us before the kernel

First useful UART log (`uart-20260807-233001.log`):

```
ApplyOverlay: ufdt apply overlay failed
Error: Dtb overlay failed
Launching fastboot
```

The stock `dtbo.img` overlay is structurally incompatible with the mainline DTB. Fix: **flash a 24 MB all-zero `dtbo-empty.img`**, so ABL skips overlay and uses only the DTB embedded in boot.img.

### 3.2 TLMM panic: death at 5.76 seconds

After bypassing DTBO the kernel started, then at about 5.76 s:

```
SError Interrupt on CPU5
pc : gpiochip_add_data_with_key
Kernel panic - not syncing
sm6125_tlmm_probe
```

We suspected the `soc@0` address format; after changing to 2-cell the **panic remained**. `fdtget` confirmed TLMM `reg` was correct; disassembly located a fault reading the **GPIO0** direction register.

Root cause: `gpio-reserved-ranges` was wrong, so the kernel touched QUP-dedicated pins:

```dts
/* 错误 */
gpio-reserved-ranges = <22 2>, <28 6>;

/* 正确 */
gpio-reserved-ranges = <0 4>, <30 4>;
```

After the fix, the log showed for the first time:

```
sm6125_tlmm_init returned 0
mmcblk1: mmc1:0001 3H6CAB 58.2 GiB
printk: legacy console [ttyMSM0] enabled
Run /init as init process
```

**The kernel was alive.**

### 3.3 Android init kicked us out

But userspace was **Android `/init`**, not Ubuntu. The bootloader appended at the end of cmdline:

```
skip_initramfs rootwait ro init=/init
```

overriding our `init=/sbin/init`. Android init failed to mount selinuxfs, timed out waiting for mmc0 SD, then:

```
Init encountered errors starting first stage, aborting
reboot: Restarting system with command 'bootloader'
```

The kernel could start, but we could not enter our own system — the most frustrating place to end stage three.

---

## 4. Stage four: actually putting Linux on the phone (2026-08-07 ~ 08-08)

### 4.1 initramfs: taking init back

We built a static aarch64 `/init` (`initramfs/init.c`). In the ramdisk it:

1. Mounts proc/sys/dev
2. Waits for and mounts userdata (ext4)
3. Deploys `rootfs-overlay/` onto `/newroot`
4. `switch_root` to `/sbin/init`

So even if the bootloader appends `init=/init`, the ramdisk `/init` runs first and switches the root to Ubuntu.

### 4.2 Ubuntu rootfs on userdata

`scripts/build-rootfs.sh` debootstraps Ubuntu 26.04 arm64 minimal and flashes it to the userdata partition (`mmcblk0p87`). Serial showed **systemd** logs for the first time, not `init: Init encountered errors`.

### 4.3 Interlude: recovery could not mount userdata

We tried mounting userdata in TWRP recovery to incrementally update the overlay. It failed because userdata ext4 uses a newer `metadata_csum_seed`, and the recovery kernel is too old to mount it.

**Workaround:** pack `rootfs-overlay/` into **initramfs**; each boot `/init` deploys it onto userdata. Flashing only `boot.img` updates userspace scripts; no need to reflash a 2 GB rootfs.

---

## 5. Stage five: USB remote debug — the longest chain (2026-08-08)

Goal: SSH into the phone from the PC over the same micro-USB data cable that was already plugged in, no longer depending on the soldered TTL wires.

### 5.1 Layer one: dwc3 would not start

Serial:

```
platform 4e00000.usb: deferred probe pending
dwc3: failed to initialize core
```

| Problem | Fix |
|---------|-----|
| `sm6125.dtsi` usb3 address format | change to 2-cell + add `resets` |
| Wrong PHY compatible | `msm8996-qusb2-phy` → `qcom,sm6125-qusb2-phy` |
| dwc3-qcom interconnect failed | skip when there is no interconnect (`-ENODEV`) |
| **PHY driver not built into the kernel** | `CONFIG_PHY_QCOM_QUSB2=m` → **`=y`** |

The last item was decisive: with `CONFIG_PHY_QCOM_QUSB2=m` the module never loaded, the PHY never probed, and dwc3 stayed deferred.

### 5.2 Layer two: RNDIS service stalled for 90 seconds

After USB hardware was fixed, the PC still saw no NIC. Serial showed `usb-gadget-rndis.service` waiting on `local-fs.target`, and `local-fs` was stuck on **persist partition mount** (80+ mmc partitions, udev extremely slow).

We briefly thought “the USB cable is not plugged in” — recovery adb had been working the whole time; the cable was in. **The gadget service never actually ran.**

Fix:

- `usb-gadget-rndis.service` drop `After=local-fs.target`, change to `WantedBy=sysinit.target`
- **Take persist out of fstab**, mount it later via `persist-mount.service`

### 5.3 Layer three: emergency mode blocked SSH

With persist in fstab, systemd by default waits **90 seconds** for `by-partlabel/persist` → `local-fs.target` fails → **emergency mode** → `ssh.service` never starts.

Serial stopped at:

```
Enter root password for system maintenance
(or press Control-D to continue):
```

USB RNDIS could ping `192.168.7.2`, but `ssh` reported `Connection refused`.

After the fix, serial showed:

```
Reached target multi-user.target
Started ssh.service
ginkgo login:
```

### 5.4 Layer four: the password was right and SSH still refused

Once the SSH port was open, both `ginkgo` and `$GINKGO_ROOT_PASSWORD` got `Permission denied`. The user had not changed the password, so it was easy to misread as “password lost”.

Real cause: **Ubuntu 26.04 / OpenSSH defaults to `PermitRootLogin prohibit-password`** — root cannot log in with a password, only a public key. Local serial login is unaffected; SSH refuses every password.

Fix:

- `/etc/ssh/sshd_config.d/99-ginkgo.conf`: `PermitRootLogin yes`
- `ensure-root-password.service`: sync the root password from `/etc/ginkgo-root-password` to `$GINKGO_ROOT_PASSWORD`

---

## 6. Finale: connected (2026-08-08 01:20)

After recovery flashed the latest `boot.img`, on the PC:

```
PING 192.168.7.2 — 0% 丢包
ssh root@192.168.7.2
```

```text
SSH_OK
ginkgo
uptime: up 2 min
ssh.service: active
PermitRootLogin yes
```

`lsusb` shows `1d6b:0104 Linux Foundation Multifunction Composite Gadget`.

**At this moment the project had its first remote debug channel that did not depend on soldered serial wires.**

---

## 7. Two USB cables, two jobs

Only later did this become completely clear (we had mixed them up):

| Connection | Device | Role |
|------------|--------|------|
| Phone **micro-USB** → PC | `1d6b:0104` RNDIS | network + SSH + recovery adb |
| **TTL test points** → CH340 | `1a86:55d3` serial | boot log only; unrelated to USB data |

The micro-USB data cable was **always plugged in**. If the PC cannot see the phone, the cable is not missing — **the gadget service is not running**.

---

## 8. Fix-chain overview (from blind to SSH)

```
[invisible] fastboot loop
    ↓ USB-TTL solder + uart-monitor
[visible] ABL / kernel log
    ↓ empty dtbo + gpio-reserved-ranges
[kernel alive] TLMM / eMMC / ttyMSM0
    ↓ initramfs + Ubuntu rootfs
[userspace] systemd starts
    ↓ DTS/driver/CONFIG_PHY_QCOM_QUSB2=y
[USB hardware] dwc3 + qusb2 phy probe succeeds
    ↓ gadget starts early + persist out of fstab
[USB network] RNDIS 192.168.7.2
    ↓ PermitRootLogin + ensure-root-password
[remote debug] ssh root@192.168.7.2 ✓
```

---

## 9. Key files and scripts (for later use)

| Path | Role |
|------|------|
| `out/boot.img` | includes initramfs overlay deploy + USB/SSH fixes |
| `config/ginkgo.fragment` | kernel fragment (includes `CONFIG_PHY_QCOM_QUSB2=y`) |
| `rootfs-overlay/` | USB gadget, fstab, sshd, password sync, etc. |
| `initramfs/init.c` | mount userdata + deploy overlay |
| `scripts/uart-monitor.py` | serial monitor |
| `scripts/flash-linux-boot.sh` | recovery flash boot (`FLASH_ROOTFS=0` flashes boot only) |
| `scripts/host-usb-connect.sh` | PC networking + SSH |
| `root-password.md` | root password `$GINKGO_ROOT_PASSWORD` |

### Everyday PC connection

```bash
# 网卡出现后
nmcli connection up ginkgo-usb
# 或
./scripts/host-usb-connect.sh

ssh root@192.168.7.2    # 密码见 `$GINKGO_ROOT_PASSWORD`
```

### Update userspace overlay only (when recovery can mount userdata)

```bash
./scripts/update-rootfs-via-recovery.sh
```

In most cases **flashing boot only** is enough (the overlay is in initramfs).

---

## 10. What is still unfinished

USB SSH is a milestone, not the end:

| Priority | Item | Status |
|----------|------|--------|
| P2 | Display (DRM/DSI) | **done** (2026-08-17, see [display complete record](./ginkgo-display-complete-2026-08-17.md)) |
| P3 | WiFi (ath10k + ICNSS) | firmware in rootfs, node still to add |
| P3 | Touch (SPI Novatek) | no mainline driver |
| — | Drop debug cmdline | informal parameters such as `loglevel=8` can be tightened |
| — | Keep serial if possible | SSH is too late on a kernel panic |

---

## 11. A few words

This road was much longer than expected.

It started as **24 unanswered fastboot cycles**; after soldering serial, a **5.76-second TLMM panic** kept coming back; the kernel entered, then **Android init kicked us to the bootloader**; systemd finally came up, and USB was a **whole deferred-probe chain**; RNDIS pinged, **SSH refused**; the port opened, the password got **Permission denied** — and the password had never changed: OpenSSH by default will not let root log in with a password.

Behind every “just one more inch” was a blocked dependency. Serial let us see the dependencies; initramfs let us take the system back; USB gadget let us leave the soldered wires; sshd config let us type the first remote command on the PC.

**In the early hours of 2026-08-08, `ssh root@192.168.7.2` returned `ginkgo` — this line was connected.**

---

## 12. Related documents

| Document | Content |
|----------|---------|
| [`ginkgo-display-complete-2026-08-17.md`](./ginkgo-display-complete-2026-08-17.md) | **Full display-image record and lessons** |
| [`ginkgo-bringup-journal.md`](./ginkgo-bringup-journal.md) | Technical log of kernel boot issues (through the Android init stage) |
| [`ginkgo-usb-ttl-uart.md`](./ginkgo-usb-ttl-uart.md) | UART wiring, parts, soldering |
| [`mainline-ginkgo-porting-guide.md`](./mainline-ginkgo-porting-guide.md) | Long-term porting roadmap |
| [`mainline-boot-failure-analysis.md`](./mainline-boot-failure-analysis.md) | Early 2026-08-05 fastboot analysis |
| [`uart-debug-ginkgo.md`](./uart-debug-ginkgo.md) | Serial log-capture procedure |
| [`../README.md`](../README.md) | Build and flash quick start |
