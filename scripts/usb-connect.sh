#!/usr/bin/env bash
# Reconfigure host RNDIS and verify ginkgo is reachable (run after every reboot).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_IP="${HOST_IP:-192.168.7.1/24}"
PHONE_IP="${PHONE_IP:-192.168.7.2}"
SSH_USER="${SSH_USER:-root}"
WAIT_PING="${WAIT_PING:-60}"
WAIT_SSH="${WAIT_SSH:-90}"

# shellcheck source=lib-ginkgo-pass.sh
export GINKGO_REPO_ROOT="$ROOT"
source "$ROOT/scripts/lib-ginkgo-pass.sh"
ginkgo_load_root_password

sudo_cmd() {
	ginkgo_sudo "$@" 2>/dev/null
}

find_rndis_iface() {
	local name driver
	for name in /sys/class/net/*; do
		name=$(basename "$name")
		[[ "$name" == lo ]] && continue
		driver=$(readlink -f "/sys/class/net/$name/device/driver" 2>/dev/null || true)
		if [[ "$driver" == *rndis* ]] || [[ "$driver" == *cdc_ether* ]] || [[ "$name" == enx* ]]; then
			echo "$name"
			return 0
		fi
	done
	return 1
}

unmanage_iface() {
	local iface="$1"
	# Host NM treats RNDIS as a LAN NIC, DHCP-fails, then flushes our
	# static 192.168.7.1. Take it out of NM before assigning the IP.
	if command -v nmcli &>/dev/null; then
		sudo_cmd nmcli device set "$iface" managed no || true
		sudo_cmd nmcli device disconnect "$iface" || true
	fi
}

configure_iface() {
	local iface="$1"
	local host_addr="${HOST_IP%%/*}"
	local net="${PHONE_IP%.*}.0/24"

	echo "==> RNDIS interface: $iface"
	unmanage_iface "$iface"
	sudo_cmd ip link set "$iface" up
	sudo_cmd ip addr flush dev "$iface" || true
	sudo_cmd ip addr add "$HOST_IP" dev "$iface"
	sudo_cmd ip route replace "$net" dev "$iface" metric 500

	echo "==> Host $host_addr -> phone $PHONE_IP"
}

wait_ping() {
	local host_addr="${HOST_IP%%/*}"
	local i
	for ((i = 1; i <= WAIT_PING; i++)); do
		if ping -c1 -W1 -I "$host_addr" "$PHONE_IP" &>/dev/null; then
			echo "==> Ping OK (${i}s)"
			return 0
		fi
		sleep 1
	done
	echo "error: $PHONE_IP not responding after ${WAIT_PING}s" >&2
	return 1
}

wait_ssh() {
	local host_addr="${HOST_IP%%/*}"
	local i
	if ! command -v sshpass &>/dev/null; then
		echo "warn: sshpass not installed; ping OK but skipping SSH probe" >&2
		return 0
	fi
	for ((i = 1; i <= WAIT_SSH; i++)); do
		if SSHPASS="$GINKGO_ROOT_PASSWORD" sshpass -e \
			ssh -b "$host_addr" -o StrictHostKeyChecking=accept-new \
			-o ConnectTimeout=5 "${SSH_USER}@${PHONE_IP}" 'echo ssh-ok' \
			&>/dev/null; then
			echo "==> SSH OK (${i}s)"
			echo "SSH: ssh -b $host_addr ${SSH_USER}@${PHONE_IP}"
			return 0
		fi
		sleep 1
	done
	echo "error: SSH not ready after ${WAIT_SSH}s" >&2
	return 1
}

iface=""
for _ in $(seq 1 45); do
	iface=$(find_rndis_iface || true)
	[[ -n "$iface" ]] && break
	sleep 1
done

if [[ -z "$iface" ]]; then
	echo "error: no USB RNDIS interface (enx*/rndis)" >&2
	ip -br link || true
	exit 1
fi

configure_iface "$iface"
wait_ping
wait_ssh
