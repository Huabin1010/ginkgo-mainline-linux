#!/usr/bin/env bash
# Standard mainline boot test: empty dtbo + debug boot.img, then reboot.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTBO="$ROOT/out/dtbo-empty.img"
BOOT="${BOOT_IMG:-$ROOT/out/boot-debug.img}"

[[ -f "$BOOT" ]] || { echo "missing $BOOT — run scripts/build-debug-boot.sh" >&2; exit 1; }

"$ROOT/scripts/make-empty-dtbo.sh"

if fastboot devices | grep -q .; then
	echo "==> Flashing via fastboot"
	fastboot flash dtbo "$DTBO"
	fastboot flash boot "$BOOT"
	echo "==> Rebooting (keep uart-monitor running, do not press volume keys)"
	fastboot reboot
	exit 0
fi

if adb devices | awk 'NR>1 && $2=="recovery"{found=1} END{exit !found}'; then
	echo "==> Flashing via recovery adb"
	adb push "$DTBO" /tmp/dtbo.img
	adb push "$BOOT" /tmp/boot.img
	adb shell "dd if=/tmp/dtbo.img of=/dev/block/by-name/dtbo bs=4M && sync"
	adb shell "dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4M && sync"
	adb shell "rm /tmp/dtbo.img /tmp/boot.img"
	echo "==> Rebooting"
	adb reboot
	exit 0
fi

echo "error: no fastboot or recovery device" >&2
exit 1
