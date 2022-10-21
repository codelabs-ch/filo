/*******************************************************************************
 *
 *	FILO Artecboot loader, enables multiboot through custom header
 *
 *	Copyright 2006 Andrei Birjukov <andrei.birjukov@artecdesign.ee> and
 *	Artec Design LLC http://www.artecdesign.ee
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ******************************************************************************/

#include <libpayload.h>
#include <config.h>
#include <fs.h>
#include <loader.h>
#include "artecboot.h"
#include "../fs/filesys.h"

#define DEBUG_THIS CONFIG_DEBUG_ARTECBOOT
#include <debug.h>

static ARTECBOOT_HEADER bootHdr;

int artecboot_load(const char *file, const char *cmdline)
{
	int i;

	printf("Starting the Artecboot loader...\n");
	// clear the boot header
	memset(&bootHdr, 0, sizeof(bootHdr));

	// try opening the boot parameter file
	if (!file_open(file))
	{
		printf("Boot error: failed to open image file: %s\n", file);
		return LOADER_NOT_SUPPORT;
	}

	file_seek(0);	// seek to the beginning of the parameter file

	// now read out the boot header
	if(file_read(&bootHdr, sizeof(ARTECBOOT_HEADER)) != sizeof(ARTECBOOT_HEADER))
	{
		printf("Boot error: failed reading the boot image header\n");
		file_close();
		return LOADER_NOT_SUPPORT;
	}

	// check whether the parameter data is valid at all
	if(bootHdr.magicHeader != ARTECBOOT_HEADER_MAGIC)
	{
		debug("No Artecboot signature found, aborting\n");
		file_close();
		return LOADER_NOT_SUPPORT;
	}

	// check the version number
	if(bootHdr.bootVersion > CURRENT_VERSION)
	{
		printf("Boot error: incompatible version number: %x\n", bootHdr.bootVersion);
		file_close();
		return LOADER_NOT_SUPPORT;
	}

	// shall we replace the command line?
	if(bootHdr.bitFlags & FLAG_CMDLINE)
	{
		// check the command line and wipe out all junk
		for(i=0; bootHdr.cmdLine[i] != 0; i++)
			switch(bootHdr.cmdLine[i])
			{
			case '\n':
			case '\r':
				bootHdr.cmdLine[i] = ' ';
				break;
			default:
				// do nothing
				break;
			}
	}
	else if(cmdline)
		strncpy(bootHdr.cmdLine, cmdline, sizeof(bootHdr.cmdLine));

	// proceed basing on the specified OS type
	switch(bootHdr.osType)
	{
	case OS_LINUX:
		if(bootHdr.bitFlags & FLAG_INITRD)
		{
			char initrdParam[100];

			if(bootHdr.bitFlags & FLAG_FILESYSTEM)
			{
				// we are using a real filesystem, so format the initrd file as usually
				sprintf(initrdParam, " initrd=%s", bootHdr.initrdFile);
			}
			else
			{
				// we are using a 'fake' filesystem, so use the image offset
				sprintf(initrdParam, " initrd=%s@0x%x,0x%x",
						dev_name, bootHdr.initrdStart, bootHdr.initrdSize);
			}

			debug("adding initrd parameter: %s\n", initrdParam);
			strncat(bootHdr.cmdLine, initrdParam, sizeof(bootHdr.cmdLine));
		}

		printf("Starting Linux loader...\n");

		// if using a real filesystem, load the kernel image from a specified file
		if(bootHdr.bitFlags & FLAG_FILESYSTEM)
			linux_load(bootHdr.kernelFile, bootHdr.cmdLine);
		// if using a 'fake' filesystem, consider reading from the same image
		else
		{
			part_start = bootHdr.kernelStart >> DEV_SECTOR_BITS;
			part_length = ((bootHdr.kernelSize-1) >> DEV_SECTOR_BITS) + 1;
			filemax = bootHdr.kernelSize;
			using_devsize = 0;
			linux_load(file, bootHdr.cmdLine);
		}

		break;

	case OS_WINCE:

		printf("Starting Windows CE loader...\n");
		// if using a real filesystem, load the kernel image from a specified file
		if(bootHdr.bitFlags & FLAG_FILESYSTEM)
			wince_load(bootHdr.kernelFile, bootHdr.cmdLine);
		// if using a 'fake' filesystem, consider reading from the same image
		else
		{
			part_start = bootHdr.kernelStart >> DEV_SECTOR_BITS;
			part_length = ((bootHdr.kernelSize-1) >> DEV_SECTOR_BITS) + 1;
			filemax = bootHdr.kernelSize;
			wince_load(file, bootHdr.cmdLine);
		}

		break;

	default:
		printf("Boot error: unknown OS type, aborting: %d\n", bootHdr.osType);
		return LOADER_NOT_SUPPORT;
	}

	file_close();
	return 0;
}
