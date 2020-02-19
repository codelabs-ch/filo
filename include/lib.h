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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.
 */

#ifndef LIB_H
#define LIB_H

#include <libpayload.h>
#include <config.h>

unsigned long long simple_strtoull(const char *cp,char **endp,unsigned int base);
unsigned long long strtoull_with_suffix(const char *cp,char **endp,unsigned int base);

long long simple_strtoll(const char *cp,char **endp,unsigned int base);

#define abort() halt()

struct sys_info;

#if IS_ENABLED(CONFIG_CSL_BOOT)
int csl_load(const char *filename, const char *cmdline);
#else
#define csl_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#endif /* LIB_H */
