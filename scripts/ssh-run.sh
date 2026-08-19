#!/usr/bin/env bash
# Run one remote command on ginkgo: reconfigure RNDIS then ssh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE_CMD="${*:-echo connected}"

export WAIT_PING="${WAIT_PING:-45}"
export WAIT_SSH="${WAIT_SSH:-60}"

"$SCRIPT_DIR/usb-connect.sh" >/dev/null

HOST_ADDR="${HOST_IP:-192.168.7.1/24}"
HOST_ADDR="${HOST_ADDR%%/*}"
PHONE_IP="${PHONE_IP:-192.168.7.2}"
SSH_USER="${SSH_USER:-root}"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
export GINKGO_REPO_ROOT="$ROOT"
# shellcheck source=lib-ginkgo-pass.sh
source "$ROOT/scripts/lib-ginkgo-pass.sh"
ginkgo_load_root_password

SSHPASS="$GINKGO_ROOT_PASSWORD" sshpass -e \
	ssh -b "$HOST_ADDR" -o StrictHostKeyChecking=accept-new \
	-o ConnectTimeout=15 "${SSH_USER}@${PHONE_IP}" "$REMOTE_CMD"
