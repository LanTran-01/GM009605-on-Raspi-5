#!/usr/bin/env bash
# Build and install the GM009605 kernel driver.
#
# Usage:
#   sudo scripts/install-driver.sh            permanent: module + DT overlay,
#                                             auto-loads at every boot (reboot)
#   sudo scripts/install-driver.sh --now      temporary: insmod + bind at
#                                             runtime, nothing persisted
#   sudo scripts/install-driver.sh --addr 0x3d ...   non-default address
set -euo pipefail
source "$(dirname "$0")/common.sh"
require_root "$@"

mode=persistent
addr=0x3c
bus=1
while [[ $# -gt 0 ]]; do
	case "$1" in
	--now)  mode=runtime ;;
	--addr) addr="$2"; shift ;;
	--bus)  bus="$2"; shift ;;
	*) die "unknown option: $1" ;;
	esac
	shift
done
addr_hex=$(printf '%04x' "$((addr))")
client="/sys/bus/i2c/devices/${bus}-${addr_hex}"

[[ -e /dev/i2c-${bus} ]] || die "/dev/i2c-${bus} missing - run scripts/enable-i2c.sh first"
[[ -d /lib/modules/$(uname -r)/build ]] || die "kernel headers missing: apt install linux-headers-$(uname -r)"

# Build as the invoking user so the repo doesn't fill up with root-owned files.
log "Building module and overlay for $(uname -r)"
if [[ -n "${SUDO_USER:-}" ]]; then
	sudo -u "$SUDO_USER" make -C "$REPO_ROOT/driver" >/dev/null
else
	make -C "$REPO_ROOT/driver" >/dev/null
fi
ok "driver/gm009605.ko, driver/gm009605.dtbo"

log "Installing udev rule"
install -m 0644 "$REPO_ROOT/udev/99-gm009605.rules" /etc/udev/rules.d/
udevadm control --reload
ok "/etc/udev/rules.d/99-gm009605.rules"

if [[ $mode == runtime ]]; then
	log "Loading driver at runtime (not persistent)"
	lsmod | grep -q '^gm009605' || insmod "$REPO_ROOT/driver/gm009605.ko"
	if [[ ! -e $client ]]; then
		echo "gm009605 $addr" >"/sys/bus/i2c/devices/i2c-${bus}/new_device"
	fi
	sleep 0.5
	if [[ -e /dev/gm009605 ]]; then
		ok "bound: $client -> /dev/gm009605"
	else
		dmesg | grep gm009605 | tail -n 3 >&2
		die "driver did not bind - see dmesg above and docs/06-troubleshooting.md"
	fi
	exit 0
fi

log "Installing module and overlay"
make -C "$REPO_ROOT/driver" install >/dev/null
ok "/lib/modules/$(uname -r)/extra/gm009605.ko"
ok "/boot/firmware/overlays/gm009605.dtbo"

log "Enabling overlay in $CONFIG_TXT"
if [[ $addr_hex == 003c ]]; then
	ensure_config_line "dtoverlay=gm009605" || true
else
	ensure_config_line "dtoverlay=gm009605,addr=${addr}" || true
fi

echo
warn "Reboot to activate: sudo reboot"
echo "    After reboot, check:  ls -l /dev/gm009605 && dmesg | grep gm009605"
