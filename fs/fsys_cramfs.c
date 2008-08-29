/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002 Russ Dill <address@hidden>
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
/* fsys_minix.c used as a skeleton, cramfs code in kernel used as
 * documentation and some code */

#include "shared.h"
#include "filesys.h"
#include "mini_inflate.h"

#ifdef CONFIG_DEBUG_CRAMFS
# define debug_cramfs(str, args...) printf(str, ## args)
#else
# define debug_cramfs(str, args...) do {;} while(0)
#endif

#if 0
/* include/asm-i386/type.h */
typedef __signed__ char s8;
typedef unsigned char u8;
typedef __signed__ short s16;
typedef unsigned short u16;
typedef __signed__ int s32;
typedef unsigned int u32;
#endif

#define BLOCK_SIZE 	SECTOR_SIZE

#define CRAMFS_MAGIC		0x28cd3d45	/* some random number */
#define CRAMFS_SIGNATURE	"Compressed ROMFS"

/*
 * Reasonably terse representation of the inode data.
 */
struct cramfs_inode {
	u32 mode:16, uid:16;
	/* SIZE for device files is i_rdev */
	u32 size:24, gid:8;
	/* NAMELEN is the length of the file name, divided by 4 and
           rounded up.  (cramfs doesn't support hard links.) */
	/* OFFSET: For symlinks and non-empty regular files, this
	   contains the offset (divided by 4) of the file data in
	   compressed form (starting with an array of block pointers;
	   see README).  For non-empty directories it is the offset
	   (divided by 4) of the inode of the first file in that
	   directory.  For anything else, offset is zero. */
	u32 namelen:6, offset:26;
};

/*
 * Superblock information at the beginning of the FS.
 */
struct cramfs_super {
	u32 magic;		/* 0x28cd3d45 - random number */
	u32 size;		/* Not used.  mkcramfs currently
                                   writes a constant 1<<16 here. */
	u32 flags;		/* 0 */
	u32 future;		/* 0 */
	u8 signature[16];	/* "Compressed ROMFS" */
	u8 fsid[16];		/* random number */
	u8 name[16];		/* user-defined name */
	struct cramfs_inode root;	/* Root inode data */
};

/*
 * Valid values in super.flags.  Currently we refuse to mount
 * if (flags & ~CRAMFS_SUPPORTED_FLAGS).  Maybe that should be
 * changed to test super.future instead.
 */
#define CRAMFS_SUPPORTED_FLAGS (0xff)

/* Uncompression interfaces to the underlying zlib */
int cramfs_uncompress_block(void *dst, int dstlen, void *src, int srclen);
int cramfs_uncompress_init(void);
int cramfs_uncompress_exit(void);

/* linux/stat.h */
#define S_IFMT  00170000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)      (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)      (((m) & S_IFMT) == S_IFDIR)

#define PATH_MAX                1024	/* include/linux/limits.h */
#define MAX_LINK_COUNT             5	/* number of symbolic links to follow */

#define NAMELEN_MAX (((1 << (6 + 1)) - 1) << 2) /* 252 */

#define CRAMFS_BLOCK (4096L)
#define CRAMFS_MAX_BLOCKS ((1 << 24) / CRAMFS_BLOCK)

/* made up, these are pointers into FSYS_BUF */
/* read once, always stays there: */
struct cramfs_buf {
	struct cramfs_super super;
	struct cramfs_inode inode;
	char name[NAMELEN_MAX + 1];
	u32 block_ptrs[CRAMFS_MAX_BLOCKS];
	char data[CRAMFS_BLOCK * 2];
	char temp[CRAMFS_BLOCK];
	/* menu.lst is read 1 byte at a time, try to aleviate *
	 * the performance problem */
	long cached_block;		/* the uncompressed block in cramfs_buf->data */
	long decompressed_block;	/* the decompressed block in cramfs_buf->temp */
	long decompressed_size;		/* the size that is got decompressed to */
};

static struct cramfs_buf *cramfs_buf;

#define CRAMFS_ROOT_INO (sizeof(struct cramfs_super) - sizeof(struct cramfs_inode))

#ifndef STAGE1_5
#define cramfs_memcmp grub_memcmp
#define cramfs_strlen grub_strlen
#else
int
cramfs_memcmp (const char *s1, const char *s2, int n)
{
  while (n)
    {
      if (*s1 < *s2)
	return -1;
      else if (*s1 > *s2)
	return 1;
      s1++;
      s2++;
      n--;
    }

  return 0;
}


int
cramfs_strlen (const char *str)
{
  int len = 0;

  while (*str++)
    len++;

  return len;
}


