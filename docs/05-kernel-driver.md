# 5. Kernel driver: build, load ("mount") and use

[← Connection test](04-connection-test.md) · [Next: Troubleshooting →](06-troubleshooting.md)

`driver/gm009605.c` is an out-of-tree Linux **I²C client driver**. Once it is bound to the display, any program, or a plain `echo` in the shell, can use the OLED without knowing anything about the SSD1306.

## Architecture

```
 user space        echo "Hi" > .../text        cat image.bin > /dev/gm009605
 ─────────────────────────│───────────────────────────────│───────────────
                     sysfs attributes             misc char device
                     (contrast, text, …)          (read/write/lseek)
                          └──────────────┬────────────────┘
 gm009605.ko                   1 KiB shadow framebuffer  ── mutex ──
                                         │ gm_flush(): 8 × 129-byte writes
                                  i2c_master_send()
 ────────────────────────────────────────│─────────────────────────────────
 I²C core / i2c-designware (RP1)         │       bound via DT overlay
                                         ▼       or i2c-1/new_device
                                 GM009605 @ 0x3C
```

Design notes:

| Topic | Choice |
|---|---|
| Binding | Device Tree `compatible = "lantran,gm009605"` (overlay), **or** an I²C id-table entry `gm009605` for runtime `new_device` instantiation |
| Framebuffer | 1024-byte shadow copy in SSD1306 page format. Every change is flushed in full (~25 ms at 400 kHz) |
| I²C transfers | One command write plus 8 data writes of 1 + 128 bytes, which stays below the limits of every Pi I²C controller |
| Concurrency | One mutex serialises framebuffer access and the bus. sysfs and `/dev` users can run concurrently |
| Lifetime | The driver state is reference-counted (`kref`). Unbinding while `/dev/gm009605` is open is safe: open handles then get `-ENODEV` |
| Multiple panels | First panel is `/dev/gm009605`, the next ones `/dev/gm009605-1`, `-2`, … (IDA-allocated) |
| Shutdown | `.shutdown` blanks the panel on reboot/poweroff to limit OLED burn-in |
| Remove | Panel is switched off on `rmmod` / unbind |

## Prerequisites

* I²C enabled and the [connection test](04-connection-test.md) passing.
* Kernel headers for the **running** kernel: `ls /lib/modules/$(uname -r)/build`. If it is missing, run `sudo apt install linux-headers-$(uname -r)`.

> **Kernel upgrades.** A module only loads into the exact kernel it was built for (`vermagic`). After `apt full-upgrade` installs a new kernel and you reboot, run `sudo scripts/install-driver.sh` again.

## Build

```bash
make driver            # or: make -C driver
```

| Output | What it is |
|---|---|
| `driver/gm009605.ko` | The loadable kernel module |
| `driver/gm009605.dtbo` | Device Tree overlay that declares the OLED on I2C1 |

Inspect the module:

```bash
modinfo driver/gm009605.ko
#   description: GM009605 / SSD1306 128x64 I2C OLED driver
#   alias:       i2c:gm009605
#   alias:       of:N*T*Clantran,gm009605
#   vermagic:    6.18.x+rpt-rpi-2712 SMP preempt mod_unload modversions aarch64
```

## Load the driver: two ways

### Method 1: runtime (temporary, great for development)

Nothing is written to `/boot`, and a reboot undoes everything.

```bash
sudo scripts/install-driver.sh --now
```

which does exactly this:

```bash
sudo insmod driver/gm009605.ko                                         # load the code
echo gm009605 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/new_device    # declare the device
```

The I²C core creates the client `1-003c`, matches the name against the driver's id table and calls `gm_probe()`. The panel shows **"GM009605 ready"**.

To unload:

```bash
echo 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/delete_device
sudo rmmod gm009605
```

### Method 2: persistent (Device Tree overlay, loads at every boot)

```bash
sudo scripts/install-driver.sh          # add --addr 0x3d for the alternate address
sudo reboot
```

which:

1. installs the module to `/lib/modules/$(uname -r)/extra/` and runs `depmod`,
2. copies `gm009605.dtbo` to `/boot/firmware/overlays/`,
3. adds `dtoverlay=gm009605` to `/boot/firmware/config.txt` (after making a backup),
4. installs a udev rule so the `i2c` group can use the driver without `sudo`.

At boot the firmware merges the overlay into the device tree. The kernel creates the I²C client `1-003c` with `compatible = "lantran,gm009605"`, udev sees the `of:` modalias and runs `modprobe gm009605`, and the driver probes. You don't need any `/etc/modules` entry.

The overlay source ([`driver/gm009605-overlay.dts`](../driver/gm009605-overlay.dts)):

```dts
fragment@0 {
    target = <&i2c_arm>;              /* I2C1 on GPIO2/3 = /dev/i2c-1 */
    __overlay__ {
        gm009605: oled@3c {
            compatible = "lantran,gm009605";
            reg = <0x3c>;
        };
    };
};
__overrides__ { addr = <&gm009605>, "reg:0"; };   /* dtoverlay=gm009605,addr=0x3d */
```

To remove everything, run `sudo scripts/uninstall-driver.sh && sudo reboot`.

### Verify

