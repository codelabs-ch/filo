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

try-run = $(shell set -e;		\
	TMP=".$$$$.tmp";		\
	if ($(1)) > /dev/null 2>&1;	\
	then echo "$(2)";		\
	else echo "$(3)";		\
	fi;				\
	rm -rf "$$TMP")

cc-option = $(call try-run,$(CC) $(1) -S -xc /dev/null -o "$$TMP",$(1),$(2))

$(if $(wildcard .xcompile),,$(shell bash util/xcompile/xcompile > .xcompile))
include .xcompile

CROSS_PREFIX ?=
CC ?= $(CROSS_PREFIX)gcc -m32
AS ?= $(CROSS_PREFIX)as --32
LD ?= $(CROSS_PREFIX)ld -belf32-i386
NM ?= $(CROSS_PREFIX)nm
STRIP ?= $(CROSS_PREFIX)strip
HOSTCC ?= gcc
HOSTCXX ?= g++
HOSTCFLAGS := -I$(srck) -I$(objk) -pipe
HOSTCXXFLAGS := -I$(srck) -I$(objk) -pipe

ifeq ($(strip $(HAVE_DOTCONFIG)),)

all: config
include util/kconfig/Makefile

else

include $(src)/.config

LIBPAYLOAD_PREFIX ?= $(obj)/libpayload
LIBPAYLOAD = $(LIBPAYLOAD_PREFIX)/lib/libpayload.a
INCPAYLOAD = $(LIBPAYLOAD_PREFIX)/include
LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
GCCINCDIR = $(shell $(CC) -print-search-dirs | head -n 1 | cut -d' ' -f2)include

ARCHDIR-$(CONFIG_TARGET_I386) := i386

CPPFLAGS := -nostdinc -imacros $(obj)/config.h
CPPFLAGS += -I$(INCPAYLOAD) -I$(INCPAYLOAD)/$(ARCHDIR-y)
CPPFLAGS += -I$(ARCHDIR-y)/include -Iinclude -I$(obj)
CPPFLAGS += -I$(GCCINCDIR)

CFLAGS := -Wall -Wshadow -Os -pipe
CFLAGS += -fomit-frame-pointer -fno-common -ffreestanding -fno-strict-aliasing
CFLAGS += $(call cc-option, -fno-stack-protector,)

LIBS := $(LIBPAYLOAD) $(LIBGCC)

SUBDIRS-y += main/ fs/ drivers/
SUBDIRS-y += $(ARCHDIR-y)/

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
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build distclean
	cp lib.config $(LIBCONFIG_PATH)/.config
	mkdir -p $(LIBCONFIG_PATH)/build
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build oldconfig
	$(MAKE) -C $(LIBCONFIG_PATH) obj=$(obj)/libpayload-build DESTDIR=$(obj) install
endif

$(obj)/filo: $(OBJS) $(LIBPAYLOAD)
	printf "  LD      $(subst $(shell pwd)/,,$(@))\n"
	$(LD) -N -T $(ARCHDIR-y)/ldscript $(OBJS) --start-group $(LIBS) --end-group -o $@

$(TARGET): $(obj)/filo $(obj)/filo.map
	printf "  STRIP   $(subst $(shell pwd)/,,$(@))\n"
	$(STRIP) -s $< -o $@

include util/kconfig/Makefile

$(KCONFIG_AUTOHEADER): $(src)/.config
	$(MAKE) silentoldconfig

$(OBJS): $(KCONFIG_AUTOHEADER) $(obj)/version.h | libpayload
$(obj)/%.o: $(src)/%.c
	printf "  CC      $(subst $(shell pwd)/,,$(@))\n"
	$(CC) -MMD $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(obj)/%.S.o: $(src)/%.S
	printf "  AS      $(subst $(shell pwd)/,,$(@))\n"
	$(AS) $(ASFLAGS) -o $@ $<

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

prepare: $(sort $(dir $(OBJS))) $(obj)/util/kconfig/lxdialog/

clean:
	rm -rf $(sort $(dir $(OBJS))) $(obj)/util
	rm -rf $(obj)/version.h

distclean: clean
	rm -rf $(obj)
	rm -f .config lib.config .config.old .xcompile ..config.tmp .kconfig.d .tmpconfig*

FORCE:

.PHONY: $(PHONY) prepare clean distclean libpayload FORCE
