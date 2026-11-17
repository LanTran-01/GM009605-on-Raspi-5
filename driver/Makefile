# SPDX-License-Identifier: GPL-2.0-only
#
# Out-of-tree build for the GM009605 kernel module and its DT overlay.
#
#   make                 build gm009605.ko and gm009605.dtbo
#   make KDIR=/path      build against a different kernel tree
#   sudo make install    install module + overlay, run depmod
#   sudo make uninstall  remove them again
#   make clean

ifneq ($(KERNELRELEASE),)
# ---- invoked by Kbuild ------------------------------------------------------
obj-m := gm009605.o
ccflags-y += -I$(src)/../common

else
# ---- invoked from the command line -----------------------------------------
KVER     ?= $(shell uname -r)
KDIR     ?= /lib/modules/$(KVER)/build
OVERLAYS ?= /boot/firmware/overlays
PWD      := $(shell pwd)

.PHONY: all module dtbo install uninstall clean

all: module dtbo

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

dtbo: gm009605.dtbo

gm009605.dtbo: gm009605-overlay.dts
	dtc -@ -q -I dts -O dtb -o $@ $<

install: all
	$(MAKE) -C $(KDIR) M=$(PWD) INSTALL_MOD_DIR=extra modules_install
	depmod -a $(KVER)
	install -m 0644 gm009605.dtbo $(OVERLAYS)/gm009605.dtbo

uninstall:
	rm -f /lib/modules/$(KVER)/extra/gm009605.ko*
	depmod -a $(KVER)
	rm -f $(OVERLAYS)/gm009605.dtbo

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f gm009605.dtbo

endif
