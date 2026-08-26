# ginkgo-mainline-linux

**Language:** English | [简体中文](README.zh-CN.md)

Mainline Linux bring-up for **Xiaomi Redmi Note 8** (codename **ginkgo**, Qualcomm **SM6125**).

This tree builds Linux 7.0 + an Ubuntu 26.04 arm64 rootfs, then flashes them onto the phone. On **Tianma NT36672A** units, display, touch, Wi-Fi, GNOME, Adreno 610, and Docker already work. The default `boot.img` is that Tianma build: DSI comes up with `power mode 0x9c` / `LANE_STATUS 0x1f00`, fbcon paints the kernel log, and Type-C stays USB gadget (RNDIS/SSH) unless you switch it to host.

| Stage | Goal | Status |
|-------|------|--------|
| P0 | UART boot log | Partial (`ttyMSM0` still deferred) |
| P1 | eMMC rootfs + systemd | Done |
| P2 | USB RNDIS + SSH | Done |
| P3 | DRM / DSI display + fbcon | Done |
| P4 | SPI touch (NT36672A) | Done |
| P5 | Wi-Fi (WCN3990 / ath10k_snoc) | Done |
| P6 | Ubuntu GNOME + Adreno 610 | Done |
| P7 | Docker Engine | Done |

**Flash a Release image:** [English](docs/flash-guide.md) · [中文](docs/zh-CN/flash-guide.md)

Hardware notes, UART wiring, and the full bring-up story live under [`docs/`](docs/README.md). Chinese originals are in [`docs/zh-CN/`](docs/zh-CN/README.md).

## Panel SKUs (Tianma vs Huaxing)

ginkgo shipped with more than one display. Same DSI connector, different panel IC and init sequence.

| Image | Panel | Status |
|-------|--------|--------|
| `boot.img` (default) | **Tianma NT36672A** 1080×2340 | Supported. Display, fbcon, SPI touch, GNOME desktop. |
| `boot-huaxing.img` | **Huaxing / CSOT FT8719** | Display now lights up (pclk locked to the proven Tianma 183012 kHz). Touch is still off. Not a supported desktop image. |

A Huaxing phone stays black on the Tianma `boot.img`. Flash `boot-huaxing.img` instead (prefer `fastboot boot` once before writing the partition).

Type-C is USB gadget by default so SSH/RNDIS keep working. Keyboard/mouse needs an OTG adapter and, on the phone:

```bash
ginkgo-usb-host.sh
```

SSH drops until you reboot (or switch the role back to `device` and restart `usb-gadget-rndis`).

On stock Android you can see which panel you have:

```bash
dmesg | grep -iE 'tianma|huaxing|ft8719|nt36672a|TP info'
```