#endif
/* check filesystem types and read superblock into memory buffer */
int 
cramfs_mount(void)
{	
	debug_cramfs("attempting to mount a cramfs\n");

	cramfs_buf = (struct cramfs_buf *) FSYS_BUF;
	if (part_length < sizeof(struct cramfs_super) / BLOCK_SIZE) {
		debug_cramfs("partition too short\n");
       		return 0;
	}
	  
	if (!devread(0, 0, sizeof(struct cramfs_super), (char *) &cramfs_buf->super)) {
		debug_cramfs("cannot read superblock\n");
		return 0;
	}
  
	if (cramfs_buf->super.magic != CRAMFS_MAGIC) {
		debug_cramfs("magic does not match\n");
		return 0;
	}
	
	if (cramfs_memcmp(CRAMFS_SIGNATURE, cramfs_buf->super.signature, 16)) {
		debug_cramfs("signiture does not match\n");
		return 0;
	}

	if (cramfs_buf->super.flags & ~CRAMFS_SUPPORTED_FLAGS) {
		debug_cramfs("unsupported flags\n");
		return 0;
	}

	if (!S_ISDIR(cramfs_buf->super.root.mode)) {
		debug_cramfs("root is not a directory\n");
		return 0;
	}
	
	debug_cramfs("cramfs mounted\n");
	return 1;
}

/* read from INODE into BUF */
int
cramfs_read (char *buf, int len)
{
	u32 start;
	u32 end;
	int nblocks;
	int block;
	int block_len;
	int ret = 0;
	long size = 0;
	long devread_ret;

	nblocks = (cramfs_buf->inode.size - 1) / CRAMFS_BLOCK + 1;
	block = filepos / CRAMFS_BLOCK;

	if (!devread(0, cramfs_buf->inode.offset << 2, nblocks << 2, (char *) &cramfs_buf->block_ptrs))
		return 0;

	if (block)
		start = cramfs_buf->block_ptrs[block - 1];
	else start = (cramfs_buf->inode.offset + nblocks) << 2;
	
	debug_cramfs("reading a file of %d blocks starting at offset %d (block %d)\n", nblocks, start, block);
	debug_cramfs("filepos is %d\n", filepos);

	while (block < nblocks && len > 0) {
		end = cramfs_buf->block_ptrs[block];
		block_len = end - start;
		
		debug_cramfs("reading to %d bytes at block %d at offset %d, %d bytes...",
			len, block, start, block_len);
		if (cramfs_buf->cached_block != block) {
			disk_read_func = disk_read_hook;
			devread_ret = devread(0, start, block_len, cramfs_buf->data);
			disk_read_func = NULL;
			cramfs_buf->cached_block = block;
		} else debug_cramfs("%d was cached...", block);
		
		if (!ret && (filepos % CRAMFS_BLOCK)) {
			/* its the first read, and its not block aligned */
			debug_cramfs("doing a non-aligned decompression of block %d at offset %d\n", 
					block, filepos % CRAMFS_BLOCK);
			if (cramfs_buf->decompressed_block != block) {
				size = decompress_block(cramfs_buf->temp, cramfs_buf->data + 2, memcpy);
				cramfs_buf->decompressed_size = size;
				cramfs_buf->decompressed_block = block;
			} else size = cramfs_buf->decompressed_size;
			size -= filepos % CRAMFS_BLOCK;
			if (size > len) size = len;
			if (size > 0)
				memcpy(buf, cramfs_buf->temp + (filepos % CRAMFS_BLOCK), size);		
		} else  {
			/* just another full block read */
			size = decompress_block(buf, cramfs_buf->data + 2, memcpy);
		}
		if (size < 0) {
			debug_cramfs("error in decomp (error %d)\n", size);
			cramfs_buf->cached_block = -1;
			cramfs_buf->decompressed_block = -1;
			return 0;
		}
		debug_cramfs("decomp`d %d bytes\n", size);
		buf += size;
		len -= size;
		filepos += size;
		ret += size;

		block++;
		start = end;
	}

	return ret;
}

/* preconditions: cramfs_mount already executed, therefore supblk in buffer
     known as SUPERBLOCK
   returns: 0 if error, nonzero iff we were able to find the file successfully
   postconditions: on a nonzero return, buffer known as INODE contains the
     inode of the file we were trying to look up
   side effects: none yet  */