```bash
lsmod | grep gm009605
dmesg | grep gm009605
#   gm009605 1-003c: 128x64 OLED ready on /dev/gm009605
ls -l /dev/gm009605
ls /sys/bus/i2c/drivers/gm009605/            # -> 1-003c
i2cdetect -y 1                               # 0x3c now shows as UU
```

## Using the driver

All examples below assume `D=/sys/bus/i2c/devices/1-003c`. Without the udev rule, use `| sudo tee` instead of `>`.

### sysfs control attributes

| Attribute | Mode | Values | Effect |
|---|:---:|---|---|
| `text` | w | any string | Clears the screen and renders text. `\n` = new line, long lines wrap (21 cols × 8 rows) |
| `clear` | w | anything | Blank the screen |
| `test_pattern` | w | `fill` `checker` `stripes` `border` | Draw a test pattern |
| `contrast` | rw | `0`–`255` | Brightness (default 207) |
| `invert` | rw | `0` / `1` | Black-on-white |
| `display_on` | rw | `0` / `1` | Sleep / wake the panel (RAM content is kept) |
| `flip` | rw | `0` / `1` | Rotate 180° |

```bash
D=/sys/bus/i2c/devices/1-003c
echo "Hello from the kernel!" > $D/text
# multi-line: each write() redraws the whole screen, and bash's printf issues
# one write() per line, so buffer through dd to send it in one write()
printf 'Line 1\nLine 2\nLine 3' | dd of=$D/text bs=4096 iflag=fullblock conv=notrunc status=none
echo checker > $D/test_pattern
echo 40  > $D/contrast
echo 1   > $D/invert
echo 1   > $D/flip
cat $D/contrast
```

### `/dev/gm009605` character device

A 1024-byte file that maps 1:1 onto the display RAM: byte `n` is page `n / 128`, column `n % 128`, and bit 0 is the top pixel of that page. Writes are flushed to the panel immediately and support `lseek()` for partial updates.

```bash
# full white, then black
head -c 1024 /dev/zero | tr '\0' '\377' > /dev/gm009605
head -c 1024 /dev/zero               > /dev/gm009605

# random noise ("TV static")
head -c 1024 /dev/urandom > /dev/gm009605

# save and restore the current screen
cat /dev/gm009605 > screen.bin
cat screen.bin    > /dev/gm009605

# update only text line 3 (page 3 = offset 384, 128 bytes)
head -c 128 /dev/urandom | dd of=/dev/gm009605 bs=128 seek=3 conv=notrunc
```

From C:

```c
int fd = open("/dev/gm009605", O_WRONLY);
uint8_t fb[1024] = { 0 };
fb[3 * 128 + 10] = 0xFF;         /* 8-pixel vertical bar at x=10, page 3 */
pwrite(fd, fb, sizeof(fb), 0);
```

From Python:

```python
fb = bytearray(1024)
for x in range(128):
    fb[x] |= 0x01; fb[7*128 + x] |= 0x80        # top and bottom border lines
with open("/dev/gm009605", "wb", buffering=0) as f:
    f.write(fb)
```

### Live system monitor demo

```bash
scripts/sysinfo-demo.sh        # refreshes every 2 s, Ctrl+C to stop
```

```
┌─────────────────────┐
│lantran              │
│Tue 17 Nov  21:45:02 │
│---------------------│
│IP   192.168.0.103   │
│CPU  51.6C  ld 0.09  │
│RAM  1512/8063MB     │
│DISK 9.4G/117G       │
│UP   27m             │
└─────────────────────┘
```

## Code walkthrough

| Function | Role |
|---|---|
| `gm_probe()` | Allocates state, runs `gm_hw_init()` (the full SSD1306 power-up sequence), shows a boot message and registers the misc device |
| `gm_hw_init()` | Display off → clock → mux → offset → **charge pump on** → horizontal addressing → orientation → contrast → flush RAM → display on |
| `gm_flush()` | Sets the column/page window to the full screen and streams the 1 KiB buffer |
| `gm_send_cmds()` | Prefixes `0x00` and sends a command sequence in one transfer |
| `gm_draw_text()` / `gm_draw_pattern()` | Render into the shadow buffer (no I/O) |
| `gm_write()` / `gm_read()` / `gm_llseek()` | Char-device file operations on the shadow buffer |
| `*_show()` / `*_store()` | sysfs attributes, grouped via `ATTRIBUTE_GROUPS(gm)` and attached through `driver.dev_groups` |
| `gm_remove()` | Deregisters `/dev`, blanks the panel, drops the driver's reference |
| `gm_shutdown()` | Blanks the panel on reboot/poweroff |

## Alternative: the in-tree `ssd130x` DRM driver

The Raspberry Pi kernel also ships a generic driver for this controller (`ssd130x-i2c`, enabled with `dtoverlay=ssd1306`). It exposes the display as `/dev/fb1` / a DRM device. That suits running a console or desktop toolkit on it, but it is a large generic DRM stack. This project's driver is deliberately small and readable, which makes it a better fit for learning and for simple status displays. **Don't enable both overlays at once**, because only one driver can own address `0x3C`.

Next: [Troubleshooting →](06-troubleshooting.md)
