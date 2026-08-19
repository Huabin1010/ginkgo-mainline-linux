#!/usr/bin/env bash
# Build boot.img for ginkgo (Qualcomm Android boot image v2 format).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/env.sh
source "$ROOT/scripts/env.sh"

OUT="$ROOT/out"
KERNEL="$OUT/Image.gz"
DTB="$OUT/$DTB_NAME"
RAMDISK="${RAMDISK:-$OUT/initramfs.cpio.gz}"
# Match stock LineageOS boot header (unpack_bootimg from backup)
PAGESIZE=4096
BASE=0x00000000
KERNEL_OFFSET=0x00008000
RAMDISK_OFFSET=0x01000000
SECOND_OFFSET=0x00f00000
TAGS_OFFSET=0x00000100
DTB_OFFSET=0x01f00000
HEADER_VERSION=2
OS_VERSION=10.0.0
OS_PATCH_LEVEL=2022-01

# console=tty0 last → fbcon is /dev/console so kernel boot log appears on
# the panel once DRM fbdev registers (~4s). UART still gets a copy.
# Old "no tty0" comment was from simplefb vs async SDHCI; eMMC now probes
# at ~2.5s (before DRM) and the SD slot (sdhc_2) is disabled.
if [[ "${DEBUG_BOOT:-}" == "1" ]]; then
	BOOTIMG="$OUT/boot-debug.img"
	CMDLINE="${CMDLINE:-console=ttyMSM0,115200n8 console=tty0 earlycon=qcom_geni,0x4a90000 keep_bootcon ignore_loglevel loglevel=8 clk_ignore_unused fw_devlink.sync_state=disabled printk.devkmsg=on printk.time=1 initcall_debug debug androidboot.hardware=qcom root=/dev/disk/by-partlabel/userdata rootwait rw init=/init}"
else
	BOOTIMG="$OUT/boot.img"
	CMDLINE="${CMDLINE:-console=ttyMSM0,115200n8 console=tty0 earlycon=qcom_geni,0x4a90000 keep_bootcon ignore_loglevel loglevel=8 clk_ignore_unused fw_devlink.sync_state=disabled androidboot.hardware=qcom root=/dev/disk/by-partlabel/userdata rootwait rw init=/init}"
fi

[[ -f "$KERNEL" ]] || { echo "missing $KERNEL — run scripts/build-kernel.sh first" >&2; exit 1; }
[[ -f "$DTB" ]] || { echo "missing $DTB — run scripts/build-kernel.sh first" >&2; exit 1; }

"$ROOT/scripts/build-initramfs.sh"

echo "==> Packing $(basename "$BOOTIMG") (header v2, separate DTB @ $DTB_OFFSET)"
mkbootimg \
	--header_version "$HEADER_VERSION" \
	--kernel "$KERNEL" \
	--dtb "$DTB" \
	--ramdisk "$RAMDISK" \
	--cmdline "$CMDLINE" \
	--pagesize "$PAGESIZE" \
	--base "$BASE" \
	--kernel_offset "$KERNEL_OFFSET" \
	--ramdisk_offset "$RAMDISK_OFFSET" \
	--second_offset "$SECOND_OFFSET" \
	--tags_offset "$TAGS_OFFSET" \
	--dtb_offset "$DTB_OFFSET" \
	--os_version "$OS_VERSION" \
	--os_patch_level "$OS_PATCH_LEVEL" \
	-o "$BOOTIMG"

unpack_bootimg --boot_img "$BOOTIMG" --out /tmp/verify-boot 2>&1 | head -15
ls -lh "$BOOTIMG"
echo "Flash: fastboot flash boot $BOOTIMG"
