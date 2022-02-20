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

#ifndef LOADER_H
#define LOADER_H

#include <config.h>

#define LOADER_NOT_SUPPORT 0xbadf11e

int elf_load(const char *filename, const char *cmdline);

#ifdef CONFIG_LINUX_LOADER
int linux_load(const char *filename, const char *cmdline);
#else
#define linux_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#ifdef CONFIG_WINCE_LOADER
int wince_load(const char *filename, const char *cmdline);
#else
#define wince_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#ifdef CONFIG_ARTEC_BOOT
int artecboot_load(const char *filename, const char *cmdline);
#else
#define artecboot_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#endif /* LOADER_H */
