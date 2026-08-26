#!/bin/sh
# Switch ginkgo Type-C from RNDIS gadget (SSH) to USB host (keyboard/mouse).
# SSH over 192.168.7.2 will drop. Use an OTG adapter, then plug the device.
set -eu

ROLE=""
for r in /sys/class/usb_role/*/role; do
	if [ -e "$r" ]; then
		ROLE="$r"
		break
	fi
done

if [ -z "$ROLE" ]; then
	echo "error: usb role switch missing (need dr_mode=otg + usb-role-switch)" >&2
	exit 1
fi

systemctl stop usb-gadget-rndis.service 2>/dev/null || true
if [ -d /sys/kernel/config/usb_gadget/ginkgo ]; then
	echo "" > /sys/kernel/config/usb_gadget/ginkgo/UDC 2>/dev/null || true
fi

echo host > "$ROLE"
echo "Type-C is USB host. Plug OTG + keyboard/mouse."
echo -n "role="; cat "$ROLE"
