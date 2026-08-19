#!/usr/bin/env bash
# Create ext4 rootfs image from out/rootfs directory.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="${ROOTFS:-$ROOT/out/rootfs}"
IMAGE="${IMAGE:-$ROOT/out/rootfs.ext4}"
SIZE_MB="${SIZE_MB:-4096}"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
	exec sudo "$0" "$@"
fi

[[ -d "$ROOTFS/bin" ]] || { echo "rootfs missing" >&2; exit 1; }

USED_MB=$(du -sm "$ROOTFS" | cut -f1)
SIZE_MB=$(( USED_MB + 512 ))
if [[ "$SIZE_MB" -lt 2048 ]]; then SIZE_MB=2048; fi

echo "==> Creating ${SIZE_MB}MB ext4 image: $IMAGE"
rm -f "$IMAGE"
truncate -s "${SIZE_MB}M" "$IMAGE"
mkfs.ext4 -F -L userdata "$IMAGE" >/dev/null

MNT=$(mktemp -d)
mount -o loop "$IMAGE" "$MNT"
cp -a "$ROOTFS"/. "$MNT"/
umount "$MNT"
rmdir "$MNT"

ls -lh "$IMAGE"
echo "Flash: fastboot flash userdata $IMAGE  (WARNING: erases data partition)"
