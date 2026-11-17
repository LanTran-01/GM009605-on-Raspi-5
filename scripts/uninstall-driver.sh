#!/usr/bin/env bash
# Unbind and remove the GM009605 driver, overlay and udev rule.
# Usage: sudo scripts/uninstall-driver.sh
set -euo pipefail
source "$(dirname "$0")/common.sh"
require_root "$@"

log "Unbinding runtime-created devices"
for d in /sys/bus/i2c/devices/*-00*; do
	[[ -e $d/name ]] || continue
	if [[ $(cat "$d/name") == gm009605 && ! -e $d/of_node ]]; then
		bus=$(basename "$d"); bus=${bus%%-*}
		addr=0x$(basename "$d" | cut -d- -f2)
		echo "$addr" >"/sys/bus/i2c/devices/i2c-${bus}/delete_device"
		ok "deleted $(basename "$d")"
	fi
done

if lsmod | grep -q '^gm009605'; then
	rmmod gm009605 && ok "module unloaded"
fi

log "Removing installed files"
make -C "$REPO_ROOT/driver" uninstall >/dev/null
rm -f /etc/udev/rules.d/99-gm009605.rules
udevadm control --reload
ok "module, overlay and udev rule removed"

log "Cleaning $CONFIG_TXT"
sed -n -E 's/^[[:space:]]*(dtoverlay=gm009605.*)$/\1/p' "$CONFIG_TXT" | while read -r line; do
	remove_config_line "$line"
done
ok "done (I2C itself is left enabled)"
