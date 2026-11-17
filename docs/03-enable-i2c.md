# 3. Enable I²C on the Raspberry Pi 5

[← Wiring](02-wiring.md) · [Next: Connection test →](04-connection-test.md)

Pins 3 and 5 belong to the RP1 I/O controller's **I2C1** block. Linux exposes it as `/dev/i2c-1` once the `i2c_arm` device-tree parameter is enabled.

## Option A: helper script (recommended)

```bash
sudo scripts/enable-i2c.sh          # 100 kHz (default)
sudo scripts/enable-i2c.sh --fast   # 400 kHz, ~4x faster screen updates
```

The script is idempotent. It:

1. installs `i2c-tools`, `build-essential`, `device-tree-compiler` and the kernel headers for the running kernel,
2. adds `dtparam=i2c_arm=on` (and optionally `dtparam=i2c_arm_baudrate=400000`) to `/boot/firmware/config.txt`, after making a timestamped backup,
3. loads `i2c-dev` now and at every boot,
4. adds your user to the `i2c` group so `/dev/i2c-1` works without `sudo`.

Reboot if the script tells you to.

## Option B: by hand

```bash
sudo raspi-config            # Interface Options → I2C → Enable
# or edit the file directly:
sudoedit /boot/firmware/config.txt
#   dtparam=i2c_arm=on
#   dtparam=i2c_arm_baudrate=400000   # optional
sudo apt install i2c-tools
sudo usermod -aG i2c "$USER"
sudo reboot
```

## Verify: scan the bus

```bash
ls /dev/i2c-1
i2cdetect -y 1
```

With the OLED wired correctly you should see `3c`:

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- 3c -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
...
```

| `i2cdetect` shows | Meaning |
|---|---|
| `3c` | Device found and nothing has claimed it. Go to the [connection test](04-connection-test.md). |
| `UU` | A kernel driver (this one, or `ssd130x`) owns the address. That's expected once the driver is loaded. |
| `--` everywhere | Wiring or power problem. See [Troubleshooting](06-troubleshooting.md#nothing-at-0x3c). |
| A number on every address | SDA/SCL shorted or stuck low. Power off and re-check the wires. |

> Other buses: `/dev/i2c-13` and `/dev/i2c-14` (sometimes 11/12) are the HDMI DDC channels for your monitors. Ignore them.

Next: [Connection test →](04-connection-test.md)
