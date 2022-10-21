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

/* Interface between GRUB's fs drivers and application code */
#include <libpayload.h>
#include <config.h>
#include <fs.h>
#include "filesys.h"
#include <dirent.h>

#define DEBUG_THIS CONFIG_DEBUG_VFS
#include <debug.h>

int filepos;
int filemax;
grub_error_t errnum;
void (*disk_read_hook) (int, int, int);
void (*disk_read_func) (int, int, int);
char FSYS_BUF[FSYS_BUFLEN];
int fsmax;
int print_possibilities = 0;

struct fsys_entry {
	char *name;
	int (*mount_func) (void);
	int (*read_func) (char *buf, int len);
	int (*dir_func) (char *dirname);
	void (*close_func) (void);
	int (*embed_func) (int *start_sector, int needed_sectors);
};

struct fsys_entry fsys_table[] = {
# ifdef CONFIG_FSYS_CBFS
	{"CBFS ROM Image", cbfs_mount, cbfs_read, cbfs_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_FAT
	{"FAT filesystem", fat_mount, fat_read, fat_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_EXT2FS
	{"EXT2 filesystem", ext2fs_mount, ext2fs_read, ext2fs_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_MINIX
	{"MINIX filesystem", minix_mount, minix_read, minix_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_REISERFS
	{"REISERFS filesystem", reiserfs_mount, reiserfs_read, reiserfs_dir, 0, reiserfs_embed},
# endif
# ifdef CONFIG_FSYS_JFS
	{"JFS filesystem", jfs_mount, jfs_read, jfs_dir, 0, jfs_embed},
# endif
# ifdef CONFIG_FSYS_XFS
	{"XFS filesystem", xfs_mount, xfs_read, xfs_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_ISO9660
	{"ISO9660 filesystem", iso9660_mount, iso9660_read, iso9660_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_CRAMFS
	{"CRAM filesystem", cramfs_mount, cramfs_read, cramfs_dir, 0, 0},
# endif
# ifdef CONFIG_FSYS_SQUASHFS
	{"SQUASH filesystem", squashfs_mount, squashfs_read, squashfs_dir, 0, 0},
# endif
# ifdef CONFIG_ARTEC_BOOT
	{"Artecboot Virtual Filesystem", aboot_mount, aboot_read, aboot_dir, 0, 0},
# endif
};

/* NULLFS is used to read images from raw device */
static int nullfs_dir(char *name)
{
	uint64_t dev_size;

	if (name) {
		debug("Can't have a named file.\n");
		return 0;
	}

	dev_size = (uint64_t) part_length << 9;

	/* GRUB code doesn't like 2GB or bigger files */
	if (dev_size > 0x7fffffff) {
		dev_size = 0x7fffffff;
	}

	filemax = dev_size;

	return 1;
}

static int nullfs_read(char *buf, int len)
{
	if (devread(filepos >> 9, filepos & 0x1ff, len, buf)) {
		filepos += len;
		return len;
	} else {
		return 0;
	}
}

static struct fsys_entry nullfs = { "nullfs", 0, nullfs_read, nullfs_dir, 0, 0 };

static struct fsys_entry *fsys;

int mount_fs(void)
{
	int i;

	for (i = 0; i < sizeof(fsys_table) / sizeof(fsys_table[0]); i++) {
		if (!fsys_table[i].mount_func())
			continue;

		fsys = &fsys_table[i];
		debug("Mounted %s\n", fsys->name);
		return 1;
	}
	fsys = 0;

	printf("Unknown filesystem type.\n");
	return 0;
}

int file_open(const char *filename)
{
	char *dev = 0;
	const char *path;
	int len;
	int retval = 0;
	int reopen;

	path = strchr(filename, ':');
	if (path) {
		len = path - filename;
		path++;
		dev = malloc(len + 1);
		memcpy(dev, filename, len);
		dev[len] = '\0';
	} else {
		/* No colon is given. Is this device or filename? */
		if (filename[0] == '/') {
			/* Anything starts with '/' must be a filename */
			dev = 0;
			path = filename;
		} else {
			dev = strdup(filename);
			path = 0;
		}
	}
	debug("dev=%s, path=%s\n", dev, path);

	if (dev && dev[0]) {
		if (!devopen(dev, &reopen)) {
			fsys = 0;
			goto out;
		}
		if (!reopen)
			fsys = 0;
	}

	if (path) {
		if (!fsys || fsys == &nullfs) {
			if (!mount_fs())
				goto out;
		}
		using_devsize = 0;
		if (!path[0]) {
			printf("No filename is given.\n");
			goto out;
		}
	} else {
		fsys = &nullfs;
	}

	filepos = 0;
	errnum = 0;
	if (!fsys->dir_func((char *) path)) {
		printf("File not found '%s'.\n", filename);
		goto out;
	}
	retval = 1;

out:
	if (dev)
		free(dev);

	return retval;
}

int file_read(void *buf, unsigned long len)
{
	if (filepos < 0 || filepos > filemax)
		filepos = filemax;

	if (len < 0 || len > filemax - filepos)
		len = filemax - filepos;

	errnum = 0;

	debug("reading %lu bytes, offset 0x%x\n", len, filepos);
	return fsys->read_func(buf, len);
}

unsigned long file_seek(unsigned long offset)
{
	debug("seeking to 0x%lx\n", offset);
	filepos = offset;
	return filepos;
}

unsigned long file_size(void)
{
	return filemax;
}

void file_set_size(unsigned long size)
{
	debug("updating file size to %lu bytes\n", size);

	filemax = size;
	using_devsize = 0;
}

void file_close(void)
{
	devclose();
}

int dir(const char *dirname)
{
	char *dev = 0;
	const char *path;
	int len;
	int retval = 0;
	int reopen;

	path = strchr(dirname, ':');
	if (path) {
		len = path - dirname;
		path++;
		dev = malloc(len + 1);
		memcpy(dev, dirname, len);
		dev[len] = '\0';
	} else {
		/* No colon is given. Is this device or dirname? */
		if (dirname[0] == '/') {
			/* Anything starts with '/' must be a dirname */
			dev = 0;
			path = dirname;
		} else {
			dev = strdup(dirname);
			path = 0;
		}
	}
	debug("dev=%s, path=%s\n", dev, path);

	if (dev && dev[0]) {
		if (!devopen(dev, &reopen)) {
			fsys = 0;
			goto out;
		}
		if (!reopen)
			fsys = 0;
	}

	if (path) {
		if (!fsys || fsys == &nullfs) {
			if (!mount_fs())
				goto out;
		}
		using_devsize = 0;
		if (!path[0]) {
			printf("No dirname is given.\n");
			goto out;
		}
	} else {
		fsys = &nullfs;
	}

	filepos = 0;
	errnum = 0;

	/* set "dir" function to list completions */
	print_possibilities = 1;

	retval = fsys->dir_func((char *) path);

	print_possibilities = 0;

out:
	if (dev)
		free(dev);

	return retval;
}

/* The following functions are BSD-L, Copyright 2011 secunet AG
 * licensing is chosen to simplify migration to libpayload at some point */

struct dirent **opendir_s;
int opendir_ssize, opendir_selem;

void print_a_completion(char *name)
{
	opendir_s[opendir_selem] = malloc(sizeof(struct dirent));
	opendir_s[opendir_selem]->d_name = strdup(name);
	opendir_selem++;
	if (opendir_selem == opendir_ssize) {
		opendir_ssize *= 2;
		opendir_s = realloc(opendir_s, sizeof(struct dirent*)*opendir_ssize);
	}
	opendir_s[opendir_selem] = 0;
}

DIR *opendir(const char *path)
{
	DIR *p = malloc(sizeof(DIR));

	if (opendir_s) free(opendir_s);
	opendir_ssize = 32;
	opendir_selem = 0;
	opendir_s = malloc(sizeof(struct dirent*)*opendir_ssize);
	dir(path);
	p->items = opendir_s;
	p->cur = opendir_s;
	return p;
}

struct dirent *readdir(DIR *dirp)
{
	if (NULL == *dirp->cur) return NULL;

	return *(dirp->cur++);
}

int closedir(DIR *dirp)
{
	if (!dirp) return -1;

	struct dirent **cur = dirp->items;

	while (*cur) {
		free(*cur);
		cur++;
	}
	free(dirp->items);
	free(dirp);

	return 0;
}

static int (*metafilter_flt)(const struct dirent *);
static int (*metafilter_cmp)(const struct dirent **, const struct dirent **);

static int metafilter(const struct dirent **a, const struct dirent **b)
{
	/* make sure that filtered out entries end up at the end, for simple elimination */
	if ((metafilter_flt(*a) == 0) && (metafilter_flt(*b) == 0))
		return 0;
	if (metafilter_flt(*a) == 0)
		return 1;
	if (metafilter_flt(*b) == 0)
		return -1;
	return metafilter_cmp(a, b);
}

int scandir(const char *path, struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	DIR *dirp = opendir(path);

	struct dirent **e = dirp->items;
	int nelem = 0;

	while (*e) {
		nelem++;
		e++;
	}

	if (filter) {
		metafilter_flt = filter;
		metafilter_cmp = compar;
		compar = metafilter;
	}

	*namelist = malloc(sizeof(struct dirent*)*nelem);
	memcpy(*namelist, dirp->items, sizeof(struct dirent*)*nelem);
	qsort(*namelist, nelem, sizeof(struct dirent*), (int(*)(const void*, const void*))compar);

	// if things were filtered, truncate them away
	if (filter) {
		int nelem2;

		e = *namelist;
		nelem2 = 0;
		while ((nelem > nelem2) && filter(*e)) {
			nelem2++;
			e++;
		}
		realloc(namelist, sizeof(struct dirent**)*nelem2);
	}

	return nelem;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
	const struct dirent *a1 = *(const struct dirent **)(a);
	const struct dirent *b1 = *(const struct dirent **)(b);

	return strcmp(a1->d_name, b1->d_name);
}

