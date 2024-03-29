/* GRUB compatibility header */

/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2003   Free Software Foundation, Inc.
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

#include <libpayload.h>
#include <config.h>
#include <fs.h>

static inline int
substring (const char *s1, const char *s2)
{
  while (*s1 == *s2)
    {
      /* The strings match exactly. */
      if (! *(s1++))
	return 0;
      s2 ++;
    }

  /* S1 is a substring of S2. */
  if (*s1 == 0)
    return -1;

  /* S1 isn't a substring. */
  return 1;
}

#define MAXINT 0x7fffffff

/* This is only used by fsys_* to determine if it's hard disk. If it is,
 * they try to guess filesystem type by partition type. I guess it is
 * not necessory, so hardcoded to 0 (first floppy) --ts1 */
#define current_drive 0

/* Ditto */
#define current_slice 0

extern char dev_name[256];
extern unsigned long part_start;
extern unsigned long part_length;
extern u64 filepos;
extern u64 filemax;
extern int fsmax;

/* Error codes (descriptions are in common.c) */
typedef enum
{
  ERR_NONE = 0,
  ERR_BAD_FILENAME,
  ERR_BAD_FILETYPE,
  ERR_BAD_GZIP_DATA,
  ERR_BAD_GZIP_HEADER,
  ERR_BAD_PART_TABLE,
  ERR_BAD_VERSION,
  ERR_BELOW_1MB,
  ERR_BOOT_COMMAND,
  ERR_BOOT_FAILURE,
  ERR_BOOT_FEATURES,
  ERR_DEV_FORMAT,
  ERR_DEV_VALUES,
  ERR_EXEC_FORMAT,
  ERR_FILELENGTH,
  ERR_FILE_NOT_FOUND,
  ERR_FSYS_CORRUPT,
  ERR_FSYS_MOUNT,
  ERR_GEOM,
  ERR_NEED_LX_KERNEL,
  ERR_NEED_MB_KERNEL,
  ERR_NO_DISK,
  ERR_NO_PART,
  ERR_NUMBER_PARSING,
  ERR_OUTSIDE_PART,
  ERR_READ,
  ERR_SYMLINK_LOOP,
  ERR_UNRECOGNIZED,
  ERR_WONT_FIT,
  ERR_WRITE,
  ERR_BAD_ARGUMENT,
  ERR_UNALIGNED,
  ERR_PRIVILEGED,
  ERR_DEV_NEED_INIT,
  ERR_NO_DISK_SPACE,
  ERR_NUMBER_OVERFLOW,

  MAX_ERR_NUM
} grub_error_t;

extern grub_error_t errnum;

/* instrumentation variables */
/* (Not used in FILO) */
extern void (*disk_read_hook) (int, int, int);
extern void (*disk_read_func) (int, int, int);

#define FSYS_BUFLEN 0x8000
extern char FSYS_BUF[FSYS_BUFLEN];

extern int print_possibilities;
void print_a_completion (char *filename);

#define SECTOR_SIZE 512
#define SECTOR_BITS 9

#ifdef CONFIG_FSYS_FAT
int fat_mount (void);
int fat_read (char *buf, int len);
int fat_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_CBFS
int cbfs_mount (void);
int cbfs_read (char *buf, int len);
int cbfs_dir (const char *dirname);
#endif

#ifdef CONFIG_FSYS_EXT2FS
int ext2fs_mount (void);
int ext2fs_read (char *buf, int len);
int ext2fs_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_MINIX
int minix_mount (void);
int minix_read (char *buf, int len);
int minix_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_REISERFS
int reiserfs_mount (void);
int reiserfs_read (char *buf, int len);
int reiserfs_dir (char *dirname);
int reiserfs_embed (int *start_sector, int needed_sectors);
#endif

#ifdef CONFIG_FSYS_JFS
int jfs_mount (void);
int jfs_read (char *buf, int len);
int jfs_dir (char *dirname);
int jfs_embed (int *start_sector, int needed_sectors);
#endif

#ifdef CONFIG_FSYS_XFS
int xfs_mount (void);
int xfs_read (char *buf, int len);
int xfs_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_ISO9660
int iso9660_mount (void);
int iso9660_read (char *buf, int len);
int iso9660_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_CRAMFS
int cramfs_mount (void);
int cramfs_read (char *buf, int len);
int cramfs_dir (char *dirname);
#endif

