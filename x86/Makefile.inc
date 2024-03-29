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

TARGETS-$(CONFIG_TARGET_I386) += x86/context.o x86/switch.S.o x86/segment.o
TARGETS-$(CONFIG_TARGET_I386) += x86/sys_info.o
TARGETS-$(CONFIG_LINUX_LOADER) += x86/linux_load.o
TARGETS-$(CONFIG_WINCE_LOADER) += x86/wince_load.o
TARGETS-$(CONFIG_ARTEC_BOOT) += x86/artecboot.o
TARGETS-$(CONFIG_CSL_BOOT) += x86/csl.o x86/pae.o
