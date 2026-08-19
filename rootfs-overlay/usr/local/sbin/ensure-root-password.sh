#!/bin/bash
# Keep root password in sync with /etc/ginkgo-root-password (dev convenience).
set -euo pipefail

PWFILE=/etc/ginkgo-root-password
MARKER=/var/lib/ginkgo-root-password-synced

[[ -f "$PWFILE" ]] || exit 0

want=$(tr -d '\r\n' <"$PWFILE")
[[ -n "$want" ]] || exit 0

if [[ -f "$MARKER" ]] && [[ "$(cat "$MARKER")" == "$want" ]]; then
	exit 0
fi

echo "root:${want}" | chpasswd
install -d -m 755 /var/lib
printf '%s' "$want" >"$MARKER"
chmod 600 "$MARKER"
logger -t ensure-root-password "root password synced from $PWFILE"
