#!/usr/bin/env bash
# Full build pipeline: kernel → boot.img → rootfs image.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

./scripts/build-kernel.sh
./scripts/build-bootimg.sh

if [[ ! -d out/rootfs/bin ]]; then
	PASS_FILE="$ROOT/root-password.md"
	if [[ -f "$PASS_FILE" ]]; then
		tr -d '\n' < "$PASS_FILE" | sudo -S ./scripts/build-rootfs.sh
	else
		sudo ./scripts/build-rootfs.sh
	fi
fi

PASS_FILE="$ROOT/root-password.md"
if [[ -f "$PASS_FILE" ]]; then
	tr -d '\n' < "$PASS_FILE" | sudo -S ./scripts/configure-rootfs.sh
	tr -d '\n' < "$PASS_FILE" | sudo -S ./scripts/build-rootfs-image.sh
else
	sudo ./scripts/configure-rootfs.sh
	sudo ./scripts/build-rootfs-image.sh
fi

echo "==> All artifacts:"
ls -lh out/Image.gz out/*.dtb out/boot.img out/rootfs.ext4 2>/dev/null
