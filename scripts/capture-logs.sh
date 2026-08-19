#!/usr/bin/env bash
# Capture kernel logs after failed mainline boot.
# Works in several scenarios — see backup/ginkgo/DEBUG.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$ROOT/backup/ginkgo/logs}"
export PATH="$HOME/.local/bin:$PATH"

mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)

echo "==> Capture $TS -> $OUT"

# 1. adb available (partial boot or back in Android)
if adb get-state >/dev/null 2>&1; then
	echo "--- adb dmesg ---"
	adb shell dmesg 2>/dev/null | tee "$OUT/dmesg-$TS.txt" || true

	echo "--- adb last kmsg ---"
	adb shell "cat /proc/last_kmsg 2>/dev/null" | tee "$OUT/last_kmsg-$TS.txt" || true

	echo "--- pstore ---"
	adb root >/dev/null 2>&1 || true
	sleep 1
	adb shell "ls -la /sys/fs/pstore/ 2>/dev/null; for f in /sys/fs/pstore/*; do echo \"=== \$f ===\"; cat \"\$f\" 2>/dev/null; done" \
		| tee "$OUT/pstore-$TS.txt" || true
fi

# 2. fastboot mode
if fastboot devices 2>/dev/null | grep -q .; then
	fastboot getvar all 2>&1 | tee "$OUT/fastboot-vars-$TS.txt" || true
fi

echo "==> Saved to $OUT"
ls -la "$OUT"/*-$TS.txt 2>/dev/null || echo "(no logs captured — see DEBUG.md)"
