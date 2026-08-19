#!/usr/bin/env bash
# Restore LineageOS boot (+ optional vbmeta/dtbo) from backup.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKUP="${BACKUP:-$ROOT/backup/ginkgo}"
export PATH="$HOME/.local/bin:$PATH"

[[ -f "$BACKUP/boot-repack.img" ]] || {
	echo "==> Creating boot-repack.img from partition backup..."
	unpack_bootimg --boot_img "$BACKUP/boot.img" --out /tmp/stock-boot-repack 2>/dev/null
	STOCK_CMD="console=ttyMSM0,115200n8 androidboot.console=ttyMSM0 earlycon=msm_serial_dm,0x4a90000 androidboot.hardware=qcom msm_rtb.filter=0x237 lpm_levels.sleep_disabled=1 service_locator.enable=1 swiotlb=1 androidboot.configfs=true androidboot.usbcontroller=4e00000.dwc3 loop.max_part=7 buildvariant=userdebug"
	mkbootimg --header_version 2 \
		--kernel /tmp/stock-boot-repack/kernel \
		--dtb /tmp/stock-boot-repack/dtb \
		--ramdisk /tmp/stock-boot-repack/ramdisk \
		--cmdline "$STOCK_CMD" \
		--pagesize 4096 --base 0 \
		--kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--second_offset 0xf00000 --tags_offset 0x100 --dtb_offset 0x1f00000 \
		--os_version 10.0.0 --os_patch_level 2022-01 \
		-o "$BACKUP/boot-repack.img"
}

BOOTIMG="$BACKUP/boot-repack.img"
[[ -f "$BOOTIMG" ]] || BOOTIMG="$BACKUP/boot.img"

echo "==> Flashing boot ($BOOTIMG)"
fastboot flash boot "$BOOTIMG"

if [[ -f "$BACKUP/vbmeta.img" ]]; then
	echo "==> Flashing vbmeta"
	fastboot flash vbmeta "$BACKUP/vbmeta.img"
fi

if [[ -f "$BACKUP/dtbo.img" ]]; then
	echo "==> Flashing dtbo"
	fastboot flash dtbo "$BACKUP/dtbo.img"
fi

echo "==> Reboot"
fastboot reboot
echo "If boot loop persists, wipe userdata in recovery or: fastboot -w"