If a panel does not light up, open a [GitHub issue](https://github.com/Huabin1010/ginkgo-mainline-linux/issues/new) and attach the **boot log** (from power-on through DRM/panel probe). That is enough; no need for photos of the whole session.

**UART (best if the screen is black).** 1.8 V only. Phone TX **TP0003** → adapter RX, phone RX **TP0012** → adapter TX, plus GND. Start capture **before** power-on. Wiring: [UART guide](docs/ginkgo-usb-ttl-uart.md).

```bash
picocom -b 115200 /dev/ttyUSB0 | tee uart.log
```

**USB SSH (if the kernel reaches userspace).** USB RNDIS, then `root@192.168.7.2`. The root password is not in this repo.

```bash
ssh root@192.168.7.2 'dmesg' > dmesg.txt
```

Paste or attach `uart.log` / `dmesg.txt`. These lines matter most:

```text
panel init complete
power mode readback
dsi_err
```

## Quick start

```bash
# 1. Host packages (once, needs sudo)
./scripts/setup-deps.sh

# 2. Fetch kernel sources (once)
./scripts/setup-kernel.sh

# 3. Build kernel + ginkgo DTB
./scripts/build-kernel.sh

# 4. Pack boot.img
./scripts/build-bootimg.sh

# 5. Build Ubuntu rootfs
./scripts/build-rootfs.sh
./scripts/configure-rootfs.sh
./scripts/build-rootfs-image.sh

# 6. Flash (device in fastboot)
./scripts/flash-boot.sh
# First install only — this wipes userdata:
# fastboot flash userdata out/rootfs.ext4
```

Kernel sources (`linux/`) and build outputs (`out/`) are not in git. Clone and compile them with the scripts above.

## Flash order

Flash rootfs first, then boot, then disable vbmeta verification.

```bash
export PATH="$HOME/.local/bin:$PATH"

# 1. fastboot (adb reboot bootloader, or the key combo)

# 2. Ubuntu rootfs → userdata (wipes that partition)
fastboot flash userdata out/rootfs.ext4

# 3. Mainline kernel
fastboot flash boot out/boot.img

# 4. Disable verified boot
fastboot flash vbmeta --disable-verification vbmeta.img

# 5. Reboot
fastboot reboot
```

Login: serial `ttyMSM0` or SSH over USB RNDIS (`root@192.168.7.2`). The root password is **not** in this repo. Put it in the gitignored file `rootfs-overlay/etc/ginkgo-root-password`, or export `GINKGO_ROOT_PASSWORD`.

Serial console: `ttyMSM0,115200n8` @ `0x4a90000` (1.8 V only). Wiring: [UART guide](docs/ginkgo-usb-ttl-uart.md).

## Layout

```
config/           Kernel config fragment
scripts/          Build, flash, UART, and host USB helpers
docs/             English technical notes
docs/zh-CN/       Chinese originals
firmware/ginkgo/  Device-extracted firmware
reference/        Downstream + mainline DTS / driver excerpts
rootfs-overlay/   Files copied into the Ubuntu image
backup/ginkgo/    Partition map and restore notes (images are gitignored)
host/             Host NetworkManager snippet for USB RNDIS
```

## Documentation

| Topic | English | 中文 |
|-------|---------|------|
| Flash a Release | [flash guide](docs/flash-guide.md) | [刷机教程](docs/zh-CN/flash-guide.md) |
| Doc index | [docs/README.md](docs/README.md) | [docs/zh-CN/README.md](docs/zh-CN/README.md) |
| Full chronicle | [bring-up chronicle](docs/ginkgo-mainline-bringup-chronicle.md) | [中文](docs/zh-CN/ginkgo-mainline-bringup-chronicle.md) |
| Porting handbook | [porting guide](docs/mainline-ginkgo-porting-guide.md) | [中文](docs/zh-CN/mainline-ginkgo-porting-guide.md) |
| Display | [2026-08-17](docs/ginkgo-display-complete-2026-08-17.md) | [中文](docs/zh-CN/ginkgo-display-complete-2026-08-17.md) |
| fbcon | [2026-08-18](docs/ginkgo-fbcon-boot-2026-08-18.md) | [中文](docs/zh-CN/ginkgo-fbcon-boot-2026-08-18.md) |
| Touch | [2026-08-17](docs/ginkgo-touch-complete-2026-08-17.md) | [中文](docs/zh-CN/ginkgo-touch-complete-2026-08-17.md) |
| Wi-Fi | [2026-08-18](docs/ginkgo-wifi-complete-2026-08-18.md) | [中文](docs/zh-CN/ginkgo-wifi-complete-2026-08-18.md) |
| Desktop / GPU | [Ubuntu](docs/ginkgo-ubuntu-desktop-2026-08-19.md) · [Adreno 610](docs/ginkgo-gpu-desktop-2026-08-19.md) | [中文](docs/zh-CN/ginkgo-ubuntu-desktop-2026-08-19.md) |
| Docker | [2026-08-19](docs/ginkgo-docker-2026-08-19.md) | [中文](docs/zh-CN/ginkgo-docker-2026-08-19.md) |

## Environment

See `scripts/env.sh`:

| Variable | Default | Meaning |
|----------|---------|---------|
| `KERNEL_TAG` | v7.0 | Kernel version |
| `KBUILD_OUTPUT` | `out/kernel` | Build tree |
| `JOBS` | `nproc` | Parallel jobs |
| `GINKGO_ROOT_PASSWORD` | *(unset)* | Root password for the image; otherwise read from the overlay file |

## License

Build scripts and original documentation in this repository are [GPL-2.0-only](LICENSE), same as the Linux kernel. Downstream excerpts under `reference/` keep their upstream licenses. Device firmware blobs are proprietary; they are provided only for running Linux on the device they were extracted from.
