/*
 * This file is part of FILO.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

// Artecboot header, gives information to loader

#define ARTECBOOT_HEADER_MAGIC		0x10ADFACE
#define CURRENT_VERSION				0x0102

#define OS_UNKNOWN					0x00
#define OS_LINUX					0x01
#define OS_WINCE					0x02

#define FLAG_INITRD					0x0001		// if set, the loader will provide initrd to kernel
#define FLAG_FILESYSTEM				0x0002		// if set, the loader will use specified file names
#define FLAG_CMDLINE				0x0004		// if set, the loader will pass the new command line

typedef struct __attribute__ ((packed))
{
	unsigned long	magicHeader;
	unsigned short	bootVersion;
	unsigned short	headerSize;		// also kernel image start
	unsigned long	imageSize;		// NB! since 1.02 is the total image/partition size
	unsigned long	bitFlags;
	unsigned short	osType;
	char			cmdLine[256];
	unsigned long	kernelStart;	// used with Artecboot VFS / NULLFS
	unsigned long	kernelSize;		// used with Artecboot VFS / NULLFS
	unsigned long	initrdStart;	// used with Artecboot VFS / NULLFS
	unsigned long	initrdSize;		// used with Artecboot VFS / NULLFS
	char			kernelFile[100];	// valid only with FLAG_FILESYSTEM
	char			initrdFile[100];	// valid only with FLAG_FILESYSTEM

} ARTECBOOT_HEADER;

#define ABOOT_FILE_KERNEL	"/kernel"
#define ABOOT_FILE_INITRD	"/initrd"
#define ABOOT_FILE_HEADER	"/header"
