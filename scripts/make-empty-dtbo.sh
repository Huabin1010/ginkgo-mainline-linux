#!/usr/bin/env bash
# Create a zero-filled dtbo.img matching ginkgo partition size.
# Qualcomm ABL treats an all-zero dtbo as "no overlay" and uses DTB from boot.img.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out/dtbo-empty.img"

# ginkgo: fastboot getvar partition-size:dtbo → 0x1800000 (24 MiB)
SIZE="${DTBO_SIZE:-25165824}"

mkdir -p "$ROOT/out"
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=4096 count=$((SIZE / 4096)) status=none

echo "==> Wrote $OUT ($(numfmt --to=iec "$SIZE"))"
echo "    All zeros — ABL should skip DTBO overlay and use boot.img DTB only."
echo "Restore stock: fastboot flash dtbo $ROOT/backup/ginkgo/dtbo.img"
