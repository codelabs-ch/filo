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

TARGETS-y += fs/blockdev.o fs/vfs.o
TARGETS-$(CONFIG_ELTORITO) += fs/eltorito.o
TARGETS-$(CONFIG_FSYS_EXT2FS) += fs/fsys_ext2fs.o
TARGETS-$(CONFIG_FSYS_FAT) += fs/fsys_fat.o
TARGETS-$(CONFIG_FSYS_JFS) += fs/fsys_jfs.o
TARGETS-$(CONFIG_FSYS_MINIX) += fs/fsys_minix.o
TARGETS-$(CONFIG_FSYS_REISERFS) += fs/fsys_reiserfs.o
TARGETS-$(CONFIG_FSYS_XFS) += fs/fsys_xfs.o
TARGETS-$(CONFIG_FSYS_ISO9660) += fs/fsys_iso9660.o
TARGETS-$(CONFIG_FSYS_CRAMFS) += fs/fsys_cramfs.o
TARGETS-$(CONFIG_FSYS_CRAMFS) += fs/mini_inflate.o
TARGETS-$(CONFIG_FSYS_SQUASHFS) += fs/fsys_squashfs.o
TARGETS-$(CONFIG_FSYS_SQUASHFS) += fs/squashfs_zlib.o
TARGETS-$(CONFIG_ARTEC_BOOT) += fs/fsys_aboot.o
TARGETS-$(CONFIG_FSYS_CBFS) += fs/fsys_cbfs.o
