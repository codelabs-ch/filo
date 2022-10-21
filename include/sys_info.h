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

#ifndef SYS_INFO_H
#define SYS_INFO_H

/* Information collected from firmware/bootloader */

struct sys_info {
    /* Values passed by bootloader */
    unsigned long boot_type;
    unsigned long boot_data;
    unsigned long boot_arg;

    char *firmware; /* "PCBIOS", "LinuxBIOS", etc. */
    char *command_line; /* command line given to us */
#if 0
    /* memory map */
    int n_memranges;
    struct memrange {
	unsigned long long base;
	unsigned long long size;
    } *memrange;
#endif
};

void collect_sys_info(struct sys_info *info);
void collect_elfboot_info(struct sys_info *info);
void collect_linuxbios_info(struct sys_info *info);
const char *get_cb_version(void);

/* Our name and version. I want to see single instance of these in the image */
extern const char *const program_name, *const program_version;

#endif /* SYS_INFO_H */
