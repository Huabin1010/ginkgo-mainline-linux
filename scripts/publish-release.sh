#!/usr/bin/env bash
# Create a GitHub Release and upload kernel-side images (not the rootfs).
#
# Interface:
#   GitHub Releases REST API  →  gh release create
#   https://docs.github.com/en/rest/releases/releases
#
# Usage:
#   gh auth login                          # once
#   ./scripts/build-bootimg.sh             # produces out/boot.img
#   ./scripts/make-empty-dtbo.sh           # produces out/dtbo-empty.img
#   ./scripts/publish-release.sh v0.1.0
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

# Never attach the Ubuntu image: it is huge and may contain a password.

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT
cp -a "$BOOT" "$WORKDIR/boot.img"
cp -a "$DTBO" "$WORKDIR/dtbo-empty.img"
(
	cd "$WORKDIR"
	sha256sum boot.img dtbo-empty.img > SHA256SUMS
)

NOTES="$WORKDIR/notes.md"
cat > "$NOTES" <<EOF
Kernel-side images for **Xiaomi Redmi Note 8 (ginkgo)**.

Flash tutorial (EN): https://github.com/${REPO}/blob/main/docs/flash-guide.md
刷机教程（中文）: https://github.com/${REPO}/blob/main/docs/zh-CN/flash-guide.md

This release does **not** include \`rootfs.ext4\`. Build that locally.

\`\`\`
fastboot getvar product    # must be ginkgo
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
	"$WORKDIR/boot.img" \
	"$WORKDIR/dtbo-empty.img" \
	"$WORKDIR/SHA256SUMS"

echo "==> $TAG published"
echo "    https://github.com/${REPO}/releases/tag/${TAG}"
