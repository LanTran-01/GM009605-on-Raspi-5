#!/usr/bin/env bash
# Enable the header I2C bus (/dev/i2c-1) and install the tools needed by
# this project. Idempotent: safe to run more than once.
#
# Usage: sudo scripts/enable-i2c.sh [--fast]
#   --fast   also set the bus to 400 kHz (faster screen refresh)
set -euo pipefail
source "$(dirname "$0")/common.sh"
require_root "$@"

fast=0
[[ "${1:-}" == "--fast" ]] && fast=1

log "Installing packages"
apt-get install -y --no-install-recommends i2c-tools build-essential device-tree-compiler \
	"linux-headers-$(uname -r)" >/dev/null || warn "some packages failed to install"
ok "i2c-tools, build-essential, dtc, kernel headers"

log "Configuring $CONFIG_TXT"
changed=0
ensure_config_line "dtparam=i2c_arm=on" && changed=1
if [[ $fast -eq 1 ]]; then
	ensure_config_line "dtparam=i2c_arm_baudrate=400000" && changed=1
fi

log "Loading i2c-dev"
modprobe i2c-dev
grep -qx 'i2c-dev' /etc/modules 2>/dev/null || echo 'i2c-dev' >>/etc/modules
ok "i2c-dev loaded and set to load at boot"

user="${SUDO_USER:-}"
if [[ -n "$user" ]] && ! id -nG "$user" | grep -qw i2c; then
	usermod -aG i2c "$user"
	ok "added $user to group i2c (log out/in to take effect)"
fi

if [[ -e /dev/i2c-1 && $changed -eq 0 ]]; then
	ok "/dev/i2c-1 is available - no reboot needed"
	echo
	i2cdetect -y 1
else
	warn "reboot required: sudo reboot"
fi
