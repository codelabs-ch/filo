/*******************************************************************************
 *
 *	FILO Artecboot Virtual File System
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

#include "artecboot.h"
#include "filesys.h"

#define DEBUG_THIS CONFIG_DEBUG_ARTECBOOT
#include <debug.h>

static ARTECBOOT_HEADER bootHdr;
static uint32_t fileStart = 0;

// device read helper, calls the block device read function
// returns number of bytes parsed fthe stream 

int aboot_devread(char* pData, int nSize)
{
	char sectorBuf[DEV_SECTOR_SIZE];
	uint32_t len, total=0;
	int failCount = 128;

	uint32_t sector = (fileStart + filepos) >> DEV_SECTOR_BITS;
	uint32_t byteOffset = (fileStart + filepos) & DEV_SECTOR_MASK;
	
	debug("file start %x, sector %x, offset %d\n", fileStart, sector, byteOffset);

	if (sector + ((nSize + DEV_SECTOR_MASK) >> DEV_SECTOR_BITS) > part_length)
	{
		printf("Error: read outside of device device/partition\n");
		debug("sector=%lu, partition size=%lu, read length=%lu\n",
			(unsigned long)sector, 	(unsigned long)part_length, (unsigned long)nSize);
		return 0;
    }

    while (nSize > 0)
	{
		if (!devread(sector, 0, DEV_SECTOR_SIZE, sectorBuf))
		{
			debug("sector 0x%x read failed\n", sector);
			// do not abort immediately, try some more
			if((failCount --) == 0) return 0;
			
			sector ++;	// try the next sector
			total += DEV_SECTOR_SIZE;
			continue;
		}

		len = SECTOR_SIZE - byteOffset;
		if (len > nSize)
			len = nSize;
		memcpy(pData, sectorBuf + byteOffset, len);
		
		sector ++;
		byteOffset = 0;
		
		nSize -= len;
		pData += len;
		total += len;
    }

	// return number of bytes read from the stream
    return total;
}

int aboot_mount(void)
{
	debug("Mounting Artecboot VFS...\n");
	// clear the boot header
	memset(&bootHdr, 0, sizeof(bootHdr));
	
	fileStart = 0;
	filepos = 0;
	
	// now read out the boot header
	if(aboot_devread((char*)&bootHdr, sizeof(ARTECBOOT_HEADER)) < sizeof(ARTECBOOT_HEADER))
	{
		debug("Boot error: failed reading the boot image header\n");
		return 0;
	}

	// check whether the flash data is valid at all
	if(bootHdr.magicHeader != ARTECBOOT_HEADER_MAGIC)
	{
		debug("No Artecboot signature found, aborting\n");
		return 0;
	}

	// check the version number
	if(bootHdr.bootVersion > CURRENT_VERSION)
	{
		debug("Boot error: incompatible version number: %x\n", bootHdr.bootVersion);
		return 0;
	}

	// align the partition length to the sector size
	part_length = ((bootHdr.imageSize - 1) >> DEV_SECTOR_BITS) + 1;
	return 1;
}

int aboot_read(char *buf, int len)
{
	int read;
	// sanity check
	if(bootHdr.magicHeader != ARTECBOOT_HEADER_MAGIC) return 0;
	debug("reading %d bytes to %x...\n", len, (unsigned int)buf);

	read = aboot_devread(buf, len);
	filepos += read;	// advance current position
	
	debug("read %d bytes, pos %x\n", read, filepos);
	
	// returned length may be greater than requested size because of skipped bad blocks
	if(read >= len) return len;
	return 0;
}

int aboot_dir(char *dirname)
{
	int nRet = 0;
	// sanity check
	if(bootHdr.magicHeader != ARTECBOOT_HEADER_MAGIC) return 0;
	
	// we can only recognize certain hardcoded filenames
	if(!strcmp(dirname, ABOOT_FILE_HEADER))
	{
		filepos = 0;
		fileStart = 0;
		filemax = sizeof(ARTECBOOT_HEADER);
		nRet = 1;
	}
	else if(!strcmp(dirname, ABOOT_FILE_KERNEL))
	{
		filepos = 0;
		fileStart = bootHdr.kernelStart;
		filemax = bootHdr.kernelSize;
		nRet = 1;
	}
	else if(!strcmp(dirname, ABOOT_FILE_INITRD))
	{
		filepos = 0;
		fileStart = bootHdr.initrdStart;
		filemax = bootHdr.initrdSize;
		nRet = 1;
	}
	else
	{
		// unknown file
		filepos = 0;
		filemax = 0;
		nRet = 0;
	}
	
	debug("open file: %s, size %d, dev start %x\n", dirname, filemax, fileStart);
	return nRet;
}

