#!/usr/bin/env bash
# Install build dependencies for ginkgo mainline kernel + Ubuntu rootfs.
set -euo pipefail

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
	exec sudo "$0" "$@"
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
	gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
	bc bison flex libssl-dev libncurses-dev libelf-dev \
	device-tree-compiler debootstrap qemu-user-static binfmt-support \
	cpio gzip xz-utils python3 \
	abootimg debootstrap qemu-user-binfmt-hwe mkbootimg

echo "Dependencies installed."
echo "Verify: aarch64-linux-gnu-gcc --version"
