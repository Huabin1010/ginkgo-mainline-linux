# Shared helpers for the phone root password. Source from other scripts.
# Password is never committed: overlay file is gitignored.

ginkgo_load_root_password() {
	if [[ -n "${GINKGO_ROOT_PASSWORD:-}" ]]; then
		return 0
	fi
	local root="${GINKGO_REPO_ROOT:-}"
	if [[ -z "$root" ]]; then
		root="$(cd "$(dirname "${BASH_SOURCE[1]}")/.." && pwd)"
	fi
	local f="$root/rootfs-overlay/etc/ginkgo-root-password"
	if [[ -f "$f" ]]; then
		GINKGO_ROOT_PASSWORD="$(tr -d '\n' < "$f")"
		return 0
	fi
	echo "error: set GINKGO_ROOT_PASSWORD or create $f" >&2
	return 1
}

ginkgo_sudo() {
	if [[ -n "${SUDO_PASSWORD:-}" ]]; then
		echo "$SUDO_PASSWORD" | sudo -S "$@"
	else
		sudo "$@"
	fi
}
