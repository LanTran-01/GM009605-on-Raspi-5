# SPDX-License-Identifier: GPL-2.0-only
#
# GM009605-on-Raspi-5 top-level Makefile
#
#   make            build everything (test tool, kernel module, overlay)
#   make test       build the user-space connection test only
#   make driver     build the kernel module + overlay only
#   make check      run the connection test against /dev/i2c-1
#   make clean

.PHONY: all test driver check clean

all: test driver

test:
	$(MAKE) -C test

driver:
	$(MAKE) -C driver

check: test
	./test/gm009605_test

clean:
	$(MAKE) -C test clean
	$(MAKE) -C driver clean
