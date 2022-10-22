#
# Copyright (C) 2011 secunet AG
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

LIBFLASHROM_PREFIX ?= $(obj)/libflashrom
LIBFLASHROM = $(LIBFLASHROM_PREFIX)/lib/libflashrom.a
INCFLASHROM = $(LIBFLASHROM_PREFIX)/include

ifeq ($(CONFIG_FLASHUPDATE),y)
CPPFLAGS += -I$(INCFLASHROM)
LIBS += $(LIBFLASHROM)
endif

TARGETS-$(CONFIG_FLASHUPDATE) += flashupdate/flashupdate.o
TARGETS-$(CONFIG_FLASHUPDATE) += flashupdate/flashrom-bridge.o
