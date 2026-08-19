#!/usr/bin/env bash
# Flash mainline boot.img to ginkgo (requires fastboot + unlocked bootloader).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOOTIMG="${BOOTIMG:-$ROOT/out/boot.img}"

export PATH="$HOME/.local/bin:$PATH"

[[ -f "$BOOTIMG" ]] || { echo "missing $BOOTIMG — run scripts/build-bootimg.sh" >&2; exit 1; }

echo "==> Device:"
fastboot devices

echo ""
echo "WARNING: This replaces the boot partition."
echo "Original Android/Lineage boot will be overwritten."
echo "Press Ctrl+C to abort, Enter to continue..."
read -r _

echo "==> Flashing boot"
fastboot flash boot "$BOOTIMG"

if [[ -f "$ROOT/out/vbmeta.img" ]]; then
	echo "==> Flashing vbmeta (verification disabled)"
	fastboot flash vbmeta --disable-verification "$ROOT/out/vbmeta.img"
else
	echo "==> Skipping vbmeta (create out/vbmeta.img or run: fastboot flash vbmeta --disable-verification vbmeta.img)"
fi

echo "==> Reboot"
fastboot reboot
