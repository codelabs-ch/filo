#
# Copyright (C) 2008 by coresystems GmbH 
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

export PROGRAM_NAME := FILO
export PROGRAM_VERSION := 0.6.0rc1

export src := $(shell pwd)
export srctree := $(src)
export srck := $(src)/util/kconfig
export obj := $(src)/build
export objk := $(src)/build/util/kconfig

export KERNELVERSION      := $(PROGRAM_VERSION)
export KCONFIG_AUTOHEADER := $(obj)/config.h
export KCONFIG_AUTOCONFIG := $(obj)/auto.conf

CONFIG_SHELL := sh
KBUILD_DEFCONFIG := configs/defconfig
UNAME_RELEASE := $(shell uname -r)
HAVE_DOTCONFIG := $(wildcard .config)

BUILD_INFO = ($(shell whoami)@$(shell hostname)) $(shell LANG=C date)

# Make is silent per default, but 'make V=1' will show all compiler calls.
ifneq ($(V),1)
Q := @
endif

CC = gcc
HOSTCC = gcc
HOSTCXX = g++
HOSTCFLAGS := -I$(srck) -I$(objk)
HOSTCXXFLAGS := -I$(srck) -I$(objk)

STRIP ?= strip
NM ?= nm

ifeq ($(strip $(HAVE_DOTCONFIG)),)

all: config
include util/kconfig/Makefile

else

include $(src)/.config

ARCHDIR-$(CONFIG_TARGET_I386) := i386

PLATFORM-y += $(ARCHDIR-y)/Makefile.inc
TARGETS-y :=

BUILD-y := main/Makefile.inc main/grub/Makefile.inc fs/Makefile.inc 
BUILD-y += drivers/Makefile.inc
BUILD-y += drivers/usb/Makefile.inc drivers/newusb/Makefile.inc drivers/flash/Makefile.inc

include $(PLATFORM-y) $(BUILD-y)

LIBPAYLOAD_PREFIX ?= libpayload
LIBPAYLOAD = $(LIBPAYLOAD_PREFIX)/lib/libpayload.a
INCPAYLOAD = $(LIBPAYLOAD_PREFIX)/include
LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

OBJS     := $(patsubst %,$(obj)/%,$(TARGETS-y))
INCLUDES := -I$(INCPAYLOAD) -Iinclude -I$(ARCHDIR-y)/include -Ibuild
INCLUDES += -I$(shell $(CC) -print-search-dirs | head -n 1 | cut -d' ' -f2)include

try-run= $(shell set -e; \
TMP=".$$$$.tmp"; \
if ($(1)) > /dev/null 2>&1; \
then echo "$(2)"; \
else echo "$(3)"; \
fi; rm -rf "$$TMP")

cc-option= $(call try-run,\
$(CC) $(1) -S -xc /dev/null -o "$$TMP", $(1), $(2))

STACKPROTECT += $(call cc-option, -fno-stack-protector,)

GCCINCDIR = $(shell $(CC) -print-search-dirs | head -n 1 | cut -d' ' -f2)include
CPPFLAGS = -nostdinc -imacros $(obj)/config.h -Iinclude -I$(GCCINCDIR) -MD
CFLAGS += -Wall $(STACKPROTECT) $(INCLUDES) -Os -fomit-frame-pointer -fno-common -ffreestanding -fno-strict-aliasing

TARGET  = $(obj)/filo.elf

include $(src)/.config

all: prepare $(obj)/version.h $(TARGET)

$(obj)/filo: $(src)/.config $(OBJS)
	$(Q)printf "  LD      $(subst $(shell pwd)/,,$(@))\n"
	$(Q)$(LD) -N -T $(ARCHDIR-y)/ldscript -o $@ $(OBJS) $(LIBPAYLOAD) $(LIBGCC)

$(TARGET): $(obj)/filo $(obj)/util/ebchecksum
	$(Q)cp $(obj)/filo $@
	$(Q)$(NM) $(obj)/filo > $(obj)/filo.map
	$(Q)printf "  STRIP   $(subst $(shell pwd)/,,$(@))\n"
	$(Q)$(STRIP) -s $@
	$(Q)printf "  EBCHECK $(subst $(shell pwd)/,,$(@))\n"
	$(Q)$(obj)/util/ebchecksum -w $@

include util/kconfig/Makefile
include util/Makefile.inc

$(obj)/%.o: $(src)/%.c
	$(Q)printf "  CC      $(subst $(shell pwd)/,,$(@))\n"
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(obj)/%.S.o: $(src)/%.S
	$(Q)printf "  AS      $(subst $(shell pwd)/,,$(@))\n"
	$(Q)$(AS) $(ASFLAGS) -o $@ $<

endif

$(obj)/version.h: FORCE
	$(Q)echo '#define PROGRAM_NAME "$(PROGRAM_NAME)"' > $@
	$(Q)echo '#define PROGRAM_VERSION "$(PROGRAM_VERSION)"' >> $@
	$(Q)echo '#define PROGRAM_VERSION_FULL "$(PROGRAM_VERSION) $(BUILD_INFO)"' >> $@
	$(Q)echo '#define BUILD_INFO "$(BUILD_INFO)"' >> $@

prepare:
	$(Q)mkdir -p $(obj)/util/kconfig/lxdialog
	$(Q)mkdir -p $(obj)/i386 $(obj)/fs $(obj)/drivers/flash $(obj)/drivers/usb $(obj)/drivers/newusb
	$(Q)mkdir -p $(obj)/main/grub

clean:
	$(Q)rm -rf $(obj)/i386 $(obj)/fs $(obj)/drivers $(obj)/main $(obj)/util

distclean: clean
	$(Q)rm -rf build
	$(Q)rm -f .config .config.old ..config.tmp .kconfig.d .tmpconfig*

FORCE:

.PHONY: $(PHONY) prepare clean distclean FORCE

