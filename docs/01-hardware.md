# 1. Hardware overview

[← README](../README.md) · [Next: Wiring →](02-wiring.md)

## The GM009605 module

Despite often being sold as an "LCD", the **GM009605** is a **0.96-inch monochrome OLED** module. The number is printed on the back of the PCB. The board carries a **Solomon Systech SSD1306** display controller, and the controller is what software actually talks to.

| Property | Value |
|---|---|
| Panel | 0.96″ passive-matrix OLED, self-emissive (no backlight) |
| Resolution | 128 × 64 pixels, 1 bit per pixel |
| Controller | SSD1306 |
| Interface | I²C, up to 400 kHz (Fast-mode) |
| I²C address | `0x3C` by default; `0x3D` with the address resistor moved (the silkscreen may say `0x78`/`0x7A`, which are the 8-bit forms) |
| Supply (VCC) | 3.3 V – 5 V (on-board LDO regulator) |
| Logic level | Follows VCC through the on-board pull-ups (see the warning below) |
| Current | ~10 mA typical, ~20 mA with every pixel lit |
| Panel voltage | Generated on-chip by the SSD1306 charge pump |
| Pins | 4: `GND`, `VCC`, `SCL`, `SDA` |

## How the controller works

```
            I²C                    ┌───────────────────── SSD1306 ─────────────────────┐
 Pi  ── SDA/SCL ──▶  control byte  │  0x00 → command decoder  (contrast, on/off, ...)  │
                     + payload     │  0x40 → GDDRAM 1 KiB  ──▶  row/col drivers ──▶ OLED │
                                   └───────────────────────────────────────────────────┘
```

* **Every I²C write starts with a control byte.** `0x00` means the bytes that follow are commands. `0x40` means they are pixel data.
* **Display RAM (GDDRAM)** is 1024 bytes, organised as **8 pages × 128 columns**. Each byte is a vertical strip of 8 pixels, with bit 0 at the top:

```
           column 0   column 1        column 127
 page 0  ┌─────────┬─────────┬ ... ┬─────────┐  rows 0-7   (byte bit0 = row 0)
 page 1  ├─────────┼─────────┼ ... ┼─────────┤  rows 8-15
   ...   │         │         │     │         │
 page 7  └─────────┴─────────┴ ... ┴─────────┘  rows 56-63
```

* **The charge pump must be enabled** (`0x8D 0x14`) before the display is switched on. The GM009605 has no external high-voltage supply, so if this step is missing the panel stays dark even though I²C works.

Because a 5×7 font glyph is also 8 pixels tall, one text line maps to exactly one page. That is why both the test tool and the driver can draw text by copying font bytes straight into RAM.

## Parts list

| Qty | Item |
|---|---|
| 1 | Raspberry Pi 5 running Raspberry Pi OS (Debian 13 "trixie", 64-bit) |
| 1 | GM009605 0.96″ I²C OLED module |
| 4 | Female-to-female Dupont jumper wires (red, black, yellow, blue recommended) |

## ⚠ Power the module from 3.3 V, not 5 V

The module's I²C pull-up resistors (typically 4.7 kΩ to 10 kΩ) are connected to **VCC**. If you power it from 5 V, SDA and SCL idle at 5 V. **The Raspberry Pi's GPIOs are 3.3 V only and are not 5 V tolerant**, so this can damage GPIO2/GPIO3 over time. Always use **pin 1 (3V3)**.

Next: [Wiring →](02-wiring.md)
