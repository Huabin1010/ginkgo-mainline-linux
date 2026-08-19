#!/usr/bin/env bash
# Configure host side USB RNDIS and SSH to ginkgo (192.168.7.2).
set -euo pipefail

HOST_IP="${HOST_IP:-192.168.7.1/24}"
PHONE_IP="${PHONE_IP:-192.168.7.2}"
SSH_USER="${SSH_USER:-root}"

iface=""
for _ in $(seq 1 30); do
	for cand in /sys/class/net/*; do
		name=$(basename "$cand")
		[[ "$name" == lo ]] && continue
		driver=$(readlink -f "$cand/device/driver" 2>/dev/null || true)
		if [[ "$driver" == *rndis* ]] || [[ "$driver" == *cdc_ether* ]] || [[ "$name" == usb* ]] || [[ "$name" == enx* ]]; then
			iface=$name
			break 2
		fi
	done
	sleep 1
done

if [[ -z "$iface" ]]; then
	echo "error: no USB RNDIS interface found (is the phone booted and USB connected?)" >&2
	echo "try: sudo modprobe rndis_host" >&2
	ip -br link
	exit 1
fi

echo "==> Using interface: $iface"
if command -v nmcli &>/dev/null; then
	sudo nmcli device set "$iface" managed no || true
	sudo nmcli device disconnect "$iface" || true
fi
sudo ip link set "$iface" up
sudo ip addr flush dev "$iface" 2>/dev/null || true
sudo ip addr add "$HOST_IP" dev "$iface"
# 仅路由手机网段，不修改默认网关（避免抢占主网络）
sudo ip route replace "${PHONE_IP%.*}.0/24" dev "$iface" metric 500

HOST_ADDR="${HOST_IP%%/*}"
echo "==> Waiting for $PHONE_IP (default route unchanged) ..."
for _ in $(seq 1 20); do
	if ping -c1 -W1 -I "$HOST_ADDR" "$PHONE_IP" &>/dev/null; then
		echo "==> Phone reachable at $PHONE_IP"
		echo "SSH: ssh -b $HOST_ADDR ${SSH_USER}@${PHONE_IP}"
		exec ssh -b "$HOST_ADDR" -o StrictHostKeyChecking=accept-new "${SSH_USER}@${PHONE_IP}"
	fi
	sleep 1
done

echo "error: phone not responding on $PHONE_IP" >&2
echo "Check serial console: usb-gadget-rndis.service status" >&2
exit 1
