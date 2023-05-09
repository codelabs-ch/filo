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
# Foundation, Inc.
#

export PROGRAM_NAME := FILO
export PROGRAM_VERSION := 0.6.0

export src := $(shell pwd)
export srctree := $(src)
export srck := $(src)/util/kconfig
export obj := $(src)/build
export objk := $(src)/build/util/kconfig

ifndef LIBCONFIG_PATH
LIBCONFIG_PATH := $(src)/../coreboot/payloads/libpayload
endif
export LIBCONFIG_PATH

ifeq ($(wildcard $(LIBCONFIG_PATH)/*),)
$(error Could not find libpayload at $(LIBCONFIG_PATH))
endif

export KERNELVERSION      := $(PROGRAM_VERSION)
export KCONFIG_AUTOHEADER := $(obj)/config.h
export KCONFIG_AUTOCONFIG := $(obj)/auto.conf
export KCONFIG_CONFIG     := .config
export KCONFIG_RUSTCCFG   := $(obj)/rustc_cfg

CONFIG_SHELL := sh
CONFIG_COMPILER_GCC=y
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

HOSTCC ?= gcc
HOSTCXX ?= g++
HOSTCFLAGS := -I$(srck) -I$(objk) -pipe
HOSTCXXFLAGS := -I$(srck) -I$(objk) -pipe

ifeq ($(strip $(HAVE_DOTCONFIG)),)

all: defconfig
include util/kconfig/Makefile.inc

else

include $(src)/.config

$(if $(wildcard .xcompile),,$(shell bash util/xcompile/xcompile > .xcompile))
include .xcompile

ARCH-$(CONFIG_TARGET_I386) := x86_32
ARCH-$(CONFIG_TARGET_ARM) := arm

CC := $(CC_$(ARCH-y))
AS := $(AS_$(ARCH-y))
LD := $(LD_$(ARCH-y))
NM := $(NM_$(ARCH-y))
OBJCOPY := $(OBJCOPY_$(ARCH-y))
OBJDUMP := $(OBJDUMP_$(ARCH-y))
READELF := $(READELF_$(ARCH-y))
STRIP := $(STRIP_$(ARCH-y))
AR := $(AR_$(ARCH-y))

CFLAGS += $(CFLAGS_$(ARCH-y))

LIBPAYLOAD_PREFIX ?= $(obj)/libpayload
LIBPAYLOAD = $(LIBPAYLOAD_PREFIX)/lib/libpayload.a
INCPAYLOAD = $(LIBPAYLOAD_PREFIX)/include
LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
GCCINCDIR = $(shell $(CC) -print-search-dirs | head -n 1 | cut -d' ' -f2)include
LPGCC = $(LIBPAYLOAD_PREFIX)/bin/lpgcc
LPAS = $(LIBPAYLOAD_PREFIX)/bin/lpas

ARCHDIR-$(CONFIG_TARGET_I386) := x86
ARCHDIR-$(CONFIG_TARGET_ARM) := arm

CPPFLAGS := -imacros $(obj)/config.h
CPPFLAGS += -I$(INCPAYLOAD) -I$(INCPAYLOAD)/$(ARCHDIR-y)
CPPFLAGS += -I$(ARCHDIR-y)/include -Iinclude -I$(obj)
CPPFLAGS += -I$(GCCINCDIR) -include $(INCPAYLOAD)/kconfig.h

CFLAGS := -Wall -Wshadow -Os -pipe
CFLAGS += -fomit-frame-pointer -fno-common -ffreestanding -fno-strict-aliasing

LIBS := $(LIBPAYLOAD) $(LIBGCC)

SUBDIRS-$(CONFIG_USE_GRUB) += flashupdate
SUBDIRS-y += main fs drivers $(ARCHDIR-y)

$(foreach subdir,$(SUBDIRS-y),$(eval include $(subdir)/Makefile.inc))

TARGET := $(obj)/filo.elf
OBJS := $(patsubst %,$(obj)/%,$(TARGETS-y))


all: prepare $(TARGET)

HAVE_LIBPAYLOAD := $(wildcard $(LIBPAYLOAD))
ifneq ($(strip $(HAVE_LIBPAYLOAD)),)
libpayload:
	@printf "Found libpayload as $(LIBPAYLOAD)\n"
else
libpayload: $(LIBPAYLOAD)
$(LIBPAYLOAD): $(src)/$(LIB_CONFIG)
	@printf "Building libpayload...\n"
	CROSS_COMPILE_$(ARCH-y)=$(CROSS_COMPILE_$(ARCH-y)) $(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build distclean
	cp lib.config $(LIBCONFIG_PATH)/.config
	mkdir -p $(LIBCONFIG_PATH)/build
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build oldconfig
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build DESTDIR=$(obj) install
endif

$(obj)/filo: $(OBJS) $(LIBPAYLOAD)
	printf "  LD      $(subst $(shell pwd)/,,$(@))\n"
	CC="$(CC)" $(LPGCC) $(OBJS) $(LIBS) -o $@

$(obj)/filo.bzImage: $(TARGET) $(obj)/x86/linux_head.o
	$(OBJCOPY) -O binary $(obj)/x86/linux_head.o $@.tmp1
	$(OBJCOPY) -O binary $< $@.tmp2
	cat $@.tmp1 $@.tmp2 > $@.tmp
	mv $@.tmp $@

$(TARGET): $(obj)/filo $(obj)/filo.map
	printf "  STRIP   $(subst $(shell pwd)/,,$(@))\n"
	$(STRIP) -s $< -o $@

include util/kconfig/Makefile.inc

$(KCONFIG_AUTOHEADER): $(src)/.config
	$(MAKE) syncconfig

$(OBJS): $(KCONFIG_AUTOHEADER) $(obj)/version.h | libpayload
$(obj)/%.o: $(src)/%.c
	printf "  CC      $(subst $(shell pwd)/,,$(@))\n"
	CC="$(CC)" $(LPGCC) -MMD $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(obj)/%.S.o: $(src)/%.S
	printf "  AS      $(subst $(shell pwd)/,,$(@))\n"
	AS="$(AS)" $(LPAS) $(ASFLAGS) -o $@ $<

$(obj)/%.map: $(obj)/%
	printf "  SYMS    $(subst $(shell pwd)/,,$(@))\n"
	$(NM) -n $< > $@

endif

$(obj)/version.h: Makefile
	printf "  GEN     $(subst $(shell pwd)/,,$(@))\n"
	echo '#define PROGRAM_NAME "$(PROGRAM_NAME)"' > $@
	echo '#define PROGRAM_VERSION "$(PROGRAM_VERSION)"' >> $@
	echo '#define PROGRAM_VERSION_FULL "$(PROGRAM_VERSION) $(BUILD_INFO)"' >> $@
	echo '#define BUILD_INFO "$(BUILD_INFO)"' >> $@

$(obj)/%/:
	mkdir -p $@

ifndef NOMKDIR
$(shell mkdir -p $(KCONFIG_SPLITCONFIG) $(objk)/lxdialog)
endif

prepare: $(sort $(dir $(OBJS))) $(obj)/util/kconfig/lxdialog/

clean:
	rm -rf $(sort $(dir $(OBJS))) $(obj)/util
	rm -rf $(obj)/version.h

distclean: clean
	rm -rf $(obj)
	rm -f .config lib.config .config.old .xcompile ..config.tmp .kconfig.d .tmpconfig*

FORCE:

.PHONY: $(PHONY) prepare clean distclean libpayload FORCE
