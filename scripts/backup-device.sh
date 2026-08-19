#!/usr/bin/env bash
# Backup boot/vbmeta/dtbo from ginkgo via adb (requires root).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKUP="${BACKUP:-$ROOT/backup/ginkgo}"
export PATH="$HOME/.local/bin:$PATH"

mkdir -p "$BACKUP"

adb get-state >/dev/null 2>&1 || { echo "device not connected" >&2; exit 1; }

adb root >/dev/null 2>&1 || true
sleep 2

echo "==> Device info"
adb shell "getprop ro.build.display.id; getprop ro.lineage.version" | tee "$BACKUP/device-info.txt"

echo "==> boot ($(adb shell blockdev --getsize64 /dev/block/by-name/boot | tr -d '\r') bytes)"
adb shell "dd if=/dev/block/by-name/boot of=/data/local/tmp/boot_backup.img bs=4096"
adb pull /data/local/tmp/boot_backup.img "$BACKUP/boot.img"

echo "==> vbmeta"
adb shell "dd if=/dev/block/by-name/vbmeta of=/data/local/tmp/vbmeta_backup.img bs=4096"
adb pull /data/local/tmp/vbmeta_backup.img "$BACKUP/vbmeta.img"

echo "==> dtbo"
adb shell "dd if=/dev/block/by-name/dtbo of=/data/local/tmp/dtbo_backup.img bs=4096"
adb pull /data/local/tmp/dtbo_backup.img "$BACKUP/dtbo.img"

adb shell "cat /proc/cmdline" > "$BACKUP/cmdline.txt"
adb shell "ls -la /dev/block/by-name/" > "$BACKUP/partitions.txt"
adb shell "rm -f /data/local/tmp/*_backup.img" 2>/dev/null || true

echo "==> Done: $BACKUP"
ls -lh "$BACKUP"/*.img
md5sum "$BACKUP"/*.img
