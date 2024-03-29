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
# Foundation, Inc.
#

include drivers/flash/Makefile.inc

TARGETS-$(CONFIG_IDE_DISK) += drivers/ide.o
TARGETS-$(CONFIG_IDE_NEW_DISK) += drivers/ide_new.o
TARGETS-$(CONFIG_VIA_SOUND) += drivers/via-sound.o
TARGETS-$(CONFIG_USB_DISK) += drivers/usb.o
TARGETS-$(CONFIG_TARGET_I386) += drivers/intel.o
TARGETS-$(CONFIG_TARGET_I386) += drivers/amd.o drivers/sb600.o
TARGETS-$(CONFIG_FLASHROM_LOCKDOWN) += drivers/flash_lockdown.o
