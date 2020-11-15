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

-include $(src)/.config

ARCHDIR-$(CONFIG_TARGET_I386) := x86
ARCHDIR-$(CONFIG_TARGET_ARM) := arm

SUBDIRS-y =
SUBDIRS-y += main fs drivers $(ARCHDIR-y)
SUBDIRS-$(CONFIG_USE_GRUB) += flashupdate

$(foreach subdir,$(SUBDIRS-y),$(eval include $(subdir)/Makefile.inc))

TARGET := $(obj)/filo.elf
OBJS := $(patsubst %,$(obj)/%,$(TARGETS-y))

LIBPAYLOAD_OBJ := $(obj)/libpayload
LIBPAYLOAD_PREFIX ?= $(LIBPAYLOAD_OBJ)
LIBPAYLOAD_DEFCONFIG ?=

all: real-all

# $(LIBPAYLOAD_PREFIX) decides if we build against an installed libpayload.
ifneq ($(wildcard $(LIBPAYLOAD_PREFIX)/lib/libpayload.a),)
include Makefile.standalone

# Otherwise, try to find libpayload sources.
else ifneq ($(wildcard $(LIBCONFIG_PATH)/Makefile.payload),)
include $(LIBCONFIG_PATH)/Makefile.payload
else ifneq ($(wildcard ../../../libpayload/Makefile.payload),)
include ../../../libpayload/Makefile.payload
else ifneq ($(wildcard ../coreboot/payloads/libpayload/Makefile.payload),)
include ../coreboot/payloads/libpayload/Makefile.payload

else
$(error Could not find libpayload.)
endif

ifeq ($(filter %clean,$(MAKECMDGOALS)),)

export KERNELVERSION      := $(PROGRAM_VERSION)
export KCONFIG_AUTOHEADER := $(obj)/config.h
export KCONFIG_AUTOCONFIG := $(obj)/auto.conf
export KCONFIG_CONFIG     := .config
export KCONFIG_RUSTCCFG   := $(obj)/rustc_cfg

CONFIG_SHELL := sh
KBUILD_DEFCONFIG := configs/defconfig
UNAME_RELEASE := $(shell uname -r)
HAVE_DOTCONFIG := $(wildcard .config)

BUILD_INFO = ($(shell whoami)@$(shell hostname)) $(shell LANG=C date)

HOSTCC ?= gcc
HOSTCXX ?= g++
HOSTCFLAGS := -I$(srck) -I$(objk) -pipe
HOSTCXXFLAGS := -I$(srck) -I$(objk) -pipe

ifeq ($(strip $(HAVE_DOTCONFIG)),)

real-all: defconfig
include util/kconfig/Makefile.inc

else

ARCH-$(CONFIG_TARGET_I386) := x86_32
ARCH-$(CONFIG_TARGET_ARM) := arm
ARCH := $(ARCH-y)

OBJCOPY := $(OBJCOPY_$(ARCH-y))

LIBGCC = $(shell $(CC_$(ARCH-y)) $(CFLAGS) -print-libgcc-file-name)

CFLAGS += -imacros $(obj)/config.h
CFLAGS += -I$(ARCHDIR-y)/include -Iinclude -I$(obj)
CFLAGS += -Wshadow -pipe -fomit-frame-pointer -fno-common -fno-strict-aliasing

# FIXME: We override Makefile.payload's -Werror, because
# FILO doesn't build without warnings yet.
CFLAGS += -Wno-error

LIBS := $(LIBPAYLOAD) $(LIBGCC)

real-all: prepare $(TARGET)

$(obj)/filo.bzImage: $(TARGET) $(obj)/x86/linux_head.o
	$(OBJCOPY) -O binary $(obj)/x86/linux_head.o $@.tmp1
	$(OBJCOPY) -O binary $< $@.tmp2
	cat $@.tmp1 $@.tmp2 > $@.tmp
	mv $@.tmp $@

include util/kconfig/Makefile.inc

$(KCONFIG_AUTOHEADER): $(src)/.config
	$(MAKE) syncconfig

$(OBJS): $(KCONFIG_AUTOHEADER) $(obj)/version.h

endif

$(obj)/version.h: Makefile
	printf "    GEN        $(subst $(shell pwd)/,,$(@))\n"
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

FORCE:

.PHONY: $(PHONY) prepare libpayload FORCE

else # %clean,$(MAKECMDGOALS)

clean:
	rm -rf $(sort $(dir $(OBJS))) $(obj)/util
	rm -rf $(obj)/version.h

distclean: clean
	rm -rf $(obj)
	rm -f .config lib.config .config.old .xcompile ..config.tmp .kconfig.d .tmpconfig*

.PHONY: clean distclean

endif
