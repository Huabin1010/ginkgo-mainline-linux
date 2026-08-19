#!/bin/bash
# Apply CN regulatory after userdata firmware is available.
# Do not bounce wlan0 here: link down/up races with ath10k scan and can
# crash WLAN.HL.3.x during bring-up.
set -euo pipefail

for _ in $(seq 1 60); do
	[[ -e /lib/firmware/regulatory.db ]] && break
	sleep 1
done

if [[ -e /lib/firmware/regulatory.db ]]; then
	iw reg reload >/dev/null 2>&1 || true
	iw reg set CN >/dev/null 2>&1 || true
fi
