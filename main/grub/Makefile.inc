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

TARGETS-$(CONFIG_USE_GRUB) += main/grub/grub.o main/grub/builtins.o
TARGETS-$(CONFIG_USE_GRUB) += main/grub/cmdline.o main/grub/char_io.o
TARGETS-$(CONFIG_USE_GRUB) += main/grub/completions.o
TARGETS-$(CONFIG_USE_MD5_PASSWORDS) += main/grub/md5.o
