/*
 * This file is part of FILO.
 *
 * Copyright (C) 2003 SONE Takeshi <ts1@tsn.or.jp>
 * Copyright (C) 2005-2008 coresystems GmbH
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

#include <libpayload.h>
#include <libpayload-config.h>
#include <config.h>
#include <fs.h>

#define DEBUG_THIS CONFIG_DEBUG_BLOCKDEV
#include <debug.h>

#define NUM_CACHE		64

static unsigned char buf_cache[NUM_CACHE][DEV_SECTOR_SIZE];
static unsigned long cache_sect[NUM_CACHE];

static char dev_name[256];

int dev_type = -1;
int dev_drive = -1;
unsigned long part_start;
unsigned long part_length;
int using_devsize;

static inline int has_pc_part_magic(unsigned char *sect)
{
	return sect[510] == 0x55 && sect[511] == 0xAA;
}

static inline int is_pc_extended_part(unsigned char type)
{
	return type == 5 || type == 0xf || type == 0x85;
}

/* IBM-PC/MS-DOS style partitioning scheme */
static int open_pc_partition(int part, unsigned long *start_p,
			     unsigned long *length_p)
{
	/* Layout of PC partition table */
	struct pc_partition {
		unsigned char boot;
		unsigned char head;
		unsigned char sector;
		unsigned char cyl;
		unsigned char type;
		unsigned char e_head;
		unsigned char e_sector;
		unsigned char e_cyl;
		unsigned char start_sect[4];	/* unaligned little endian */
		unsigned char nr_sects[4];	/* ditto */
	} *p;
	unsigned char buf[DEV_SECTOR_SIZE];

	/* PC partition probe */
	if (!devread(0, 0, sizeof(buf), buf)) {
		debug("device read failed.\n");
		return 0;
	}
	if (!has_pc_part_magic(buf)) {
		debug("PC partition table magic number not found.\n");
		//debug_hexdump(buf, DEV_SECTOR_SIZE);
		return PARTITION_UNKNOWN;
	}
	p = (struct pc_partition *) (buf + 0x1be);
	if (part < 4) {
		/* Primary partition */
		p += part;
		if (p->type == 0 || is_pc_extended_part(p->type)) {
			printf("Partition %d does not exist.\n", part + 1);
			return 0;
		}
		*start_p = cpu_to_le32(*(u32 *) (p->start_sect));
		*length_p = cpu_to_le32(*(u32 *) (p->nr_sects));
		return 1;
	} else {
		/* Extended partition */
		int i;
		int cur_part;
		unsigned long ext_start, cur_table;
		/* Search for the extended partition
		 * which contains logical partitions */
		for (i = 0; i < 4; i++) {
			if (is_pc_extended_part(p[i].type))
				break;
		}
		if (i >= 4) {
			printf("No extended partition found.\n");
			return 0;
		}
		debug("Extended partition at %d.\n", i + 1);
		/* Visit each logical partition labels */
		ext_start = cpu_to_le32(*(u32 *) (p[i].start_sect));
		cur_table = ext_start;
		cur_part = 4;
		for (;;) {
			debug("cur_part=%d at %lu\n", cur_part, cur_table);
			if (!devread(cur_table, 0, sizeof(buf), buf))
				return 0;
			if (!has_pc_part_magic(buf)) {
				debug("PC partition table magic number not found.\n");
				break;
			}

			p = (struct pc_partition *) (buf + 0x1be);
			/* First entry is the logical partition */
			if (cur_part == part) {
				if (p->type == 0) {
					printf("Partition %d is empty.\n", part + 1);
					return 0;
				}
				*start_p = cur_table + cpu_to_le32(*(u32 *) (p->start_sect));
				*length_p = cpu_to_le32(*(u32 *) (p->nr_sects));
				return 1;
			}
			/* Second entry is link to next partition */
			if (!is_pc_extended_part(p[1].type)) {
				debug("no link\n");
				break;
			}
			cur_table = ext_start + cpu_to_le32(*(u32 *) (p[1].start_sect));

			cur_part++;
		}
		printf("Logical partition %d does not exist.\n", part + 1);
		return 0;
	}
}

static void flush_cache(void)
{
	int i;
	for (i = 0; i < NUM_CACHE; i++)
		cache_sect[i] = (unsigned long) -1;
}

