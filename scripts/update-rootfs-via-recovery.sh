#!/usr/bin/env bash
# Push rootfs-overlay into running userdata via recovery adb (incremental update).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVERLAY="$ROOT/rootfs-overlay"
MNT=/tmp/ginkgo-rootfs-mnt
TARBALL=/tmp/ginkgo-rootfs-overlay.tar.gz

adb devices | grep -q recovery || { echo "error: need recovery adb" >&2; exit 1; }
[[ -d "$OVERLAY" ]] || { echo "missing $OVERLAY" >&2; exit 1; }

echo "==> Pack overlay"
tar -C "$OVERLAY" -czf "$TARBALL" .

echo "==> Mount userdata"
adb shell "mkdir -p $MNT && mount -t ext4 /dev/block/mmcblk0p87 $MNT"

echo "==> Extract overlay"
adb push "$TARBALL" /tmp/ginkgo-rootfs-overlay.tar.gz
adb shell "tar -xzf /tmp/ginkgo-rootfs-overlay.tar.gz -C $MNT && \
	chmod 755 $MNT/usr/local/sbin/usb-gadget-rndis.sh $MNT/usr/local/sbin/mount-persist.sh $MNT/usr/local/sbin/ensure-root-password.sh && \
	chmod 600 $MNT/etc/ginkgo-root-password && \
	rm /tmp/ginkgo-rootfs-overlay.tar.gz"

echo "==> Enable services + fix fstab"
adb shell "
	mkdir -p $MNT/etc/systemd/system/{sysinit.target.wants,multi-user.target.wants}
	ln -sf ../usb-gadget-rndis.service $MNT/etc/systemd/system/sysinit.target.wants/usb-gadget-rndis.service
	ln -sf ../ensure-root-password.service $MNT/etc/systemd/system/sysinit.target.wants/ensure-root-password.service
	ln -sf /lib/systemd/system/ssh.service $MNT/etc/systemd/system/multi-user.target.wants/ssh.service
	ln -sf ../persist-mount.service $MNT/etc/systemd/system/multi-user.target.wants/persist-mount.service
	sync
"

adb shell "umount $MNT"
rm -f "$TARBALL"
echo "==> Done. Reboot phone: adb reboot"
