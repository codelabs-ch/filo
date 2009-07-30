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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */


#ifndef LIB_H
#define LIB_H

#include <libpayload.h>
#include <config.h>

unsigned long long simple_strtoull(const char *cp,char **endp,unsigned int base);
unsigned long long strtoull_with_suffix(const char *cp,char **endp,unsigned int base);

void hexdump(const void *p, unsigned int len);

long long simple_strtoll(const char *cp,char **endp,unsigned int base);

#define abort() halt()

#define LOADER_NOT_SUPPORT 0xbadf11e

struct sys_info;
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

#endif /* LIB_H */
