/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2005   Free Software Foundation, Inc.
 *  Copyright (C) 2009   coresystems GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2,
 *  as published by the Free Software Foundation.
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

#include "filesys.h"
#include <cbfs.h>

struct cbfs_file *file;
void* fileptr;

int
cbfs_mount (void)
{
  /* Check if it's a 4GB device (ie. RAM) */
  if (part_length != (1<<(32-9)))
    return 0;

  /* CBFS? */
  if (get_cbfs_header() == (void*)0xffffffff)
    return 0;

  return 1;
}

int
cbfs_read (char *buf, int len)
{
  if (filepos + len > filemax) {
    len = filemax - filepos;
  }
  memcpy(buf, fileptr+filepos, len);
  filepos+=len;
  return len;
}

int
cbfs_dir (char *dirname)
{
  if (dirname[0]=='/') dirname++;
  file = cbfs_find(dirname);
  if (!file) {
    errnum = ERR_FILE_NOT_FOUND;
    return 0;
  }

  filepos = 0;
  filemax = be32toh(file->len);
  fileptr = (void*)file+be32toh(file->offset);
  return 1;
}

