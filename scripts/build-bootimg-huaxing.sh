#!/usr/bin/env bash
# Pack a Huaxing FT8719 test boot.img (does not overwrite out/boot.img).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/env.sh
source "$ROOT/scripts/env.sh"

export DTB_NAME=sm6125-xiaomi-ginkgo-huaxing.dtb
export BOOTIMG="$ROOT/out/boot-huaxing.img"

exec "$ROOT/scripts/build-bootimg.sh"
