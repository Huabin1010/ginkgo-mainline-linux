**Language:** English | [简体中文](zh-CN/flash-guide.md)

# Flash guide (Redmi Note 8 / ginkgo)

This is for **Xiaomi Redmi Note 8 only** (codename **ginkgo**, SM6125). Do not flash these images on willow, lavender, or any other phone.

You need an **unlocked bootloader**. Flashing **userdata** erases Android and everything on that partition.

## What to download

A GitHub [Release](https://github.com/Huabin1010/ginkgo-mainline-linux/releases) is meant for the **kernel side**:

| Asset | Flash to | Notes |
|-------|----------|--------|
| `boot.img` | `boot` | Mainline kernel + DTB + initramfs |
| `dtbo-empty.img` | `dtbo` | 24 MiB of zeros so ABL skips overlay and uses the DTB in `boot.img` |
| `rootfs.ext4.zst` | `userdata` after decompress | GitHub rejects a raw 2 GiB `rootfs.ext4`; unpack first. **Wipes userdata.** |
| `SHA256SUMS` | — | Check the files before flashing |

The Release rootfs is the **early minimal** image, not the later full GNOME desktop. After first boot run `passwd`. You can still build a newer image locally with `scripts/build-rootfs.sh`.

Stock `vbmeta.img` / Lineage `boot.img` are not in the Release — keep your own backup; see `backup/ginkgo/`.

First install: decompress the rootfs, flash userdata, then flash `boot` + empty `dtbo`. Later kernel updates are **boot + dtbo only**.

## Host tools

```bash
sudo apt install android-sdk-platform-tools   # adb, fastboot
# or:  export PATH="$HOME/.local/bin:$PATH"
```

Unlock check:

```bash
fastboot devices
fastboot getvar unlocked          # should be yes
fastboot getvar product           # must be ginkgo
```

If `product` is not `ginkgo`, stop.

## First install (wipes userdata)

Download the Release assets (example tag `v0.1.0`):

```bash
gh release download v0.1.0 --repo Huabin1010/ginkgo-mainline-linux --dir out
# or the browser: https://github.com/Huabin1010/ginkgo-mainline-linux/releases
cd out && sha256sum -c SHA256SUMS
sudo apt install zstd
zstd -d -f rootfs.ext4.zst          # → rootfs.ext4 (2 GiB)
```

Or build the rootfs on the PC yourself (newer / full desktop):

```bash
git clone https://github.com/Huabin1010/ginkgo-mainline-linux.git
cd ginkgo-mainline-linux
./scripts/setup-deps.sh
echo 'your-password' > rootfs-overlay/etc/ginkgo-root-password
chmod 600 rootfs-overlay/etc/ginkgo-root-password
./scripts/build-rootfs.sh
./scripts/configure-rootfs.sh
./scripts/build-rootfs-image.sh
```

Enter fastboot (`adb reboot bootloader`, or Vol− + Power). Then:

```bash
export PATH="$HOME/.local/bin:$PATH"

# 1. Ubuntu → userdata  (ERASES this partition)
fastboot flash userdata out/rootfs.ext4

# 2. Empty DTBO so ABL does not overlay the mainline DTB
fastboot flash dtbo out/dtbo-empty.img

# 3. Mainline kernel
fastboot flash boot out/boot.img

# 4. Disable verified boot (use your backup vbmeta, or any valid vbmeta.img)
fastboot flash vbmeta --disable-verification backup/ginkgo/vbmeta.img

fastboot reboot
```

`backup/ginkgo/vbmeta.img` is **not** in git (too easy to confuse with a full device dump). If you do not have it, any stock/Lineage `vbmeta.img` for ginkgo plus `--disable-verification` is enough.

## Kernel-only update (keep Ubuntu)

Phone already runs this rootfs; you only want a new kernel:

```bash
fastboot flash dtbo dtbo-empty.img
fastboot flash boot boot.img
fastboot reboot
```

Do **not** flash userdata again unless you intend to wipe.

From Ubuntu you can reboot to fastboot with `reboot-fastboot` (or `reboot bootloader`), then flash from the PC.

## After boot

USB RNDIS: phone `192.168.7.2`, host `192.168.7.1`.

```bash
./scripts/usb-connect.sh
# or: ssh -b 192.168.7.1 root@192.168.7.2
```

Serial (optional, **1.8 V only**): `ttyMSM0,115200n8`. Wiring: [UART guide](ginkgo-usb-ttl-uart.md).

## Restore Android

You need the Lineage/stock images you saved **before** flashing Linux (`boot.img`, `dtbo.img`, `vbmeta.img`). Those files are gitignored.

```bash
./scripts/restore-android.sh
```

If userdata already holds Ubuntu, wipe in TWRP or `fastboot -w` (this erases data), then flash a ROM.

## Do not

- Flash these images on a non-ginkgo device
- Put `rootfs.ext4` or passwords in a public Release
- Use 3.3 V / 5 V on the debug UART
- Re-enable `ufw` on the phone without allowing port 22 first
- `fastboot flash boot` unless you mean to write the partition (`fastboot boot boot.img` only tests once)

## Publish a Release (maintainers)

Interface: GitHub Releases REST API, wrapped by `gh`.

```bash
gh auth login          # once; git SSH is not enough for the API
./scripts/publish-release.sh v0.1.0
```

The script uploads `out/boot.img`, `out/dtbo-empty.img`, and `SHA256SUMS`. If `out/rootfs.ext4` exists, it compresses it to `rootfs.ext4.zst` (GitHub rejects a raw 2 GiB file) and uploads that too.
