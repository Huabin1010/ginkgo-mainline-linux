#!/bin/sh
# Start SM6125 MPSS so WCN3990 ath10k can reach the WLFW QMI service.
set -eu

RPROC_DIR=""
for d in /sys/devices/platform/soc@0/6080000.remoteproc/remoteproc/remoteproc*; do
	if [ -f "$d/state" ]; then
		RPROC_DIR="$d"
		break
	fi
done

if [ -z "$RPROC_DIR" ]; then
	echo "ginkgo-start-mpss: remoteproc sysfs not found" >&2
	exit 1
fi

state=$(cat "$RPROC_DIR/state")
if [ "$state" = "running" ]; then
	echo "ginkgo-start-mpss: already running ($RPROC_DIR)"
	exit 0
fi

echo "ginkgo-start-mpss: starting $RPROC_DIR (was $state)"
echo start > "$RPROC_DIR/state"
echo "ginkgo-start-mpss: now $(cat "$RPROC_DIR/state")"
