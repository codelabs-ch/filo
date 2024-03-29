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

include main/grub/Makefile.inc

TARGETS-y += main/filo.o main/strtox.o
TARGETS-y += main/loader.o main/timer.o
TARGETS-$(CONFIG_ELF_BOOT) += main/elfload.o
TARGETS-$(CONFIG_SUPPORT_SOUND) += main/sound.o
TARGETS-$(CONFIG_MULTIBOOT_IMAGE) += main/mb_hdr.o
