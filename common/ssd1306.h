/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ssd1306.h - SSD1306 controller constants used by the GM009605 module.
 *
 * Shared by the kernel driver (driver/) and the user-space test (test/).
 * See the SSD1306 datasheet, section 9 "Command Table".
 */
#ifndef GM009605_SSD1306_H
#define GM009605_SSD1306_H

/* Panel geometry of the GM009605 (0.96", 128x64, monochrome) */
#define SSD1306_WIDTH		128
#define SSD1306_HEIGHT		64
#define SSD1306_PAGES		(SSD1306_HEIGHT / 8)
#define SSD1306_FB_SIZE		(SSD1306_WIDTH * SSD1306_PAGES)	/* 1024 bytes */

/* Default 7-bit I2C address (0x3D if the address jumper is moved) */
#define SSD1306_I2C_ADDR	0x3C

/*
 * The first byte of every I2C write is a "control byte":
 *   0x00 -> the following bytes are commands
 *   0x40 -> the following bytes are display RAM data
 */
#define SSD1306_CTRL_CMD	0x00
#define SSD1306_CTRL_DATA	0x40

/* Fundamental commands */
#define SSD1306_SET_CONTRAST		0x81	/* + 1 byte, 0x00..0xFF */
#define SSD1306_DISPLAY_RAM		0xA4	/* show RAM contents */
#define SSD1306_DISPLAY_ALL_ON		0xA5	/* every pixel on (test) */
#define SSD1306_NORMAL			0xA6
#define SSD1306_INVERT			0xA7
#define SSD1306_DISPLAY_OFF		0xAE	/* sleep */
#define SSD1306_DISPLAY_ON		0xAF

/* Scrolling */
#define SSD1306_SCROLL_OFF		0x2E

/* Addressing */
#define SSD1306_MEM_MODE		0x20	/* + 1 byte */
#define SSD1306_MEM_MODE_HORIZONTAL	0x00
#define SSD1306_COL_RANGE		0x21	/* + start, end */
#define SSD1306_PAGE_RANGE		0x22	/* + start, end */

/* Hardware configuration */
#define SSD1306_START_LINE(n)		(0x40 | ((n) & 0x3F))
#define SSD1306_SEG_REMAP_OFF		0xA0	/* column 0 -> SEG0 */
#define SSD1306_SEG_REMAP_ON		0xA1	/* column 127 -> SEG0 */
#define SSD1306_MUX_RATIO		0xA8	/* + 1 byte (height - 1) */
#define SSD1306_COM_SCAN_INC		0xC0
#define SSD1306_COM_SCAN_DEC		0xC8
#define SSD1306_DISPLAY_OFFSET		0xD3	/* + 1 byte */
#define SSD1306_COM_PINS		0xDA	/* + 1 byte */
#define SSD1306_COM_PINS_ALT		0x12	/* required for 128x64 */

/* Timing & driving scheme */
#define SSD1306_CLOCK_DIV		0xD5	/* + 1 byte */
#define SSD1306_PRECHARGE		0xD9	/* + 1 byte */
#define SSD1306_VCOMH			0xDB	/* + 1 byte */

/* Charge pump (the GM009605 has no external VCC, so this must be on) */
#define SSD1306_CHARGE_PUMP		0x8D	/* + 1 byte */
#define SSD1306_CHARGE_PUMP_ON		0x14
#define SSD1306_CHARGE_PUMP_OFF		0x10

#define SSD1306_DEFAULT_CONTRAST	0xCF

#endif /* GM009605_SSD1306_H */
