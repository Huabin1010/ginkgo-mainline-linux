#!/usr/bin/env bash
# Post-process rootfs: firmware, fstab, serial console, hostname.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="${ROOTFS:-$ROOT/out/rootfs}"
FW_SRC="$ROOT/firmware/ginkgo"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
	exec sudo "$0" "$@"
fi

[[ -d "$ROOTFS/bin" ]] || { echo "rootfs missing — run scripts/build-rootfs.sh first" >&2; exit 1; }

echo "==> Installing firmware"
install -d "$ROOTFS/lib/firmware/ath10k/WCN3990/hw1.0"
install -d "$ROOTFS/lib/firmware/qcom"

# Touch
for f in novatek_ts_tianma_fw.bin novatek_ts_ebbg_fw.bin; do
	[[ -f "$FW_SRC/$f" ]] && install -m 644 "$FW_SRC/$f" "$ROOTFS/lib/firmware/"
done

# WiFi (WCN3990). board.bin is the raw c3j BDF (26328 bytes); ath10k
# falls back to it if board-2.bin has no matching qmi-board-id entry.
# Do not install the raw BDF as board-2.bin — that file needs the
# QCA-ATH10K-BOARD IE wrapper.
install -m 644 "$FW_SRC/wifi/wlanmdsp.mbn" "$ROOTFS/lib/firmware/ath10k/WCN3990/hw1.0/"
install -m 644 "$FW_SRC/wifi/firmware-5.bin" "$ROOTFS/lib/firmware/ath10k/WCN3990/hw1.0/"
	if [[ -f "$FW_SRC/wifi/bdf_c3j.bin" ]]; then
	install -m 644 "$FW_SRC/wifi/bdf_c3j.bin" "$ROOTFS/lib/firmware/ath10k/WCN3990/hw1.0/board.bin"
fi

# Adreno 610: SQE from linux-firmware (device SQE is too old for the
# mainline whereami check). Zap is device-signed — must be the ginkgo
# vendor image, not a generic linux-firmware zap.
install -d "$ROOTFS/lib/firmware/qcom/sm6125/xiaomi/ginkgo"
if [[ -f "$FW_SRC/gpu/a630_sqe.fw" ]]; then
	install -m 644 "$FW_SRC/gpu/a630_sqe.fw" "$ROOTFS/lib/firmware/qcom/"
fi
for f in a610_zap.mdt a610_zap.b00 a610_zap.b01 a610_zap.b02; do
	[[ -f "$FW_SRC/gpu/$f" ]] && install -m 644 "$FW_SRC/gpu/$f" \
		"$ROOTFS/lib/firmware/qcom/sm6125/xiaomi/ginkgo/"
done
# Optional signed regulatory database for 2.4 + 5 GHz (cfg80211).
for f in regulatory.db regulatory.db.p7s; do
	if [[ -f /lib/firmware/$f ]]; then
		install -m 644 "/lib/firmware/$f" "$ROOTFS/lib/firmware/"
	fi
done

echo "==> fstab"
cat > "$ROOTFS/etc/fstab" <<'EOF'
# <file system>  <mount point>  <type>  <options>         <dump> <pass>
/dev/disk/by-partlabel/userdata  /  ext4  defaults,noatime  0  1
tmpfs  /tmp   tmpfs  defaults,nodev,nosuid  0  0
tmpfs  /run   tmpfs  defaults,nodev,nosuid  0  0
# persist: late mount via persist-mount.service (avoid 90s udev timeout at boot)
EOF

echo "==> hostname + hosts"
echo ginkgo > "$ROOTFS/etc/hostname"
cat > "$ROOTFS/etc/hosts" <<'EOF'
127.0.0.1 localhost ginkgo
::1       localhost ip6-localhost ip6-loopback
EOF

echo "==> serial console getty"
mkdir -p "$ROOTFS/etc/systemd/system/getty.target.wants"
ln -sf /lib/systemd/system/serial-getty@.service \
	"$ROOTFS/etc/systemd/system/getty.target.wants/serial-getty@ttyMSM0.service"

echo "==> enable NetworkManager"
chroot "$ROOTFS" systemctl enable NetworkManager 2>/dev/null || true

echo "==> USB RNDIS gadget + SSH"
OVERLAY="$ROOT/rootfs-overlay"
if [[ -d "$OVERLAY" ]]; then
	cp -a "$OVERLAY/." "$ROOTFS/"
fi
chmod 755 "$ROOTFS/usr/local/sbin/usb-gadget-rndis.sh"
chmod 755 "$ROOTFS/usr/local/sbin/mount-persist.sh" 2>/dev/null || true
chmod 755 "$ROOTFS/usr/local/sbin/ensure-root-password.sh" 2>/dev/null || true
chroot "$ROOTFS" systemctl enable usb-gadget-rndis.service 2>/dev/null || true
chroot "$ROOTFS" systemctl enable ensure-root-password.service 2>/dev/null || true
chroot "$ROOTFS" systemctl enable persist-mount.service 2>/dev/null || true
chroot "$ROOTFS" systemctl enable ssh.service 2>/dev/null || true

echo "==> Done post-install"
du -sh "$ROOTFS"
