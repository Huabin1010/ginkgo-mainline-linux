#!/bin/sh
# Join / switch Wi-Fi like iOS:
#   - do not disconnect the current AP first
#   - never reuse a profile that needs a secret agent (HUD has none)
#   - persist PSK with psk-flags=0
#   - on failure the previous network stays up
# Args: SSID  PASS  IFACE  SECURITY
#    or: forget  SSID  IFACE
ACTION=""
if [ "${1:-}" = forget ]; then
	ACTION=forget
	SSID="${2:-}"
	PASS=""
	IFACE="${3:-}"
	SEC=""
else
	SSID="${1:-}"
	PASS="${2:-}"
	IFACE="${3:-}"
	SEC="${4:-}"
fi
LOG="${GEMINI_WIFI_LOG:-/var/log/ginkgo-wifi.log}"
export LC_ALL=C
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SYSTEM_BUS_ADDRESS:-unix:path=/run/dbus/system_bus_socket}"

if [ -z "$SSID" ]; then
	echo "未指定网络名称"
	exit 1
fi

if [ -z "${NMCLI:-}" ]; then
	NMCLI="$(command -v nmcli || true)"
fi
if [ -z "$NMCLI" ]; then
	echo "未找到 nmcli"
	exit 1
fi

# Forget is a tap: delete profiles and return. Do not toggle radio first.
if [ "$ACTION" = forget ]; then
	"$NMCLI" -w 3 connection delete id "$SSID" >/dev/null 2>&1 || true
	"$NMCLI" -w 3 connection delete id "gemini-$SSID" >/dev/null 2>&1 || true
	"$NMCLI" -t -f UUID,TYPE,NAME connection show 2>/dev/null | while IFS=: read -r uuid type name; do
		[ "$type" = "802-11-wireless" ] || [ "$type" = "wifi" ] || continue
		s=$("$NMCLI" -g 802-11-wireless.ssid connection show "$uuid" 2>/dev/null || true)
		if [ "$s" = "$SSID" ] || [ "$name" = "$SSID" ] || [ "$name" = "gemini-$SSID" ]; then
			"$NMCLI" -w 3 connection delete uuid "$uuid" >/dev/null 2>&1 || true
		fi
	done
	inuse=$("$NMCLI" -t -f IN-USE,SSID device wifi list --rescan no 2>/dev/null |
		awk -F: '$1=="*" || $1=="yes" || $1=="Yes" { print $2; exit }')
	if [ "$inuse" = "$SSID" ]; then
		if [ -n "$IFACE" ]; then
			"$NMCLI" -w 3 device disconnect "$IFACE" >/dev/null 2>&1 || true
		else
			"$NMCLI" -t -f DEVICE,TYPE device status 2>/dev/null |
				awk -F: '$2=="wifi" { print $1 }' |
				while read -r d; do
					[ -n "$d" ] || continue
					"$NMCLI" -w 3 device disconnect "$d" >/dev/null 2>&1 || true
				done
		fi
	fi
	echo "已忘记 $SSID"
	exit 0
fi

log() { echo "$*" >>"$LOG" 2>/dev/null || true; }
run() { "$@" >>"$LOG" 2>&1; }

echo "=== $(date -Is 2>/dev/null || date) ssid=$SSID if=$IFACE sec=$SEC pass=$([ -n "$PASS" ] && echo yes || echo no) ===" >>"$LOG" 2>/dev/null || true

run "$NMCLI" radio wifi on || true
run "$NMCLI" networking on || true
if [ -n "$IFACE" ]; then
	run "$NMCLI" device set "$IFACE" managed yes || true
	run "$NMCLI" device set "$IFACE" autoconnect yes || true
fi

is_open() {
	case "$SEC" in
	""|"--"|"none"|"None"|"OPEN"|"Open") return 0 ;;
	esac
	return 1
}

is_enterprise() {
	case "$SEC" in
	*802.1*|*EAP*|*Enterprise*|*enterprise*) return 0 ;;
	esac
	return 1
}

key_mgmt() {
	case "$SEC" in
	*SAE*|*WPA3*)
		case "$SEC" in
		*WPA2*|*WPA1*|*PSK*) echo wpa-psk ;;
		*) echo sae ;;
		esac
		;;
	*) echo wpa-psk ;;
	esac
}

# Print UUIDs of wifi profiles whose broadcast SSID matches.
uuids_for_ssid() {
	"$NMCLI" -t -f UUID,TYPE connection show 2>/dev/null | while IFS=: read -r uuid type; do
		[ -n "$uuid" ] || continue
		[ "$type" = "802-11-wireless" ] || [ "$type" = "wifi" ] || continue
		s=$("$NMCLI" -g 802-11-wireless.ssid connection show "$uuid" 2>/dev/null || true)
		[ "$s" = "$SSID" ] && echo "$uuid"
	done
	# leftover names from older HUD builds
	"$NMCLI" -t -f NAME,UUID,TYPE connection show 2>/dev/null | while IFS=: read -r name uuid type; do
		[ "$type" = "802-11-wireless" ] || [ "$type" = "wifi" ] || continue
		[ "$name" = "gemini-$SSID" ] && echo "$uuid"
	done
}