int
cramfs_dir(char *dirname)
{
	int str_chk;			     /* used ot hold the results of a string
					        compare */

	u32 current_ino;  		     /* inode info for current_ino */
	u32 parent_ino;

	char linkbuf[PATH_MAX];	  	   /* buffer for following sym-links */
	int link_count = 0;
	
	char *rest;
	char ch;
	
	u32 dir_size;			     /* size of this directory */
	u32 off;			     /* offset of this directory */
	u32 loc;			     /* location within a directory */
	
	int namelen;

  /* loop invariants:
     current_ino = inode to lookup
     dirname = pointer to filename component we are cur looking up within
     the directory known pointed to by current_ino (if any) */

#ifdef CONFIG_DEBUG_CRAMFS
	printf("\n");
#endif  

	current_ino = CRAMFS_ROOT_INO;
	parent_ino = current_ino;

	for (;;) {
		debug_cramfs("inode offset %d, dirname %s\n", current_ino, dirname);

		if (!devread(0, current_ino, sizeof(struct cramfs_inode), (char *) &cramfs_buf->inode))
			return 0;

		/* If we've got a symbolic link, then chase it. */
		if (S_ISLNK(cramfs_buf->inode.mode)) {
			int len;

			if (++link_count > MAX_LINK_COUNT) {
				errnum = ERR_SYMLINK_LOOP;
				return 0;
			}
			debug_cramfs("S_ISLNK(%s)\n", dirname);

			/* Find out how long our remaining name is. */
			len = 0;
			while (dirname[len] && !isspace(dirname[len]))
				len++;

			/* Get the symlink size. */
			filemax = cramfs_buf->inode.size;
			if (filemax + len > sizeof(linkbuf) - 2) {
				errnum = ERR_FILELENGTH;
				return 0;
			}

			if (len) {
				/* Copy the remaining name to the end of the symlink data.
				   Note that DIRNAME and LINKBUF may overlap! */
				memmove(linkbuf + filemax, dirname, len);
			}
			linkbuf[filemax + len] = '\0';

			/* Read the necessary blocks, and reset the file pointer. */
			len = grub_read(linkbuf, filemax);
			filepos = 0;
			if (!len)
				return 0;

			debug_cramfs("symlink=%s\n", linkbuf);

			dirname = linkbuf;
			if (*dirname == '/') {
				/* It's an absolute link, so look it up in root. */
				current_ino = CRAMFS_ROOT_INO;
				parent_ino = current_ino;
			} else {
				/* Relative, so look it up in our parent directory. */
				current_ino = parent_ino;
			}

			/* Try again using the new name. */
			continue;
		}

		/* If end of filename, INODE points to the file's inode */
		if (!*dirname || isspace(*dirname)) {
			if (!S_ISREG(cramfs_buf->inode.mode)) {
				errnum = ERR_BAD_FILETYPE;
				return 0;
			}
			filemax = cramfs_buf->inode.size;
			debug_cramfs("file found, size %d\n", filemax);
			cramfs_buf->cached_block = -1;
			cramfs_buf->decompressed_block = -1;
			return 1;
		}

		/* else we have to traverse a directory */
		parent_ino = current_ino;

		/* skip over slashes */
		while (*dirname == '/') dirname++;

		/* if this isn't a directory of sufficient size to hold our file, 
		   abort */
		if (!(cramfs_buf->inode.size) || !S_ISDIR(cramfs_buf->inode.mode)) {
			errnum = ERR_BAD_FILETYPE;
			return 0;
		}

		/* skip to next slash or end of filename (space) */
		for (rest = dirname; (ch = *rest) && !isspace(ch) && ch != '/'; rest++);

		/* look through this directory and find the next filename component */
		/* invariant: rest points to slash after the next filename component */
		*rest = 0;
		loc = 0;
		off = cramfs_buf->inode.offset << 2;
		dir_size = cramfs_buf->inode.size;

		do {
			debug_cramfs("dirname=`%s', rest=`%s', loc=%d\n", dirname, rest, loc);

			/* if our location/byte offset into the directory exceeds the size,
			   give up */
			if (loc >= dir_size) {
				if (print_possibilities < 0) {
#if 0
					putchar ('\n');
#endif
				} else {
					errnum = ERR_FILE_NOT_FOUND;
					*rest = ch;
				}
				return (print_possibilities < 0);
			}
			
			current_ino = off + loc;
			
			/* read in this inode */
			if (!devread(0, current_ino, sizeof(struct cramfs_inode), (char *) &cramfs_buf->inode))
				return 0;
			if (!devread(0, current_ino + sizeof(struct cramfs_inode), 
					cramfs_buf->inode.namelen << 2, cramfs_buf->name))
				return 0;
			cramfs_buf->name[cramfs_buf->inode.namelen << 2] = '\0';
			namelen = cramfs_strlen(cramfs_buf->name);
			
			/* advance loc prematurely to next on-disk directory entry  */
			loc += sizeof(struct cramfs_inode) + (cramfs_buf->inode.namelen << 2);

			debug_cramfs("directory entry index=%d\n", loc + off);
			debug_cramfs("entry=%s\n", cramfs_buf->name);

			str_chk = substring(dirname, cramfs_buf->name);

#ifndef STAGE1_5
			if (print_possibilities && ch != '/'
				&& (!*dirname || str_chk <= 0)) {
				if (print_possibilities > 0)
					print_possibilities = -print_possibilities;
				print_a_completion(cramfs_buf->name);
			}
# endif


		} while (str_chk || (print_possibilities && ch != '/'));

		*(dirname = rest) = ch;
	}
	/* never get here */
}
