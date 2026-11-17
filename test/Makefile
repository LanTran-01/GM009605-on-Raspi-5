# SPDX-License-Identifier: GPL-2.0-only
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../common

gm009605_test: gm009605_test.c ../common/font5x7.h ../common/ssd1306.h
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f gm009605_test
