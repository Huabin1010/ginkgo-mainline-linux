#!/usr/bin/env bash
# Create Ubuntu 26.04 arm64 minimal rootfs via debootstrap.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="${ROOTFS:-$ROOT/out/rootfs}"
SUITE="${SUITE:-resolute}"
MIRROR="${MIRROR:-http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports}"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
	exec sudo "$0" "$@"
fi

command -v debootstrap >/dev/null || { echo "run scripts/setup-deps.sh first" >&2; exit 1; }

if [[ -d "$ROOTFS/bin" ]]; then
	echo "Rootfs already exists at $ROOTFS"
	exit 0
fi

echo "==> debootstrap $SUITE arm64 -> $ROOTFS"
debootstrap --arch=arm64 --variant=minbase \
	"$SUITE" "$ROOTFS" "$MIRROR"

echo "==> Installing essentials"
chroot "$ROOTFS" apt-get update
chroot "$ROOTFS" apt-get install -y --no-install-recommends \
	systemd systemd-sysv openssh-server sudo \
	network-manager iproute2 iputils-ping \
	kmod udev ca-certificates

echo "==> Setting root password"
# shellcheck source=lib-ginkgo-pass.sh
GINKGO_REPO_ROOT="$ROOT"
source "$ROOT/scripts/lib-ginkgo-pass.sh"
ginkgo_load_root_password
echo "root:${GINKGO_ROOT_PASSWORD}" | chroot "$ROOTFS" chpasswd

echo "==> Done: $ROOTFS"
du -sh "$ROOTFS"
