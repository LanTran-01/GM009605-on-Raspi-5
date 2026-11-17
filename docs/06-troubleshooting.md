# 6. Troubleshooting

[← Kernel driver](05-kernel-driver.md) · [README](../README.md)

## Quick diagnosis

```bash
grep -n i2c /boot/firmware/config.txt          # dtparam=i2c_arm=on present?
ls -l /dev/i2c-1                               # bus exists?
i2cdetect -y 1                                 # 3c / UU / -- ?
lsmod | grep gm009605                          # module loaded?
dmesg | grep -iE 'gm009605|i2c' | tail        # driver messages
ls /sys/bus/i2c/devices/                       # 1-003c present?
```

## Symptoms

### `/dev/i2c-1` does not exist
* `dtparam=i2c_arm=on` is missing or commented out in `/boot/firmware/config.txt`. Run `sudo scripts/enable-i2c.sh`, then reboot.
* Make sure you edited `/boot/firmware/config.txt`. On current Raspberry Pi OS, `/boot/config.txt` is only a stub.

### Nothing at 0x3C
`i2cdetect -y 1` shows only `--`.

1. **Power:** measure VCC to GND on the module. It should read about 3.3 V.
2. **VCC/GND swapped:** check the silkscreen. Some batches use the order `VCC GND SCL SDA`. A reversed module often gets warm. Power off immediately.
3. **SDA/SCL swapped:** SDA → pin 3, SCL → pin 5.
4. **Loose Dupont connector:** wiggle-test, or replace the jumpers.
5. **Other address:** look for `3d` in the scan.

### `UU` at 0x3C, but you didn't load this driver
Another driver owns the address, usually `ssd130x-i2c` from `dtoverlay=ssd1306`. Remove that line from `config.txt` and reboot.

### Test tool: `Device or resource busy`
This project's driver is bound. Unload it while you run the raw test:
```bash
echo 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/delete_device   # runtime binding
# or: sudo scripts/uninstall-driver.sh && sudo reboot           # overlay binding
```

### `insmod: ERROR: could not insert module: Invalid module format`
The module was built for a different kernel. Compare `uname -r` with `modinfo -F vermagic driver/gm009605.ko`. Rebuild with `make -C driver clean all`, or run `sudo scripts/install-driver.sh` again. This typically happens after an `apt full-upgrade` followed by a reboot.

### `make: *** /lib/modules/.../build: No such file or directory`
```bash
sudo apt install linux-headers-$(uname -r)
```

### `dmesg`: `gm009605 1-003c: no response at 0x3c - check wiring`
The driver was bound, but the very first command write failed. This is the same problem as [Nothing at 0x3C](#nothing-at-0x3c).

### `Permission denied` on `/dev/gm009605` or sysfs
Use `sudo tee`, or install the udev rule (`sudo scripts/install-driver.sh` does this) and make sure you are in the `i2c` group (`groups`). Log out and back in after `usermod`.

### Display works but is dim, flickers, or shows garbage
* Shorten the wires and slow the bus down: remove `i2c_arm_baudrate=400000`.
* Garbage in the bottom half usually means a 128×32 module was sold as 128×64. This driver targets 128×64 only.
* Lower `contrast` to 0x40 or below if the panel looks washed out.

### Image is upside-down
```bash
echo 1 | sudo tee /sys/bus/i2c/devices/1-003c/flip
```

### Burn-in
OLED pixels age with use. Avoid leaving a static image at full contrast for days. Use `display_on=0` when you don't need the screen; the driver also blanks the panel on shutdown.

## Still stuck?

Open an issue with the output of:

```bash
uname -a; cat /proc/device-tree/model; echo
grep -nE 'i2c|dtoverlay' /boot/firmware/config.txt
i2cdetect -y 1
dmesg | grep -iE 'gm009605|i2c'
```
