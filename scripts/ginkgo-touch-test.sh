#!/usr/bin/env bash
# 把触控测试推到 ginkgo 并运行。
#   ./scripts/ginkgo-touch-test.sh --probe   # 只列 /dev/input
#   ./scripts/ginkgo-touch-test.sh           # 画屏，点屏幕看点；音量- 退出
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/scripts/ginkgo-touch-test.py"
REMOTE=/usr/local/sbin/ginkgo-touch-test.py
HOST_ADDR="${HOST_IP:-192.168.7.1/24}"
HOST_ADDR="${HOST_ADDR%%/*}"
PHONE_IP="${PHONE_IP:-192.168.7.2}"
SSH_USER="${SSH_USER:-root}"
# shellcheck source=lib-ginkgo-pass.sh
GINKGO_REPO_ROOT="$ROOT"
source "$ROOT/scripts/lib-ginkgo-pass.sh"
ginkgo_load_root_password

"$ROOT/scripts/usb-connect.sh"

ssh_cmd() {
	SSHPASS="$GINKGO_ROOT_PASSWORD" sshpass -e \
		ssh -b "$HOST_ADDR" -o StrictHostKeyChecking=accept-new \
		-o ConnectTimeout=15 "${SSH_USER}@${PHONE_IP}" "$@"
}

SSHPASS="$GINKGO_ROOT_PASSWORD" sshpass -e \
	scp -o BindAddress="$HOST_ADDR" -o StrictHostKeyChecking=accept-new \
	"$SRC" "${SSH_USER}@${PHONE_IP}:${REMOTE}"
ssh_cmd "chmod +x ${REMOTE}"

if [[ "${1:-}" == "--probe" ]]; then
	ssh_cmd "python3 ${REMOTE} --probe"
else
	echo "手机屏幕会变成测试图。用手指点屏；音量- 退出，音量+ 清屏。"
	echo "Ctrl-C 也会停。"
	ssh_cmd "python3 -u ${REMOTE}"
fi
