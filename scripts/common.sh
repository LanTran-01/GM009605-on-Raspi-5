# shellcheck shell=bash
# Shared helpers for the GM009605 scripts. Source, don't execute.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_TXT="${CONFIG_TXT:-/boot/firmware/config.txt}"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m [!]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m [x]\033[0m %s\n' "$*" >&2; exit 1; }

require_root() {
	[[ $EUID -eq 0 ]] || die "run with sudo: sudo $0 $*"
}

# Back up config.txt once per run before the first modification.
_config_backed_up=0
backup_config() {
	if [[ $_config_backed_up -eq 0 ]]; then
		cp -a "$CONFIG_TXT" "$CONFIG_TXT.bak.$(date +%Y%m%d-%H%M%S)"
		_config_backed_up=1
	fi
}

# ensure_config_line "dtparam=i2c_arm=on"
# Un-comments the line if present as a comment, appends it otherwise.
ensure_config_line() {
	local line="$1" esc
	esc="$(printf '%s' "$line" | sed 's/[][\.*^$/]/\\&/g')"
	if grep -qE "^[[:space:]]*${esc}[[:space:]]*$" "$CONFIG_TXT"; then
		ok "config.txt already has: $line"
		return 1
	fi
	backup_config
	if grep -qE "^[[:space:]]*#[[:space:]]*${esc}[[:space:]]*$" "$CONFIG_TXT"; then
		sed -i -E "s/^[[:space:]]*#[[:space:]]*${esc}[[:space:]]*$/${esc}/" "$CONFIG_TXT"
	else
		printf '%s\n' "$line" >>"$CONFIG_TXT"
	fi
	ok "config.txt += $line"
	return 0
}

remove_config_line() {
	local line="$1" esc
	esc="$(printf '%s' "$line" | sed 's/[][\.*^$/]/\\&/g')"
	if grep -qE "^[[:space:]]*${esc}[[:space:]]*$" "$CONFIG_TXT"; then
		backup_config
		sed -i -E "/^[[:space:]]*${esc}[[:space:]]*$/d" "$CONFIG_TXT"
		ok "config.txt -= $line"
	fi
}
