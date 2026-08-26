#!/bin/sh
# msm deferred fbdev may register with FB_BLANK_POWERDOWN; unblank so KMS/GDM can take over.
# Do not paint a test pattern — that fights the desktop compositor.
set -eu

for _ in $(seq 1 50); do
	[ -e /sys/class/graphics/fb0/blank ] && break
	sleep 0.2
done

[ -e /sys/class/graphics/fb0/blank ] || exit 0

echo 0 > /sys/class/graphics/fb0/blank

# Huaxing units with LM3697 (id=0x01) often miss the probe-time
# brightness write. Re-apply so the panel is visible without SSH.
bl=/sys/class/backlight/ktd3136-backlight/brightness
if [ -e "$bl" ]; then
	b=$(cat "$bl")
	[ "$b" = "0" ] && b=1024
	echo "$b" > "$bl"
fi
