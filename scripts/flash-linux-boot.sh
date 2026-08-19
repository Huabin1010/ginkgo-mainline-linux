#!/usr/bin/env bash
# Flash mainline Linux: optional userdata rootfs + empty dtbo + boot.img.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTBO="$ROOT/out/dtbo-empty.img"
BOOT="${BOOT_IMG:-$ROOT/out/boot.img}"
ROOTFS="${ROOTFS_IMG:-$ROOT/out/rootfs.ext4}"
FLASH_ROOTFS="${FLASH_ROOTFS:-1}"

[[ -f "$BOOT" ]] || { echo "missing $BOOT — run scripts/build-bootimg.sh" >&2; exit 1; }
[[ -f "$DTBO" ]] || "$ROOT/scripts/make-empty-dtbo.sh"

flash_recovery() {
	echo "==> Flashing via recovery adb"
	if [[ "$FLASH_ROOTFS" == "1" ]]; then
		[[ -f "$ROOTFS" ]] || { echo "missing $ROOTFS" >&2; exit 1; }
		echo "==> Pushing rootfs (this may take several minutes)..."
		adb push "$ROOTFS" /tmp/rootfs.ext4
		echo "==> Writing userdata (erases Android data on userdata)"
		adb shell "dd if=/tmp/rootfs.ext4 of=/dev/block/by-name/userdata bs=4M && sync"
		adb shell "rm /tmp/rootfs.ext4"
	fi
	adb push "$DTBO" /tmp/dtbo.img
	adb push "$BOOT" /tmp/boot.img
	adb shell "dd if=/tmp/dtbo.img of=/dev/block/by-name/dtbo bs=4M && sync"
	adb shell "dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4M && sync"
	adb shell "rm /tmp/dtbo.img /tmp/boot.img"
	echo "==> Rebooting"
	adb reboot
}

flash_fastboot() {
	echo "==> Flashing via fastboot"
	if [[ "$FLASH_ROOTFS" == "1" ]]; then
		[[ -f "$ROOTFS" ]] || { echo "missing $ROOTFS" >&2; exit 1; }
		fastboot flash userdata "$ROOTFS"
	fi
	fastboot flash dtbo "$DTBO"
	fastboot flash boot "$BOOT"
	fastboot reboot
}

if fastboot devices | grep -q .; then
	flash_fastboot
elif adb devices | awk 'NR>1 && $2=="recovery"{found=1} END{exit !found}'; then
	flash_recovery
else
	echo "error: no fastboot or recovery device" >&2
	exit 1
fi
