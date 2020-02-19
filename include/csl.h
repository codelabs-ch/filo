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
 */

#ifndef CSL_H
#define CSL_H

struct csl_file_operations {
	int (*open)(const char *filename);
	int (*read)(void *buf, unsigned long len);
	unsigned long (*size)(void);
	void (*close)(void);
};

extern struct csl_file_operations csl_fs_ops;

#endif /* CSL_H */
