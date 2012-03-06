#
# Copyright (C) 2008-2009 by coresystems GmbH 
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
export PROGRAM_VERSION := 0.6.0

export src := $(shell pwd)
export srctree := $(src)
export srck := $(src)/util/kconfig
export obj := $(src)/build
export objk := $(src)/build/util/kconfig

ifndef LIBCONFIG_PATH
	LIBCONFIG_PATH := $(src)/../libpayload
endif
export LIBCONFIG_PATH

export KERNELVERSION      := $(PROGRAM_VERSION)
export KCONFIG_AUTOHEADER := $(obj)/config.h
export KCONFIG_AUTOCONFIG := $(obj)/auto.conf

CONFIG_SHELL := sh
KBUILD_DEFCONFIG := configs/defconfig
UNAME_RELEASE := $(shell uname -r)
HAVE_DOTCONFIG := $(wildcard .config)

BUILD_INFO = ($(shell whoami)@$(shell hostname)) $(shell LANG=C date)

# Make is silent per default, but 'make V=1' will show all compiler calls.
Q=@
ifneq ($(V),1)
ifneq ($(Q),)
.SILENT:
endif
endif

$(if $(wildcard .xcompile),,$(shell bash util/xcompile/xcompile > .xcompile))
include .xcompile

CROSS_PREFIX =
CC ?= $(CROSS_PREFIX)gcc -m32
AS ?= $(CROSS_PREFIX)as --32
LD ?= $(CROSS_PREFIX)ld -belf32-i386
STRIP ?= $(CROSS_PREFIX)strip
NM ?= $(CROSS_PREFIX)nm
HOSTCC = gcc
HOSTCXX = g++
HOSTCFLAGS := -I$(srck) -I$(objk) -pipe
HOSTCXXFLAGS := -I$(srck) -I$(objk) -pipe

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
BUILD-y += drivers/flash/Makefile.inc

include $(PLATFORM-y) $(BUILD-y)

LIBPAYLOAD_PREFIX ?= $(obj)/libpayload
LIBPAYLOAD = $(LIBPAYLOAD_PREFIX)/lib/libpayload.a
INCPAYLOAD = $(LIBPAYLOAD_PREFIX)/include
LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

OBJS     := $(patsubst %,$(obj)/%,$(TARGETS-y))
INCLUDES := -I$(INCPAYLOAD) -I$(INCPAYLOAD)/$(ARCHDIR-y) -Iinclude -I$(ARCHDIR-y)/include -Ibuild
INCLUDES += -I$(GCCINCDIR)

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
CFLAGS += $(STACKPROTECT) $(INCLUDES) -Wall -Os -fomit-frame-pointer -fno-common -ffreestanding -fno-strict-aliasing -Wshadow -pipe

TARGET  = $(obj)/filo.elf

HAVE_LIBCONFIG := $(wildcard $(LIBCONFIG_PATH))

all: prepare $(obj)/version.h $(TARGET)


HAVE_LIBPAYLOAD := $(wildcard $(LIBPAYLOAD))
ifneq ($(strip $(HAVE_LIBPAYLOAD)),)
libpayload:
	@printf "Found Libpayload $(LIBPAYLOAD).\n"
else
libpayload: $(src)/$(LIB_CONFIG)
	printf "building libpayload.\n"
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build distclean
	cp lib.config $(LIBCONFIG_PATH)/.config
	mkdir -p $(LIBCONFIG_PATH)/build
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build oldconfig
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build DESTDIR=$(src)/build install
endif

$(obj)/filo: $(OBJS) libpayload
	printf "  LD      $(subst $(shell pwd)/,,$(@))\n"
	$(LD) -N -T $(ARCHDIR-y)/ldscript -o $@ $(OBJS) $(LIBPAYLOAD) $(LIBGCC)

$(TARGET): $(obj)/filo libpayload
	cp $(obj)/filo $@
	$(NM) $(obj)/filo | sort > $(obj)/filo.map
	printf "  STRIP   $(subst $(shell pwd)/,,$(@))\n"
	$(STRIP) -s $@

include util/kconfig/Makefile

$(KCONFIG_AUTOHEADER): $(src)/.config
	$(MAKE) silentoldconfig

$(OBJS): $(KCONFIG_AUTOHEADER) libpayload
$(obj)/%.o: $(src)/%.c
	printf "  CC      $(subst $(shell pwd)/,,$(@))\n"
	$(CC) -MMD $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(obj)/%.S.o: $(src)/%.S
	printf "  AS      $(subst $(shell pwd)/,,$(@))\n"
	$(AS) $(ASFLAGS) -o $@ $<

endif

$(obj)/version.h: FORCE
	echo '#define PROGRAM_NAME "$(PROGRAM_NAME)"' > $@
	echo '#define PROGRAM_VERSION "$(PROGRAM_VERSION)"' >> $@
	echo '#define PROGRAM_VERSION_FULL "$(PROGRAM_VERSION) $(BUILD_INFO)"' >> $@
	echo '#define BUILD_INFO "$(BUILD_INFO)"' >> $@

prepare:
	mkdir -p $(obj)/util/kconfig/lxdialog
	mkdir -p $(obj)/i386 $(obj)/fs $(obj)/drivers/flash
	mkdir -p $(obj)/main/grub

clean:
	rm -rf $(obj)/i386 $(obj)/fs $(obj)/drivers $(obj)/main $(obj)/util

distclean: clean
	rm -rf build
	rm -f .config lib.config .config.old .xcompile ..config.tmp .kconfig.d .tmpconfig*

FORCE:

.PHONY: $(PHONY) prepare clean distclean FORCE

