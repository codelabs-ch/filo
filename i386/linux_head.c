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

/*
 * Linux/i386 loader
 * Supports bzImage, zImage and Image format.
 *
 * Based on work by Steve Gehlbach.
 * Portions are taken from mkelfImage.
 *
 * 2003-09 by SONE Takeshi
 */

#include <libpayload.h>
#include <libpayload-config.h>

/* The header of Linux/i386 kernel */
struct linux_header {
	u8 reserved1[0x1f1];	/* 0x000 */
	u8 setup_sects;		/* 0x1f1 */
	u16 root_flags;		/* 0x1f2 */
	u32 syssize;		/* 0x1f4 (2.04+) */
	u8 reserved2[2];	/* 0x1f8 */
	u16 vid_mode;		/* 0x1fa */
	u16 root_dev;		/* 0x1fc */
	u16 boot_sector_magic;	/* 0x1fe */
	/* 2.00+ */
	u8 reserved3[2];	/* 0x200 */
	u8 header_magic[4];	/* 0x202 */
	u16 protocol_version;	/* 0x206 */
	u32 realmode_swtch;	/* 0x208 */
	u16 start_sys;		/* 0x20c */
	u16 kver_addr;		/* 0x20e */
	u8 type_of_loader;	/* 0x210 */
	u8 loadflags;		/* 0x211 */
	u16 setup_move_size;	/* 0x212 */
	u32 code32_start;	/* 0x214 */
	u32 ramdisk_image;	/* 0x218 */
	u32 ramdisk_size;	/* 0x21c */
	u8 reserved4[4];	/* 0x220 */
	/* 2.01+ */
	u16 heap_end_ptr;	/* 0x224 */
	u8 reserved5[2];	/* 0x226 */
	/* 2.02+ */
	u32 cmd_line_ptr;	/* 0x228 */
	/* 2.03+ */
	u32 initrd_addr_max;	/* 0x22c */
	/* 2.05+ */
	u32 kernel_alignment;	/* 0x230 */
	u8 relocatable_kernel;	/* 0x234 */
	u8 min_alignment;	/* 0x235 (2.10+) */
	u8 reserved6[2];	/* 0x236 */
	/* 2.06+ */
	u32 cmdline_size;	/* 0x238 */
	/* 2.07+ */
	u32 hardware_subarch;	/* 0x23c */
	u64 hardware_subarch_data;/* 0x240 */
	/* 2.08+ */
	u32 payload_offset;	/* 0x248 */
	u32 payload_length;	/* 0x24c */
	/* 2.09+ */
	u64 setup_data;		/* 0x250 */
	/* 2.10+ */
	u64 pref_address;	/* 0x258 */
	u32 init_size;		/* 0x260 */
	u8 filler[0x1000-0x264];
} __attribute__ ((packed));

struct linux_header data = {
	.setup_sects = 7,
	.boot_sector_magic = 0xaa55,
	.header_magic = { 'H', 'd', 'r', 'S' },
	.protocol_version = 0x200,
	.loadflags = 1,
};
