#!/usr/bin/env bash
# Build minimal gzip cpio initramfs with static aarch64 /init.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out"
STAGING="$OUT/initramfs-staging"
IMAGE="$OUT/initramfs.cpio.gz"
INIT_BIN="$OUT/initramfs-init"
CC="${CC:-aarch64-linux-gnu-gcc}"

echo "==> Compiling static aarch64 init"
"$CC" -static -Os -Wall -Wextra -o "$INIT_BIN" "$ROOT/initramfs/init.c"
file "$INIT_BIN"

echo "==> Building initramfs -> $IMAGE"
rm -rf "$STAGING"
mkdir -p "$STAGING"
install -m 755 "$INIT_BIN" "$STAGING/init"
mkdir -p "$STAGING/sbin"
ln -sf ../init "$STAGING/sbin/init"

if [[ -d "$ROOT/rootfs-overlay" ]]; then
	echo "==> Adding rootfs-overlay to initramfs"
	cp -a "$ROOT/rootfs-overlay/." "$STAGING/overlay/"
	chmod 755 "$STAGING/overlay/usr/local/sbin/usb-gadget-rndis.sh" \
		"$STAGING/overlay/usr/local/sbin/mount-persist.sh" \
		"$STAGING/overlay/usr/local/sbin/ensure-root-password.sh" \
		"$STAGING/overlay/usr/local/sbin/ginkgo-wifi-setup.sh"
	mkdir -p "$STAGING/overlay/etc/systemd/system/"{sysinit.target.wants,multi-user.target.wants}
	ln -sfn ../usb-gadget-rndis.service \
		"$STAGING/overlay/etc/systemd/system/sysinit.target.wants/usb-gadget-rndis.service"
	ln -sfn ../ensure-root-password.service \
		"$STAGING/overlay/etc/systemd/system/sysinit.target.wants/ensure-root-password.service"
	ln -sfn /lib/systemd/system/ssh.service \
		"$STAGING/overlay/etc/systemd/system/multi-user.target.wants/ssh.service"
	ln -sfn ../persist-mount.service \
		"$STAGING/overlay/etc/systemd/system/multi-user.target.wants/persist-mount.service"
fi

# Adreno 610 firmware: overlay lands on userdata at switch_root; also
# put a copy in initramfs /lib/firmware in case GPU binds before that.
GPU_FW="$ROOT/firmware/ginkgo/gpu"
if [[ -f "$GPU_FW/a630_sqe.fw" ]]; then
	echo "==> Adding Adreno 610 firmware"
	for dest in "$STAGING/overlay/lib/firmware/qcom" "$STAGING/lib/firmware/qcom"; do
		install -d "$dest/sm6125/xiaomi/ginkgo"
		install -m 644 "$GPU_FW/a630_sqe.fw" "$dest/"
		for f in a610_zap.mdt a610_zap.b00 a610_zap.b01 a610_zap.b02; do
			[[ -f "$GPU_FW/$f" ]] && install -m 644 "$GPU_FW/$f" \
				"$dest/sm6125/xiaomi/ginkgo/"
		done
	done
fi

(
	cd "$STAGING"
	find . -print0 | cpio --null -o --format=newc
) | gzip -9 >"$IMAGE"

ls -lh "$IMAGE"
gzip -dc "$IMAGE" | cpio -it 2>/dev/null || true
