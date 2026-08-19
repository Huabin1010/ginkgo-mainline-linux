#!/usr/bin/env bash
# Capture ginkgo UART console to backup/ginkgo/logs/
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/backup/ginkgo/logs"
mkdir -p "$OUT"
DEV="${1:-}"
BAUD="${BAUD:-115200}"

if [[ -z "$DEV" ]]; then
	for c in /dev/ttyUSB0 /dev/ttyACM0 /dev/ttyUSB1; do
		[[ -e "$c" ]] && DEV=$c && break
	done
fi
[[ -n "${DEV:-}" && -e "$DEV" ]] || {
	echo "No serial device. Plug 1.8V USB-TTL and pass path, e.g.:"
	echo "  $0 /dev/ttyUSB0"
	ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
	exit 1
}

LOG="$OUT/uart-$(date +%Y%m%d-%H%M%S).log"
echo "Capturing $DEV @ $BAUD -> $LOG"
echo "Open serial FIRST, then reboot the phone. Ctrl-A Ctrl-X to quit picocom."
exec picocom -b "$BAUD" "$DEV" | tee "$LOG"
