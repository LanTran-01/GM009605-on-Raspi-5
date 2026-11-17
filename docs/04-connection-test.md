# 4. Connection test (no driver needed)

[← Enable I²C](03-enable-i2c.md) · [Next: Kernel driver →](05-kernel-driver.md)

Before building a kernel module, prove that the hardware works. `test/gm009605_test` is a small C program that talks to the SSD1306 directly through the generic **i2c-dev** interface (`/dev/i2c-1`). No driver, Python or library is needed.

## Build and run

```bash
make test                 # or: make -C test
./test/gm009605_test      # or: make check
```

Options:

```
-b <bus>    I2C bus number      (default 1)
-a <addr>   7-bit address       (default 0x3c)
-q          quiet, errors only (exit code 0 = pass)
```

## What it does

| Step | What you should see on the OLED | What it proves |
|---|---|---|
| Open I2C bus | (nothing) | `/dev/i2c-1` exists and you have permission |
| Probe (ACK check) | (nothing) | Something at `0x3C` acknowledged a write |
| Initialise SSD1306 | screen may flicker | Command path works |
| All pixels on | entire panel lit for ~1 s | Charge pump, panel and every pixel are good |
| Checkerboard | fine checker pattern | Data (GDDRAM) writes work |
| Invert / normal | pattern flips briefly | Command/data interleaving works |
| Text banner | bordered "GM009605 OK" screen | Font and page addressing work, orientation is correct |

Expected terminal output:

```
GM009605 connection test  (/dev/i2c-1, address 0x3c)

  [ OK ] Open I2C bus
  [ OK ] Probe device (ACK check)
  [ OK ] Initialise SSD1306
  [ OK ] All pixels on
  [ OK ] Checkerboard pattern
  [ OK ] Invert / normal
  [ OK ] Text banner

All steps passed. The banner stays on screen.
```

Expected screen:

```
┌──────────────────────────────┐
│         GM009605 OK          │
│                              │
│        Raspberry Pi 5        │
│                              │
│      bus 1  addr 0x3C        │
│      128 x 64 SSD1306        │
└──────────────────────────────┘
```

## How it works (in 20 lines)

```c
fd = open("/dev/i2c-1", O_RDWR);
ioctl(fd, I2C_SLAVE, 0x3c);                 // select the slave address

// Command write: control byte 0x00 + commands
write(fd, (uint8_t[]){0x00, 0xAE, 0x8D, 0x14, ..., 0xAF}, n);

// Data write: set the window, then control byte 0x40 + pixels
write(fd, (uint8_t[]){0x00, 0x21, 0, 127, 0x22, 0, 7}, 7);
for (page = 0; page < 8; page++)
        write(fd, [0x40, fb[page*128 ... page*128+127]], 129);
```

The complete init sequence is in [`test/gm009605_test.c`](../test/gm009605_test.c) (`display_init()`), with the constants named in [`common/ssd1306.h`](../common/ssd1306.h).

## If a step fails

| Failure | Likely cause |
|---|---|
| `cannot open /dev/i2c-1` | I²C not enabled, or you're not in the `i2c` group (log out and back in) |
| `I2C_SLAVE 0x3c: Device or resource busy` | The kernel driver is bound. Unload it first (`sudo scripts/uninstall-driver.sh`) |
| `Remote I/O error` at *Probe* | No ACK: wiring, power or wrong address. See [Troubleshooting](06-troubleshooting.md) |
| All steps OK but the screen is black | Most likely a counterfeit or damaged panel. Try `-a 0x3d` and re-check VCC |
| Image upside-down or mirrored | Mount the module the other way round, or use the driver's `flip` attribute |

Next: [Kernel driver →](05-kernel-driver.md)
