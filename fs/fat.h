/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2001  Free Software Foundation, Inc.
 *  Copyright (C) 2008  coresystems GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc.
 */

/*
 *  Defines for the FAT BIOS Parameter Block (embedded in the first block
 *  of the partition.
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;
typedef __signed__ short __s16;
typedef unsigned short __u16;
typedef __signed__ int __s32;
typedef unsigned int __u32;

struct fat16_extended_bpb {
	__u8    drive_number;	/* Physical Drive Number */
	__u8    reserved;	/* "current head" */
	__u8	boot_signature;	/* Extended Boot Signature 0x28 or 0x29 */
	__u8    id[4];		/* ID (serial number) */
	__u8    volumelabel[11];/* volume label */
	__s8    type[8];	/* Padded with blanks: "FAT12", "FAT16" */
} __attribute__((packed));

struct fat32_extended_bpb {
	/* The following fields are only used by FAT32 */
	__u32	fat32_length;	/* sectors/FAT */
	__u16	flags;		/* bit 8: fat mirroring, low 4: active fat */
	__u8	version[2];	/* major, minor filesystem version */
	__u32	root_cluster;	/* first cluster in root directory */
	__u16	info_sector;	/* filesystem info sector */
	__u16	backup_boot;	/* backup boot sector */
	__u8	reserved2[12];	/* Reserved */
	/* from here on it looks like the fat12/fat16 code */
	__u8    drive_number;	/* Physical Drive Number */
	__u8    reserved;	/* "current head" */
	__u8	boot_signature;	/* Extended Boot Signature 0x28 or 0x29 */
	__u8    id[4];		/* ID (serial number) */
	__u8    volumelabel[11];/* volume label */
	__s8    type[8];	/* Padded with blanks: "FAT32" */
} __attribute__((packed));

/* Note that some shorts are not aligned, and must therefore
 * be declared as array of two bytes.
 */
struct fat_bpb {
	__s8	ignored[3];	/* Boot strap short or near jump */
	__s8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	bytes_per_sect[2];	/* bytes per logical sector */
	__u8	sects_per_clust;/* sectors/cluster */
	__u8	reserved_sects[2];	/* reserved sectors */
	__u8	num_fats;	/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	short_sectors[2];	/* number of sectors */
	__u8	media;		/* media code (unused) */
	__u16	fat_length;	/* sectors/FAT */
	__u16	secs_track;	/* sectors per track */
	__u16	heads;		/* number of heads */
	__u32	hidden;		/* hidden sectors (unused) */
	__u32	long_sectors;	/* number of sectors (if short_sectors == 0) */

	union {
		struct fat16_extended_bpb fat16;
		struct fat32_extended_bpb fat32;
	} extended;
};

#define FAT_CVT_U16(bytarr) (* (__u16*)(bytarr))

/*
 *  Defines how to differentiate a 12-bit and 16-bit FAT.
 */

#define FAT_MAX_12BIT_CLUST       4087	/* 4085 + 2 */

/*
 *  Defines for the file "attribute" byte
 */

#define FAT_ATTRIB_OK_MASK        0x37
#define FAT_ATTRIB_NOT_OK_MASK    0xC8
#define FAT_ATTRIB_DIR            0x10
#define FAT_ATTRIB_LONGNAME       0x0F

/*
 *  Defines for FAT directory entries
 */

#define FAT_DIRENTRY_LENGTH       32

#define FAT_DIRENTRY_ATTRIB(entry) \
  (*((unsigned char *) (entry+11)))
#define FAT_DIRENTRY_VALID(entry) \
  ( ((*((unsigned char *) entry)) != 0) \
    && ((*((unsigned char *) entry)) != 0xE5) \
    && !(FAT_DIRENTRY_ATTRIB(entry) & FAT_ATTRIB_NOT_OK_MASK) )
#define FAT_DIRENTRY_FIRST_CLUSTER(entry) \
  ((*((unsigned short *) (entry+26)))+(*((unsigned short *) (entry+20)) << 16))
#define FAT_DIRENTRY_FILELENGTH(entry) \
  (*((unsigned long *) (entry+28)))

#define FAT_LONGDIR_ID(entry) \
  (*((unsigned char *) (entry)))
#define FAT_LONGDIR_ALIASCHECKSUM(entry) \
  (*((unsigned char *) (entry+13)))
