#!/usr/bin/env bash
# Show live Raspberry Pi stats on the OLED through the driver's sysfs "text"
# attribute. Requires the kernel driver to be bound (docs/05-kernel-driver.md).
#
# Usage: scripts/sysinfo-demo.sh [interval_seconds]     (Ctrl+C to stop)
set -euo pipefail

interval="${1:-2}"
attr=$(ls -d /sys/bus/i2c/drivers/gm009605/*-00*/text 2>/dev/null | head -n1 || true)
[[ -n "$attr" ]] || { echo "gm009605 driver not bound - see docs/05-kernel-driver.md" >&2; exit 1; }

# Each write() to the sysfs "text" attribute redraws the whole screen, but
# bash's printf emits one write() per line. Buffer the full screen through
# dd (iflag=fullblock) so it reaches the driver as a single write().
write() {
	local sudo=""
	[[ -w "$attr" ]] || sudo=sudo
	printf '%s' "$1" | $sudo dd of="$attr" bs=4096 iflag=fullblock conv=notrunc status=none
}

trap 'write "Bye!"; exit 0' INT TERM

while :; do
	ip=$(hostname -I 2>/dev/null | awk '{print $1}')
	temp=$(awk '{printf "%.1fC", $1/1000}' /sys/class/thermal/thermal_zone0/temp)
	load=$(cut -d' ' -f1 /proc/loadavg)
	mem=$(free -m | awk '/^Mem:/{printf "%d/%dMB", $3, $2}')
	disk=$(df -h / | awk 'NR==2{print $3"/"$2}')
	up=$(uptime -p | sed 's/^up //; s/ hours\?/h/; s/ minutes\?/m/; s/ days\?/d/; s/,//g')

	write "$(printf '%s\n%s\n---------------------\nIP   %s\nCPU  %s  ld %s\nRAM  %s\nDISK %s\nUP   %s' \
		"$(hostname)" "$(date '+%a %d %b  %H:%M:%S')" "$ip" "$temp" "$load" "$mem" "$disk" "$up")"
	sleep "$interval"
done