static int parse_device_name(const char *name, int *type, int *drive,
			     int *part, uint64_t * offset,
			     uint64_t * length)
{
	*offset = *length = 0;

	if (memcmp(name, "hd", 2) == 0) {
		*type = DISK_IDE;
		name += 2;
		if (*name < 'a' || *name > 'z') {
			printf("Invalid drive\n");
			return 0;
		}
		*drive = *name - 'a';
		name++;
	} else if (memcmp(name, "ud", 2) == 0) {
		*type = DISK_USB;
		name += 2;
		if (*name < 'a' || *name > 'z') {
			printf("Invalid drive\n");
			return 0;
		}
		*drive = *name - 'a';
		name++;
	} else if (memcmp(name, "flash", 5) == 0) {
		*type = DISK_FLASH;
		name += 5;
		if (*name < 'a' || *name > 'z') {
			printf("Invalid flash chip\n");
			return 0;
		}
		*drive = *name - 'a';
		name++;
	} else if (memcmp(name, "mem", 3) == 0) {
		*type = DISK_MEM;
		name += 3;
		*drive = 0;
	} else {
		printf("Unknown device type: %s\n", name);
		return 0;
	}

	*part = (int) simple_strtoull(name, (char **) &name, 0);

	if (*name == '@') {
		name++;
		*offset = strtoull_with_suffix(name, (char **) &name, 0);
		if (*name == ',')
			*length =
			    strtoull_with_suffix(name + 1, (char **) &name,
						 0);
		debug("offset=%#Lx length=%#Lx\n", *offset, *length);
	}

	if (*name != '\0') {
		printf("Can't parse device name.\n");
		return 0;
	}

	return 1;
}

int devopen(const char *name, int *reopen)
{
	int type, drive, part;
	uint64_t offset, length;
	uint32_t disk_size = 0;

	/* Don't re-open the device that's already open */
	if (strcmp(name, dev_name) == 0 && dev_type != -1) {
		debug("already open\n");
		*reopen = 1;
		return 1;
	}
	*reopen = 0;

	if (!parse_device_name
	    (name, &type, &drive, &part, &offset, &length)) {
		debug("failed to parse device name: %s\n", name);
		return 0;
	}

	/* If we have another dev open, close it first! */
	if (dev_type != type && dev_type != -1)
		devclose();

	/* Do simple sanity check first */
	if (offset & DEV_SECTOR_MASK) {
		printf("Device offset must be a multiple of %d.\n", DEV_SECTOR_SIZE);
		return 0;
	}
	if (length & DEV_SECTOR_MASK) {
		printf("WARNING: length is rounded up to multiple of %d.\n", DEV_SECTOR_SIZE);
		length = (length + DEV_SECTOR_MASK) & ~DEV_SECTOR_MASK;
	}

	switch (type) {
#if defined(CONFIG_IDE_DISK) || defined(CONFIG_IDE_NEW_DISK)
	case DISK_IDE:
		if (ide_probe(drive) != 0) {
			debug("Failed to open IDE.\n");
			return 0;
		}
		disk_size = (uint32_t) - 1;	/* FIXME */
		break;
#endif
#ifdef CONFIG_USB_DISK
	case DISK_USB:
		if (usb_probe(drive) != 0) {
			debug("Failed to open USB.\n");
			return 0;
		}
		disk_size = (uint32_t) - 1;	/* FIXME */
		break;
#endif

#ifdef CONFIG_FLASH_DISK
	case DISK_FLASH:
		if (flash_probe(drive) != 0) {
			debug("Failed to open FLASH.\n");
			return 0;
		}
		disk_size = (uint32_t) - 1;	/* FIXME */
		break;
#endif

	case DISK_MEM:
		disk_size = 1 << (32 - DEV_SECTOR_BITS);	/* 4GB/512-byte */
		break;

	default:
		printf("Unknown device type %d.\n", type);
		return 0;
	}

	if (dev_type != type || dev_drive != drive)
		flush_cache();

	/* start with whole disk */
	dev_type = type;
	dev_drive = drive;
	part_start = 0;
	part_length = disk_size;
	using_devsize = 1;

	if (part != 0) {
		/* partition is specified */
		int ret;
		ret =
		    open_pc_partition(part - 1, &part_start, &part_length);
		if (ret == PARTITION_UNKNOWN) {
			ret =
			    open_eltorito_image(part - 1, &part_start,
						&part_length);
			if (ret == PARTITION_UNKNOWN) {
				printf("Unrecognized partitioning scheme.\n");
				return 0;
			}
		}
		if (ret == 0) {
			debug("Can't open partition %d.\n", part);
			return 0;
		}

		debug("Partition %d start %lu length %lu\n", part,
		      part_start, part_length);
	}

	if (offset) {
		if (offset >= (uint64_t) part_length << DEV_SECTOR_BITS) {
			printf("Device offset is too high.\n");
			return 0;
		}
		part_start += offset >> DEV_SECTOR_BITS;
		part_length -= offset >> DEV_SECTOR_BITS;
		debug("after offset: start %lu, length %lu\n", part_start,
		      part_length);
	}

	if (length) {
		if (length > (uint64_t) part_length << DEV_SECTOR_BITS) {
			printf ("Specified length exceeds the size of device.\n");
			return 0;
		}
		part_length = length >> DEV_SECTOR_BITS;
		debug("after length: length %lu\n", part_length);
		using_devsize = 0;
	}

	strncpy(dev_name, name, sizeof(dev_name) - 1);

	return 1;
}

