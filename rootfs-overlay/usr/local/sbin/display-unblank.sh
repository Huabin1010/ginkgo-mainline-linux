#!/bin/sh
# Unblank msm DRM, restore backlight, then own the panel with the HUD.
set -eu

for _ in $(seq 1 100); do
	if [ -e /dev/dri/card0 ] && [ -e /dev/fb0 ]; then
		break
	fi
	sleep 0.2
done
sleep 1

if [ -e /sys/class/graphics/fb0/blank ]; then
	echo 0 > /sys/class/graphics/fb0/blank || true
fi

for d in /sys/class/backlight/*; do
	[ -d "$d" ] || continue
	case "$(basename "$d")" in
		*button*|*kbd*|*keyboard*) continue ;;
	esac
	echo 0 > "$d/bl_power" 2>/dev/null || true
	max=$(cat "$d/max_brightness" 2>/dev/null || echo 0)
	[ "$max" -gt 0 ] 2>/dev/null || continue
	pct=""
	if [ -f /var/lib/ginkgo/hud.conf ]; then
		pct=$(awk -F= '/^bl=/{print $2; exit}' /var/lib/ginkgo/hud.conf 2>/dev/null || true)
	fi
	if [ -n "$pct" ] && [ "$pct" -ge 1 ] && [ "$pct" -le 100 ]; then
		val=$((1 + pct * (max - 1) / 100))
		echo "$val" > "$d/brightness" 2>/dev/null || true
	else
		cur=$(cat "$d/brightness" 2>/dev/null || echo 0)
		if [ "$cur" = "0" ]; then
			echo "$max" > "$d/brightness" 2>/dev/null || true
		fi
	fi
done

for d in /sys/class/rfkill/*; do
	[ -d "$d" ] || continue
	echo 1 > "$d/state" 2>/dev/null || true
done
rfkill unblock all >/dev/null 2>&1 || true

{
	echo "=== $(date -Is 2>/dev/null || date) ==="
	uname -r
	ls -l /dev/dri /dev/fb0 /dev/input 2>/dev/null || true
	ls -l /sys/class/net 2>/dev/null || true
	cat /proc/bus/input/devices 2>/dev/null || true
} >> /var/log/ginkgo-display.log 2>&1 || true

exec /usr/local/sbin/ginkgo-status