delete_ssid_profiles() {
	uuids_for_ssid | awk 'NF && !seen[$0]++' | while read -r uuid; do
		log "delete profile $uuid"
		run "$NMCLI" connection delete "$uuid" || true
	done
}

up_uuid() {
	uuid="$1"
	if [ -n "$IFACE" ]; then
		run "$NMCLI" -w 30 connection up "$uuid" ifname "$IFACE"
	else
		run "$NMCLI" -w 30 connection up "$uuid"
	fi
}

up_name() {
	name="$1"
	if [ -n "$IFACE" ]; then
		run "$NMCLI" -w 30 connection up "$name" ifname "$IFACE"
	else
		run "$NMCLI" -w 30 connection up "$name"
	fi
}

PSKFILE="${TMPDIR:-/tmp}/gemini-wifi-psk.$$"
GAVE_PSK=0

write_psk_file() {
	umask 077
	printf '802-11-wireless-security.psk:%s\n' "$1" >"$PSKFILE"
}

up_with_psk() {
	id="$1"
	psk="$2"
	if [ -n "$psk" ]; then
		GAVE_PSK=1
		write_psk_file "$psk"
		if [ -n "$IFACE" ]; then
			run "$NMCLI" -w 30 connection up "$id" ifname "$IFACE" passwd-file "$PSKFILE"
		else
			run "$NMCLI" -w 30 connection up "$id" passwd-file "$PSKFILE"
		fi
	else
		if [ "$id" = "$SSID" ]; then
			up_name "$id"
		else
			up_uuid "$id"
		fi
	fi
}

stored_psk() {
	"$NMCLI" -s -g 802-11-wireless-security.psk connection show "$1" 2>/dev/null || true
}

is_wrong_pw() {
	grep -qiE 'psk mismatch|wrong password|bad password|handshake timeout|4-way handshake' "$ATTEMPT" 2>/dev/null && return 0
	if [ "$GAVE_PSK" = 1 ] && grep -qi 'secrets were required' "$ATTEMPT" 2>/dev/null; then
		return 0
	fi
	return 1
}

finish_ok() {
	echo "已连接 $SSID"
	cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
	rm -f "$ATTEMPT" "$PSKFILE"
	exit 0
}

fail_wrong_pw() {
	echo "密码不对，请再试一次"
	cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
	rm -f "$ATTEMPT" "$PSKFILE"
	exit 3
}

fail_need_pw() {
	echo "NEED_PASSWORD"
	cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
	rm -f "$ATTEMPT" "$PSKFILE"
	exit 2
}

fail_explain() {
	if is_wrong_pw; then
		fail_wrong_pw
	fi
	if grep -qiE 'could not be found|network could not' "$ATTEMPT" 2>/dev/null; then
		echo "找不到这个网络"
		cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
		rm -f "$ATTEMPT" "$PSKFILE"
		exit 1
	fi
	if grep -qiE 'Timeout expired|timeout expired' "$ATTEMPT" 2>/dev/null; then
		echo "连接超时"
		cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
		rm -f "$ATTEMPT" "$PSKFILE"
		exit 1
	fi
	if grep -qiE 'No suitable device|mismatching interface' "$ATTEMPT" 2>/dev/null; then
		echo "网卡不匹配"
		cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
		rm -f "$ATTEMPT" "$PSKFILE"
		exit 1
	fi
	if grep -qiE 'IP configuration|dhcp' "$ATTEMPT" 2>/dev/null; then
		echo "没拿到地址"
		cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
		rm -f "$ATTEMPT" "$PSKFILE"
		exit 1
	fi
	echo "未能加入此网络"
	cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
	rm -f "$ATTEMPT" "$PSKFILE"
	exit 1
}

IFADD="*"
[ -n "$IFACE" ] && IFADD="$IFACE"

ATTEMPT="${TMPDIR:-/tmp}/gemini-wifi-attempt.$$"
: >"$ATTEMPT"
run() { "$@" >>"$ATTEMPT" 2>&1; }

already_up() {
	live_ok
}

# Fully associated to $SSID: device state is connected (not connecting)
# and the in-use scan row is this SSID. Activating profiles must not count.
live_ok() {
	st=$("$NMCLI" -t -f DEVICE,TYPE,STATE,CONNECTION device status 2>/dev/null |
		awk -F: -v ifc="$IFACE" '
			$2=="wifi" {
				if (ifc=="" || $1==ifc) { print $3; exit }
			}')
	case "$st" in
	connected) ;;
	*) return 1 ;;
	esac
	"$NMCLI" -t -f IN-USE,SSID device wifi list --rescan no 2>/dev/null |
		awk -F: -v s="$SSID" '
			$1=="*" || $1=="yes" || $1=="Yes" {
				if ($2==s) { found=1; exit }
			}
			END { exit found ? 0 : 1 }'
}

