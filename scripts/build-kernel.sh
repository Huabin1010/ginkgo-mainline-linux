#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/env.sh
source "$ROOT/scripts/env.sh"

JOBS="${JOBS:-$(nproc)}"
FRAGMENT="$ROOT/config/ginkgo.fragment"
DEFCONFIG="${DEFCONFIG:-defconfig}"
OUT="$ROOT/out"

die() { echo "error: $*" >&2; exit 1; }

[[ -d "$KERNEL_SRC" ]] || die "kernel source missing at $KERNEL_SRC (run scripts/setup-kernel.sh)"

command -v "${CROSS_COMPILE}gcc" >/dev/null || die "cross compiler not found; run scripts/setup-deps.sh"

mkdir -p "$KBUILD_OUTPUT" "$OUT"

cd "$KERNEL_SRC"

if [[ ! -f "$KBUILD_OUTPUT/.config" ]]; then
	echo "==> Initial config: $DEFCONFIG"
	make O="$KBUILD_OUTPUT" "$DEFCONFIG"
fi

if [[ -f "$FRAGMENT" ]]; then
	echo "==> Merging ginkgo fragment"
	FW_FRAG="$(mktemp)"
	echo "CONFIG_EXTRA_FIRMWARE_DIR=\"$ROOT/firmware/ginkgo\"" > "$FW_FRAG"
	"$KERNEL_SRC/scripts/kconfig/merge_config.sh" -m -O "$KBUILD_OUTPUT" \
		"$KBUILD_OUTPUT/.config" "$FRAGMENT" "$FW_FRAG"
	rm -f "$FW_FRAG"
	make O="$KBUILD_OUTPUT" olddefconfig
fi

echo "==> Building Image.gz + $DTB_NAME ($JOBS jobs)"
make O="$KBUILD_OUTPUT" -j"$JOBS" Image.gz "dtbs"

if grep -q '^CONFIG_LEDS_QCOM_LPG=m' "$KBUILD_OUTPUT/.config"; then
	echo "==> Building leds-qcom-lpg module (PWM backlight)"
	make O="$KBUILD_OUTPUT" -j"$JOBS" modules
	MODDIR="$OUT/kernel-modules"
	rm -rf "$MODDIR"
	make O="$KBUILD_OUTPUT" INSTALL_MOD_PATH="$MODDIR" modules_install
	KREL="$(make O="$KBUILD_OUTPUT" -s kernelrelease)"
	cp -a "$MODDIR/lib/modules/$KREL" "$OUT/modules"
	echo "==> Installed modules to $OUT/modules"
fi

cp -f "$KBUILD_OUTPUT/arch/arm64/boot/Image.gz" "$OUT/Image.gz"
cp -f "$KBUILD_OUTPUT/arch/arm64/boot/dts/qcom/$DTB_NAME" "$OUT/$DTB_NAME"

echo "==> Done"
ls -lh "$OUT/Image.gz" "$OUT/$DTB_NAME"
