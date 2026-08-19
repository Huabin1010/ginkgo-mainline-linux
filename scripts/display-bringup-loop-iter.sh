#!/usr/bin/env bash
# One iteration of the display bring-up loop: SSH check -> report -> exit code = pass/fail
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK="$ROOT/scripts/display-success-check.py"
ITER_LOG="${ITER_LOG:-$ROOT/out/display-loop.log}"

mkdir -p "$(dirname "$ITER_LOG")"

echo "======== $(date -Is) LOOP ITERATION ========" | tee -a "$ITER_LOG"

"$ROOT/scripts/usb-connect.sh" 2>&1 | tee -a "$ITER_LOG"

HOST_ADDR="${HOST_IP:-192.168.7.1/24}"
HOST_ADDR="${HOST_ADDR%%/*}"
PHONE_IP="${PHONE_IP:-192.168.7.2}"
export GINKGO_REPO_ROOT="$ROOT"
# shellcheck source=lib-ginkgo-pass.sh
source "$ROOT/scripts/lib-ginkgo-pass.sh"
ginkgo_load_root_password
GINKGO_PASS="$GINKGO_ROOT_PASSWORD"

echo "--- display-success-check ---" | tee -a "$ITER_LOG"
SSHPASS="$GINKGO_PASS" sshpass -e scp -q -o BindAddress="$HOST_ADDR" \
	-o StrictHostKeyChecking=accept-new "$CHECK" "root@${PHONE_IP}:/tmp/display-success-check.py"
if SSHPASS="$GINKGO_PASS" sshpass -e ssh -b "$HOST_ADDR" \
	-o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 \
	"root@${PHONE_IP}" 'python3 -u /tmp/display-success-check.py' 2>&1 | tee -a "$ITER_LOG"; then
	echo "LOOP_EXIT: LEVEL_B_PASS" | tee -a "$ITER_LOG"
	exit 0
fi

echo "LOOP_EXIT: LEVEL_B_FAIL — analyze dmesg and kernel" | tee -a "$ITER_LOG"
SSHPASS="$GINKGO_PASS" sshpass -e \
	ssh -b "$HOST_ADDR" -o StrictHostKeyChecking=accept-new \
	-o ConnectTimeout=15 "root@${PHONE_IP}" \
	'dmesg | grep -iE "LCDB|panel|dsi_err|FIFO|msm_drm|underrun|tearcheck|vblank|encoder" | tail -40' 2>&1 | tee -a "$ITER_LOG"
exit 1
