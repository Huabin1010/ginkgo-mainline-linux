#!/usr/bin/env bash
# Trigger ginkgo Ubuntu -> fastboot via SSH, then wait for a fastboot device on the host.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WAIT_SEC="${WAIT_SEC:-60}"
SSH_RUN="$ROOT/scripts/ssh-run.sh"
CONNECT="$ROOT/scripts/usb-connect.sh"

echo "==> Ensuring SSH to ginkgo"
"$CONNECT" | tail -3

echo "==> Rebooting to fastboot (reboot-fastboot)"
"$SSH_RUN" 'command -v reboot-fastboot >/dev/null && reboot-fastboot || reboot bootloader' || true

echo "==> Waiting up to ${WAIT_SEC}s for fastboot device"
for ((i = 1; i <= WAIT_SEC; i++)); do
	if fastboot devices 2>/dev/null | grep -q .; then
		echo "==> fastboot OK (${i}s)"
		fastboot devices
		exit 0
	fi
	sleep 1
done

echo "error: no fastboot device after ${WAIT_SEC}s" >&2
fastboot devices || true
exit 1