void devclose(void)
{
#ifdef CONFIG_FLASH_DISK
	/* Try to close NAND if it was left open */
	if (dev_type == DISK_FLASH)
		NAND_close();
#endif

	dev_type = -1;
}

/* Read a sector from opened device with simple/stupid buffer cache */
static void *read_sector(unsigned long sector)
{
	unsigned int hash;
	void *buf;

	/* If reading memory, just return the memory as the buffer */
	if (dev_type == DISK_MEM) {
		unsigned long phys = sector << DEV_SECTOR_BITS;
		//debug("mem: %#lx\n", phys);
		return phys_to_virt(phys);
	}

	/* Search in the cache */
	hash = sector % NUM_CACHE;
	buf = buf_cache[hash];
	if (cache_sect[hash] != sector) {
		cache_sect[hash] = (unsigned long) -1;
		switch (dev_type) {
#if defined(CONFIG_IDE_DISK) 
		case DISK_IDE:
			if (ide_read(dev_drive, sector, buf) != 0)
				goto readerr;
			break;
#endif
#if defined(CONFIG_IDE_NEW_DISK)
		case DISK_IDE:
		{
			int count = (NUM_CACHE-hash>8)?8:(NUM_CACHE-hash);
			int ret;
			ret = ide_read_blocks(dev_drive, sector, count, buf);
			if (ret == 2)
				goto nomedium;
			if (ret != 0)
				goto readerr;
			while (--count>0) {
				cache_sect[hash+count] = sector + count;
			}
			break;
		}
#endif
#ifdef CONFIG_USB_DISK
		case DISK_USB:
		{
			int count = (NUM_CACHE-hash>8)?8:(NUM_CACHE-hash);
			if (usb_read(dev_drive, sector, count, buf) != 0)
				goto readerr;
			while (--count>0) {
				cache_sect[hash+count] = sector + count;
			}
			break;
		}
#endif

#ifdef CONFIG_FLASH_DISK
		case DISK_FLASH:
			if (flash_read(dev_drive, sector, buf) != 0)
				return 0;
			break;
#endif

		default:
			printf("read_sector: device not open\n");
			return 0;
		}
		cache_sect[hash] = sector;
	}
	return buf;

      readerr:
	printf("Disk read error dev=%d drive=%d sector=%lu\n",
	       dev_type, dev_drive, sector);
	flush_cache();
	dev_name[0] = '\0';	/* force re-open the device next time */
	return 0;
      nomedium:
	printf("No disk in drive.\n");
	flush_cache();
	dev_name[0] = '\0';	/* force re-open the device next time */
	return 0;
}

int devread(unsigned long sector, unsigned long byte_offset,
	    unsigned long byte_len, void *buf)
{
	char *sector_buffer;
	char *dest = buf;
	unsigned long len;

	/* while sectors are technically 512b in filo, iso9660 requires
	   some special handling. This wastes up to three sectors worth
	   of RAM on a 512b filesystem, but enables iso9660 to work. */
	sector += (byte_offset >> 11) * 4;
	byte_offset &= 0x7ff;

	if (sector + ((byte_len + 0x1ff) >> 9) > part_length) {
		printf("Attempt to read beyond device/partition.\n");
		debug("sector=%lu part_length=%lu byte_len=%lu\n",
		      sector, part_length, byte_len);
		return 0;
	}

	while (byte_len > 0) {
		sector_buffer = read_sector(part_start + sector);
		if (!sector_buffer) {
			debug("Couldn't read sector.\n");
			return 0;
		}
		/* now, we always only request one sector at a time,
		   but rely on the cache system to iron out the
		   issues with 2048b sectors. Yet, we have to skip
		   the first fake sectors in processing. */
		if (byte_offset >= 512) {
			byte_offset -= 512;
			sector++;
			continue;
		}
		len = 512 - byte_offset;
		if (len > byte_len)
			len = byte_len;
		memcpy(dest, sector_buffer + byte_offset, len);
		sector++;
		byte_offset = 0;
		byte_len -= len;
		dest += len;
	}
	return 1;
}
