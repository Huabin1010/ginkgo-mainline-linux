#!/bin/bash
# USB gadget: RNDIS network to the connected PC (usb0 = 192.168.7.2).
set -euo pipefail

GADGET_NAME="${GADGET_NAME:-ginkgo}"
GADGET="/sys/kernel/config/usb_gadget/${GADGET_NAME}"
CONFIGFS="/sys/kernel/config"
UDC="${UDC:-}"
PHONE_IP="${PHONE_IP:-192.168.7.2/24}"
IFACE="${IFACE:-usb0}"

mount_configfs() {
	if ! mountpoint -q "$CONFIGFS"; then
		mkdir -p "$CONFIGFS"
		mount -t configfs none "$CONFIGFS"
	fi
}

find_udc() {
	local udc

	if [[ -n "$UDC" ]]; then
		echo "$UDC"
		return
	fi

	for _ in $(seq 1 60); do
		udc=$(ls /sys/class/udc 2>/dev/null | head -1 || true)
		[[ -n "$udc" ]] && { echo "$udc"; return; }
		sleep 0.5
	done
	return 1
}

setup_gadget() {
	local udc host_mac dev_mac

	mount_configfs
	[[ -d "$GADGET" ]] && return 0

	mkdir -p "$GADGET"
	cd "$GADGET"

	# Linux Foundation / RNDIS
	echo 0x1d6b > idVendor
	echo 0x0104 > idProduct
	echo 0x0100 > bcdDevice
	echo 0x0200 > bcdUSB

	mkdir -p strings/0x409
	echo "ginkgo-usb-net" > strings/0x409/serialnumber
	echo "Kernel-Build" > strings/0x409/manufacturer
	echo "Ginkgo USB Network" > strings/0x409/product

	mkdir -p configs/c.1/strings/0x409
	echo "RNDIS" > configs/c.1/strings/0x409/configuration
	echo 250 > configs/c.1/MaxPower

	mkdir -p functions/rndis.usb0
	# Fixed MACs so the host always sees enx020000000701. RANDOM MACs
	# made a new interface every boot; NetworkManager then DHCP-failed
	# and flushed 192.168.7.1.
	host_mac="${HOST_MAC:-02:00:00:00:07:01}"
	dev_mac="${DEV_MAC:-02:00:00:00:07:02}"
	echo "$host_mac" > functions/rndis.usb0/host_addr
	echo "$dev_mac" > functions/rndis.usb0/dev_addr

	ln -sf functions/rndis.usb0 configs/c.1/

	udc=$(find_udc) || { echo "usb-gadget: no UDC found" >&2; return 1; }
	echo "$udc" > UDC
	echo "usb-gadget: bound to UDC $udc" >&2
}

wait_iface() {
	for _ in $(seq 1 40); do
		ip link show "$IFACE" &>/dev/null && return 0
		sleep 0.25
	done
	return 1
}

setup_network() {
	wait_iface || { echo "usb-gadget: $IFACE not found" >&2; return 1; }
	ip link set "$IFACE" up
	ip addr flush dev "$IFACE" 2>/dev/null || true
	ip addr add "$PHONE_IP" dev "$IFACE"
	echo "usb-gadget: $IFACE $PHONE_IP ready" >&2
}

teardown_network() {
	ip addr flush dev "$IFACE" 2>/dev/null || true
	ip link set "$IFACE" down 2>/dev/null || true
}

teardown_gadget() {
	[[ -d "$GADGET" ]] || return 0
	echo "" > "$GADGET/UDC" 2>/dev/null || true
	rm -f "$GADGET/configs/c.1/rndis.usb0" 2>/dev/null || true
	rmdir "$GADGET/functions/rndis.usb0" 2>/dev/null || true
	rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null || true
	rmdir "$GADGET/configs/c.1" 2>/dev/null || true
	rmdir "$GADGET/strings/0x409" 2>/dev/null || true
	rmdir "$GADGET" 2>/dev/null || true
}

case "${1:-start}" in
	start)
		setup_gadget
		setup_network
		;;
	stop)
		teardown_network
		teardown_gadget
		;;
	restart)
		teardown_network
		teardown_gadget
		setup_gadget
		setup_network
		;;
	*)
		echo "Usage: $0 {start|stop|restart}" >&2
		exit 1
		;;
esac
