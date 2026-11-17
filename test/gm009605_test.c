// SPDX-License-Identifier: GPL-2.0-only
/*
 * gm009605_test.c - Driver-less connection test for the GM009605 OLED.
 *
 * Talks to the SSD1306 controller directly through the generic
 * /dev/i2c-N character device (i2c-dev). Use this FIRST to prove the
 * wiring is correct before building or loading the kernel driver.
 *
 * Test sequence:
 *   1. probe   - single-byte write; fails fast if nothing ACKs
 *   2. all-on  - every pixel lit (verifies panel + charge pump)
 *   3. pattern - checkerboard, then border
 *   4. text    - "GM009605 OK" banner
 *
 * Usage: gm009605_test [-b bus] [-a addr] [-q]
 *
 * Copyright (C) 2026 Lan Tran
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "font5x7.h"
#include "ssd1306.h"

#define STEP_DELAY_MS	1200

static int fd = -1;
static uint8_t fb[SSD1306_FB_SIZE];
static int quiet;

static void msleep(unsigned int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void step(const char *name)
{
	if (!quiet) {
		printf("  [....] %s", name);
		fflush(stdout);
	}
}

static void ok(void)
{
	if (!quiet)
		printf("\r  [ OK ]\n");
}

static int write_bytes(const uint8_t *buf, size_t len)
{
	ssize_t n = write(fd, buf, len);

	if (n != (ssize_t)len) {
		fprintf(stderr, "\n  i2c write failed: %s\n",
			n < 0 ? strerror(errno) : "short write");
		return -1;
	}
	return 0;
}

static int cmds(const uint8_t *c, size_t len)
{
	uint8_t buf[32];

	if (len + 1 > sizeof(buf))
		return -1;
	buf[0] = SSD1306_CTRL_CMD;
	memcpy(buf + 1, c, len);
	return write_bytes(buf, len + 1);
}

#define CMDS(...) ({ const uint8_t _c[] = { __VA_ARGS__ }; cmds(_c, sizeof(_c)); })

static int display_init(void)
{
	return CMDS(SSD1306_DISPLAY_OFF,
		    SSD1306_CLOCK_DIV, 0x80,
		    SSD1306_MUX_RATIO, SSD1306_HEIGHT - 1,
		    SSD1306_DISPLAY_OFFSET, 0x00,
		    SSD1306_START_LINE(0),
		    SSD1306_CHARGE_PUMP, SSD1306_CHARGE_PUMP_ON,
		    SSD1306_MEM_MODE, SSD1306_MEM_MODE_HORIZONTAL,
		    SSD1306_SEG_REMAP_ON,
		    SSD1306_COM_SCAN_DEC,
		    SSD1306_COM_PINS, SSD1306_COM_PINS_ALT,
		    SSD1306_SET_CONTRAST, SSD1306_DEFAULT_CONTRAST,
		    SSD1306_PRECHARGE, 0xF1,
		    SSD1306_VCOMH, 0x40,
		    SSD1306_DISPLAY_RAM,
		    SSD1306_NORMAL,
		    SSD1306_SCROLL_OFF,
		    SSD1306_DISPLAY_ON);
}

/* Push the whole 1 KiB framebuffer, 128 bytes (one page) per transfer. */
static int flush(void)
{
	uint8_t buf[1 + SSD1306_WIDTH];
	int page;

	if (CMDS(SSD1306_COL_RANGE, 0, SSD1306_WIDTH - 1,
		 SSD1306_PAGE_RANGE, 0, SSD1306_PAGES - 1))
		return -1;

	buf[0] = SSD1306_CTRL_DATA;
	for (page = 0; page < SSD1306_PAGES; page++) {
		memcpy(buf + 1, fb + page * SSD1306_WIDTH, SSD1306_WIDTH);
		if (write_bytes(buf, sizeof(buf)))
			return -1;
	}
	return 0;
}

static void set_pixel(int x, int y)
{
	if (x >= 0 && x < SSD1306_WIDTH && y >= 0 && y < SSD1306_HEIGHT)
		fb[(y / 8) * SSD1306_WIDTH + x] |= 1u << (y % 8);
}

