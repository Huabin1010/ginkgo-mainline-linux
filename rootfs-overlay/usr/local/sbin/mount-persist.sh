#!/bin/bash
# Mount Android persist after boot (not in fstab — avoids 90s udev timeout).
set -euo pipefail

MOUNT_POINT=/persist
PARTITIONS=(
	/dev/mmcblk0p69
	/dev/disk/by-partlabel/persist
)

if mountpoint -q "$MOUNT_POINT"; then
	exit 0
fi

mkdir -p "$MOUNT_POINT"

for dev in "${PARTITIONS[@]}"; do
	[[ -b "$dev" ]] || continue
	if mount -t ext4 -o ro,nosuid,nodev,noatime "$dev" "$MOUNT_POINT" 2>/dev/null; then
		logger -t mount-persist "mounted $dev on $MOUNT_POINT"
		exit 0
	fi
	if mount -t ext4 -o nosuid,nodev,noatime "$dev" "$MOUNT_POINT" 2>/dev/null; then
		logger -t mount-persist "mounted $dev on $MOUNT_POINT (rw)"
		exit 0
	fi
done

logger -t mount-persist "persist partition not available, skipping"
exit 0
