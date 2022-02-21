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
 */

#include <limits.h>
#include <cbfs.h>

#include "filesys.h"

static char *fileptr;

int cbfs_mount(void)
{
	/* Check if it's a 4GB device (ie. RAM) */
	if (part_length != 4 * (GiB / 512))
		return 0;

	return 1;
}

int cbfs_read(char *const buf, int len)
{
	if (!fileptr)
		return -1;

	if (filepos > filepos + len || filepos + len > filemax)
		len = filemax - filepos;

	memcpy(buf, fileptr + filepos, len);
	filepos += len;

	return len;
}

int cbfs_dir(const char *const dirname)
{
	const char *const path = dirname[0] == '/' ? dirname + 1 : dirname;
	size_t size;

	if (fileptr)
		cbfs_unmap(fileptr);

	fileptr = cbfs_map(path, &size);
	if (!fileptr)
		return 0;

	filepos = 0;
	filemax = size <= INT_MAX ? size : INT_MAX;
	return 1;
}
