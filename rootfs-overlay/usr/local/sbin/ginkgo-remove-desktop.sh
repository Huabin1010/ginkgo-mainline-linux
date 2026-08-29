#!/bin/sh
# Stop Ubuntu GNOME from ever starting, then purge the desktop metapackages.
# HUD / Mesa / NetworkManager stay. Purge does not need the network.
set -eu

LOG=/var/log/ginkgo-remove-desktop.log
exec >>"$LOG" 2>&1
echo "=== $(date -Is 2>/dev/null || date) ==="

systemctl set-default multi-user.target || true
systemctl mask gdm.service gdm3.service display-manager.service getty@tty1.service || true
systemctl stop gdm.service gdm3.service display-manager.service || true
pkill -TERM gnome-shell 2>/dev/null || true
pkill -TERM mutter 2>/dev/null || true

if ! command -v apt-get >/dev/null; then
	mkdir -p /var/lib/ginkgo
	touch /var/lib/ginkgo-remove-desktop.done
	exit 0
fi

export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a

# HUD scanout needs these after gnome-shell is gone.
apt-mark manual \
	libegl1 libgles2 libgbm1 libdrm2 \
	libgl1-mesa-dri mesa-libgallium mesa-vulkan-drivers \
	network-manager wpasupplicant iw rfkill openssh-server \
	2>/dev/null || true

nice -n 19 ionice -c3 apt-get purge -y --auto-remove \
	gdm3 gnome-shell ubuntu-desktop ubuntu-desktop-minimal \
	ubuntu-session gnome-session gnome-control-center \
	gnome-settings-daemon mutter \
	2>/dev/null || true

mkdir -p /var/lib/ginkgo
touch /var/lib/ginkgo-remove-desktop.done
echo "done"
