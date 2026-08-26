#!/usr/bin/env bash
# Create a GitHub Release: boot.img, empty dtbo, optional compressed rootfs.
#
# Interface:
#   GitHub Releases REST API  →  gh release create
#   https://docs.github.com/en/rest/releases/releases
#
# Usage:
#   gh auth login                          # once
#   ./scripts/build-bootimg.sh             # produces out/boot.img
#   ./scripts/build-bootimg-huaxing.sh     # optional: out/boot-huaxing.img
#   ./scripts/make-empty-dtbo.sh           # produces out/dtbo-empty.img
#   ./scripts/publish-release.sh v0.2.0
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-}"
REPO="${GITHUB_REPOSITORY:-Huabin1010/ginkgo-mainline-linux}"
OUT="$ROOT/out"
BOOT="${BOOTIMG:-$OUT/boot.img}"
DTBO="${DTBOIMG:-$OUT/dtbo-empty.img}"

usage() {
	echo "usage: $0 vX.Y.Z" >&2
	exit 1
}

[[ -n "$TAG" ]] || usage
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+ ]] || {
	echo "error: tag should look like v0.1.0" >&2
	exit 1
}

command -v gh >/dev/null || {
	echo "error: gh not found (https://cli.github.com/)" >&2
	exit 1
}

if ! gh auth status >/dev/null 2>&1; then
	echo "error: GitHub API not logged in. Run:  gh auth login" >&2
	echo "       git SSH is not enough for Releases." >&2
	exit 1
fi

[[ -f "$BOOT" ]] || { echo "error: missing $BOOT — run scripts/build-bootimg.sh" >&2; exit 1; }
[[ -f "$DTBO" ]] || "$ROOT/scripts/make-empty-dtbo.sh"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT
cp -a "$BOOT" "$WORKDIR/boot.img"
cp -a "$DTBO" "$WORKDIR/dtbo-empty.img"
ASSETS=( "$WORKDIR/boot.img" "$WORKDIR/dtbo-empty.img" )

HUAXING="${BOOT_HUAXING:-$OUT/boot-huaxing.img}"
if [[ -f "$HUAXING" ]]; then
	cp -a "$HUAXING" "$WORKDIR/boot-huaxing.img"
	ASSETS+=( "$WORKDIR/boot-huaxing.img" )
fi

ROOTFS="${ROOTFS_IMG:-$OUT/rootfs.ext4}"
if [[ -f "$ROOTFS" ]]; then
	command -v zstd >/dev/null || { echo "error: zstd required to pack rootfs (apt install zstd)" >&2; exit 1; }
	echo "==> Compressing $ROOTFS (GitHub rejects a raw 2 GiB file)"
	zstd -T0 -3 -f -o "$WORKDIR/rootfs.ext4.zst" "$ROOTFS"
	ASSETS+=( "$WORKDIR/rootfs.ext4.zst" )
fi

(
	cd "$WORKDIR"
	sha256sum -- "${ASSETS[@]##*/}" > SHA256SUMS
)
ASSETS+=( "$WORKDIR/SHA256SUMS" )

NOTES="$WORKDIR/notes.md"
cat > "$NOTES" <<EOF
Images for **Xiaomi Redmi Note 8 (ginkgo)**.

\`boot.img\` is the supported Tianma NT36672A build (display, touch, desktop). \`boot-huaxing.img\` (if attached) is the Huaxing FT8719 test kernel — display works on tester units; touch is off. File an issue with UART or \`dmesg\` if a panel stays black.

Flash tutorial (EN): https://github.com/${REPO}/blob/main/docs/flash-guide.md
刷机教程（中文）: https://github.com/${REPO}/blob/main/docs/zh-CN/flash-guide.md

\`rootfs.ext4\` is shipped as \`rootfs.ext4.zst\` (the raw 2 GiB file is over GitHub's limit). Unpack with \`zstd -d\`, then flash userdata.

\`\`\`
fastboot getvar product    # must be ginkgo
zstd -d rootfs.ext4.zst
fastboot flash userdata rootfs.ext4
fastboot flash dtbo dtbo-empty.img
fastboot flash boot boot.img
fastboot reboot
\`\`\`
EOF

echo "==> Creating $REPO release $TAG"
gh release create "$TAG" \
	--repo "$REPO" \
	--title "ginkgo mainline $TAG" \
	--notes-file "$NOTES" \
	"${ASSETS[@]}"

echo "==> $TAG published"
echo "    https://github.com/${REPO}/releases/tag/${TAG}"