wait_live() {
	tries=${GEMINI_WIFI_WAIT:-10}
	i=0
	while [ "$i" -le "$tries" ]; do
		if live_ok; then
			return 0
		fi
		if [ "$tries" = 0 ]; then
			break
		fi
		i=$((i + 1))
		[ "$i" -le "$tries" ] && sleep 1
	done
	return 1
}

if is_enterprise; then
	echo "暂不支持企业级网络"
	rm -f "$ATTEMPT" "$PSKFILE"
	exit 1
fi

if already_up; then
	finish_ok
fi

# --- no password: join a saved / open network (iOS tap-to-join) ---
if [ -z "$PASS" ]; then
	ok=0
	for uuid in $(uuids_for_ssid | awk 'NF && !seen[$0]++'); do
		km=$("$NMCLI" -g 802-11-wireless-security.key-mgmt connection show "$uuid" 2>/dev/null || true)
		flags=$("$NMCLI" -g 802-11-wireless-security.psk-flags connection show "$uuid" 2>/dev/null || true)
		psk=$(stored_psk "$uuid")
		log "saved uuid=$uuid km=$km flags=$flags psk=$([ -n "$psk" ] && echo yes || echo no)"
		case "$km" in
		""|"none"|"None")
			if up_uuid "$uuid" && wait_live; then
				ok=1
				break
			fi
			;;
		*)
			# This image's nmcli will not use the keyfile PSK unless it
			# is also passed with passwd-file. Saved-but-wrong PSK must
			# surface as 密码不对, not a blank password sheet.
			if [ -n "$psk" ]; then
				if up_with_psk "$uuid" "$psk"; then
					if wait_live; then
						ok=1
						break
					fi
				fi
				if is_wrong_pw; then
					fail_wrong_pw
				fi
				# Still on the previous AP is not a hard fail —
				# ask for a password instead of 未能加入.
			else
				case "$flags" in
				1|0x1|2|0x2)
					;;
				*)
					if up_uuid "$uuid" && wait_live; then
						ok=1
						break
					fi
					if is_wrong_pw; then
						fail_wrong_pw
					fi
					;;
				esac
			fi
			;;
		esac
	done
	if [ "$ok" = 1 ]; then
		finish_ok
	fi
	if already_up; then
		finish_ok
	fi
	if is_open; then
		delete_ssid_profiles
		run "$NMCLI" connection add type wifi con-name "$SSID" ifname "$IFADD" \
			ssid "$SSID" connection.autoconnect yes \
			connection.autoconnect-priority 10 || true
		if up_name "$SSID" && wait_live; then
			finish_ok
		fi
		echo "加入开放网络失败"
		cat "$ATTEMPT" >>"$LOG" 2>/dev/null || true
		rm -f "$ATTEMPT" "$PSKFILE"
		exit 1
	fi
	fail_need_pw
fi

# --- have password: always pass PSK via passwd-file (netplan/nmcli 1.54
# will not activate from the keyfile alone). ---
KM="$(key_mgmt)"
GAVE_PSK=1
existed=$(uuids_for_ssid | awk 'NF && !seen[$0]++ { print; exit }')
if [ -n "$existed" ]; then
	run "$NMCLI" connection modify "$existed" \
		wifi-sec.key-mgmt "$KM" \
		wifi-sec.psk "$PASS" \
		wifi-sec.psk-flags 0 || true
	log "modify uuid=$existed key-mgmt=$KM"
	if up_with_psk "$existed" "$PASS"; then
		if wait_live; then
			finish_ok
		fi
		fail_explain
	fi
	if is_wrong_pw; then
		fail_wrong_pw
	fi
fi

delete_ssid_profiles

run "$NMCLI" connection add type wifi con-name "$SSID" ifname "$IFADD" \
	ssid "$SSID" \
	connection.autoconnect yes \
	connection.autoconnect-priority 10 \
	wifi-sec.key-mgmt "$KM" \
	wifi-sec.psk "$PASS" \
	wifi-sec.psk-flags 0
add_rc=$?
log "add key-mgmt=$KM rc=$add_rc"
run "$NMCLI" connection modify "$SSID" \
	wifi-sec.psk "$PASS" \
	wifi-sec.psk-flags 0 || true

if up_with_psk "$SSID" "$PASS" && wait_live; then
	finish_ok
fi

if [ "$KM" = sae ]; then
	log "retry wpa-psk"
	run "$NMCLI" connection modify "$SSID" \
		wifi-sec.key-mgmt wpa-psk \
		wifi-sec.psk "$PASS" \
		wifi-sec.psk-flags 0 || true
	if up_with_psk "$SSID" "$PASS" && wait_live; then
		finish_ok
	fi
fi

if [ -n "$IFACE" ]; then
	run "$NMCLI" -w 30 device wifi connect "$SSID" password "$PASS" ifname "$IFACE" || true
else
	run "$NMCLI" -w 30 device wifi connect "$SSID" password "$PASS" || true
fi
if wait_live; then
	finish_ok
fi

if is_wrong_pw; then
	fail_wrong_pw
fi
fail_explain