#ifdef CONFIG_FSYS_SQUASHFS
int squashfs_mount (void);
int squashfs_read (char *buf, int len);
int squashfs_dir (char *dirname);
#endif

#ifdef CONFIG_ARTEC_BOOT
int aboot_mount (void);
int aboot_read (char *buf, int len);
int aboot_dir (char *dirname);
#endif

/* This is not a flag actually, but used as if it were a flag.  */
#define PC_SLICE_TYPE_HIDDEN_FLAG	0x10

#define PC_SLICE_TYPE_NONE		0
#define PC_SLICE_TYPE_FAT12		1
#define PC_SLICE_TYPE_FAT16_LT32M	4
#define PC_SLICE_TYPE_EXTENDED	5
#define PC_SLICE_TYPE_FAT16_GT32M	6
#define PC_SLICE_TYPE_FAT32		0xb
#define PC_SLICE_TYPE_FAT32_LBA		0xc
#define PC_SLICE_TYPE_FAT16_LBA		0xe
#define PC_SLICE_TYPE_WIN95_EXTENDED	0xf
#define PC_SLICE_TYPE_EZD		0x55
#define PC_SLICE_TYPE_MINIX		0x80
#define PC_SLICE_TYPE_LINUX_MINIX	0x81
#define PC_SLICE_TYPE_EXT2FS	0x83
#define PC_SLICE_TYPE_LINUX_EXTENDED	0x85
#define PC_SLICE_TYPE_VSTAFS		0x9e
#define PC_SLICE_TYPE_DELL_UTIL		0xde
#define PC_SLICE_TYPE_LINUX_RAID	0xfd

/* For convenience.  */
/* Check if TYPE is a FAT partition type. Clear the hidden flag before
   the check, to allow the user to mount a hidden partition in GRUB.  */
#define IS_PC_SLICE_TYPE_FAT(type)	\
  ({ int _type = (type) & ~PC_SLICE_TYPE_HIDDEN_FLAG; \
     _type == PC_SLICE_TYPE_FAT12 \
     || _type == PC_SLICE_TYPE_FAT16_LT32M \
     || _type == PC_SLICE_TYPE_FAT16_GT32M \
     || _type == PC_SLICE_TYPE_FAT16_LBA \
     || _type == PC_SLICE_TYPE_FAT32 \
     || _type == PC_SLICE_TYPE_FAT32_LBA \
     || _type == PC_SLICE_TYPE_DELL_UTIL; })

#define IS_PC_SLICE_TYPE_MINIX(type) \
  (((type) == PC_SLICE_TYPE_MINIX)	\
   || ((type) == PC_SLICE_TYPE_LINUX_MINIX))

#define IS_PC_SLICE_TYPE_BSD_WITH_FS(type,fs) 0

/* possible values for the *BSD-style partition type */
#define	FS_UNUSED	0	/* unused */
#define	FS_SWAP		1	/* swap */
#define	FS_V6		2	/* Sixth Edition */
#define	FS_V7		3	/* Seventh Edition */
#define	FS_SYSV		4	/* System V */
#define	FS_V71K		5	/* V7 with 1K blocks (4.1, 2.9) */
#define	FS_V8		6	/* Eighth Edition, 4K blocks */
#define	FS_BSDFFS	7	/* 4.2BSD fast file system */
#define	FS_MSDOS	8	/* MSDOS file system */
#define	FS_BSDLFS	9	/* 4.4BSD log-structured file system */
#define	FS_OTHER	10	/* in use, but unknown/unsupported */
#define	FS_HPFS		11	/* OS/2 high-performance file system */
#define	FS_ISO9660	12	/* ISO 9660, normally CD-ROM */
#define	FS_BOOT		13	/* partition contains bootstrap */
#define	FS_ADOS		14	/* AmigaDOS fast file system */
#define	FS_HFS		15	/* Macintosh HFS */
#define	FS_FILECORE	16	/* Acorn Filecore Filing System */
#define	FS_EXT2FS	17	/* Linux Extended 2 file system */

#ifdef CONFIG_DEBUG_FSYS_EXT2FS
#define E2DEBUG
#endif
