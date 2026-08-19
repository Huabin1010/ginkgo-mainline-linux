#!/usr/bin/env bash
# Full display bring-up loop: check -> if fail, build+flash+reboot+recheck
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ITER="$ROOT/scripts/display-bringup-loop-iter.sh"
LOG="$ROOT/out/display-loop.log"

echo "=== DISPLAY BRINGUP LOOP ===" | tee -a "$LOG"
echo "Exit when display-success-check.py reports LEVEL_B_PASS (15/15)" | tee -a "$LOG"

if "$ITER"; then
	echo "LOOP COMPLETE: all Level B metrics pass" | tee -a "$LOG"
	exit 0
fi

echo "==> Building kernel + boot.img" | tee -a "$LOG"
cd "$ROOT"
./scripts/build-kernel.sh 2>&1 | tail -5 | tee -a "$LOG"
./scripts/build-bootimg.sh 2>&1 | tail -3 | tee -a "$LOG"

echo "==> Entering fastboot and flashing" | tee -a "$LOG"
"$ROOT/scripts/reboot-fastboot.sh" 2>&1 | tee -a "$LOG"
FLASH_ROOTFS=0 "$ROOT/scripts/flash-linux-boot.sh" 2>&1 | tee -a "$LOG"

echo "==> Waiting for boot + SSH" | tee -a "$LOG"
sleep 50
"$ROOT/scripts/usb-connect.sh" 2>&1 | tee -a "$LOG"

echo "==> Post-flash check" | tee -a "$LOG"
if "$ITER"; then
	echo "LOOP COMPLETE after flash" | tee -a "$LOG"
	exit 0
fi

echo "LOOP CONTINUE: still failing — next kernel fix needed" | tee -a "$LOG"
exit 1