/* Text is drawn on 8-pixel "pages", so row 0..7 maps to one text line each. */
static void draw_text(int col, int row, const char *s)
{
	int x = col;

	for (; *s && x + FONT5X7_WIDTH <= SSD1306_WIDTH; s++) {
		unsigned char ch = (unsigned char)*s;

		if (ch < FONT5X7_FIRST || ch > FONT5X7_LAST)
			ch = '?';
		memcpy(&fb[row * SSD1306_WIDTH + x],
		       font5x7[ch - FONT5X7_FIRST], FONT5X7_WIDTH);
		x += FONT5X7_ADVANCE;
	}
}

static void draw_text_centered(int row, const char *s)
{
	int w = (int)strlen(s) * FONT5X7_ADVANCE - 1;

	draw_text((SSD1306_WIDTH - w) / 2, row, s);
}

static void draw_border(void)
{
	int i;

	for (i = 0; i < SSD1306_WIDTH; i++)
		set_pixel(i, 0), set_pixel(i, SSD1306_HEIGHT - 1);
	for (i = 0; i < SSD1306_HEIGHT; i++)
		set_pixel(0, i), set_pixel(SSD1306_WIDTH - 1, i);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-b bus] [-a addr] [-q]\n"
		"  -b bus   I2C bus number (default 1 -> /dev/i2c-1)\n"
		"  -a addr  7-bit I2C address (default 0x3c)\n"
		"  -q       quiet, only report errors\n", prog);
}

int main(int argc, char **argv)
{
	int bus = 1, addr = SSD1306_I2C_ADDR, opt, i;
	char dev[32], line[32];

	while ((opt = getopt(argc, argv, "b:a:qh")) != -1) {
		switch (opt) {
		case 'b': bus = (int)strtol(optarg, NULL, 0); break;
		case 'a': addr = (int)strtol(optarg, NULL, 0); break;
		case 'q': quiet = 1; break;
		default: usage(argv[0]); return opt == 'h' ? 0 : 2;
		}
	}

	snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);
	if (!quiet)
		printf("GM009605 connection test  (%s, address 0x%02x)\n\n",
		       dev, addr);

	step("Open I2C bus");
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "\n  cannot open %s: %s\n"
			"  -> is I2C enabled? (docs/03-enable-i2c.md)\n",
			dev, strerror(errno));
		return 1;
	}
	if (ioctl(fd, I2C_SLAVE, addr) < 0) {
		fprintf(stderr, "\n  I2C_SLAVE 0x%02x: %s\n"
			"  -> is the kernel driver bound to this address? "
			"Unload it first.\n", addr, strerror(errno));
		return 1;
	}
	ok();

	step("Probe device (ACK check)");
	if (CMDS(SSD1306_DISPLAY_OFF)) {
		fprintf(stderr, "  -> no ACK from 0x%02x: check wiring "
			"(docs/02-wiring.md) and run 'i2cdetect -y %d'\n",
			addr, bus);
		return 1;
	}
	ok();

	step("Initialise SSD1306");
	if (display_init())
		return 1;
	ok();

	step("All pixels on");
	if (CMDS(SSD1306_DISPLAY_ALL_ON))
		return 1;
	msleep(STEP_DELAY_MS);
	if (CMDS(SSD1306_DISPLAY_RAM))
		return 1;
	ok();

	step("Checkerboard pattern");
	for (i = 0; i < SSD1306_FB_SIZE; i++)
		fb[i] = (i / 4) % 2 ? 0x0F : 0xF0;
	if (flush())
		return 1;
	msleep(STEP_DELAY_MS);
	ok();

	step("Invert / normal");
	if (CMDS(SSD1306_INVERT))
		return 1;
	msleep(STEP_DELAY_MS / 2);
	if (CMDS(SSD1306_NORMAL))
		return 1;
	ok();

	step("Text banner");
	memset(fb, 0, sizeof(fb));
	draw_border();
	draw_text_centered(1, "GM009605 OK");
	draw_text_centered(3, "Raspberry Pi 5");
	snprintf(line, sizeof(line), "bus %d  addr 0x%02X", bus, addr);
	draw_text_centered(5, line);
	draw_text_centered(6, "128 x 64 SSD1306");
	if (flush())
		return 1;
	ok();

	close(fd);
	if (!quiet)
		printf("\nAll steps passed. The banner stays on screen.\n");
	return 0;
}
