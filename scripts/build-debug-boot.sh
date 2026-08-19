#!/usr/bin/env bash
# Build boot-debug.img: kernel + DTB with verbose printk, cmdline tuned for UART spam.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/env.sh
source "$ROOT/scripts/env.sh"

DEBUG_FRAGMENT="$ROOT/config/ginkgo-debug.fragment"
[[ -f "$DEBUG_FRAGMENT" ]] || { echo "missing $DEBUG_FRAGMENT" >&2; exit 1; }

echo "==> Merging debug kernel fragment"
"$KERNEL_SRC/scripts/kconfig/merge_config.sh" -m -O "$KBUILD_OUTPUT" \
	"$KBUILD_OUTPUT/.config" "$DEBUG_FRAGMENT"
make -C "$KERNEL_SRC" O="$KBUILD_OUTPUT" olddefconfig

echo "==> Building debug kernel + DTB"
DEBUG_BOOT=1 "$ROOT/scripts/build-kernel.sh"

echo "==> Packing boot-debug.img"
DEBUG_BOOT=1 "$ROOT/scripts/build-bootimg.sh"

echo "==> Done: $ROOT/out/boot-debug.img"
echo "Flash: adb push out/boot-debug.img /tmp/boot.img && adb shell 'dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4M && sync'"
