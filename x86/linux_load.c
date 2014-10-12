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
#include <coreboot_tables.h>
#include <config.h>
#include <fs.h>
#include "context.h"
#include "segment.h"

#define DEBUG_THIS CONFIG_DEBUG_LINUXLOAD
#include <debug.h>

#define LINUX_PARAM_LOC 0x90000
#define COMMAND_LINE_LOC 0x91000
#define GDT_LOC 0x92000
#define STACK_LOC 0x93000

#define E820MAX	32		/* number of entries in E820MAP */
struct e820entry {
	unsigned long long addr;	/* start of memory segment */
	unsigned long long size;	/* size of memory segment */
	unsigned long type;	/* type of memory segment */
#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3	/* usable as RAM once ACPI tables have been read */
#define E820_NVS	4
};

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
} __attribute__ ((packed));

/* Paramters passed to 32-bit part of Linux
 * This is another view of the structure above.. */
struct linux_params {
	u8 orig_x;		/* 0x00 */
	u8 orig_y;		/* 0x01 */
	u16 ext_mem_k;		/* 0x02 -- EXT_MEM_K sits here */
	u16 orig_video_page;	/* 0x04 */
	u8 orig_video_mode;	/* 0x06 */
	u8 orig_video_cols;	/* 0x07 */
	u16 unused2;		/* 0x08 */
	u16 orig_video_ega_bx;	/* 0x0a */
	u16 unused3;		/* 0x0c */
	u8 orig_video_lines;	/* 0x0e */
	u8 orig_video_isVGA;	/* 0x0f */
	u16 orig_video_points;	/* 0x10 */

	/* VESA graphic mode -- linear frame buffer */
	u16 lfb_width;		/* 0x12 */
	u16 lfb_height;		/* 0x14 */
	u16 lfb_depth;		/* 0x16 */
	u32 lfb_base;		/* 0x18 */
	u32 lfb_size;		/* 0x1c */
	u16 cl_magic;		/* 0x20 */
#define CL_MAGIC_VALUE 0xA33F
	u16 cl_offset;		/* 0x22 */
	u16 lfb_linelength;	/* 0x24 */
	u8 red_size;		/* 0x26 */
	u8 red_pos;		/* 0x27 */
	u8 green_size;		/* 0x28 */
	u8 green_pos;		/* 0x29 */
	u8 blue_size;		/* 0x2a */
	u8 blue_pos;		/* 0x2b */
	u8 rsvd_size;		/* 0x2c */
	u8 rsvd_pos;		/* 0x2d */
	u16 vesapm_seg;		/* 0x2e */
	u16 vesapm_off;		/* 0x30 */
	u16 pages;		/* 0x32 */
	u8 reserved4[12];	/* 0x34 -- 0x3f reserved for future expansion */

	//struct apm_bios_info apm_bios_info;   /* 0x40 */
	u8 apm_bios_info[0x40];
	//struct drive_info_struct drive_info;  /* 0x80 */
	u8 drive_info[0x20];
	//struct sys_desc_table sys_desc_table; /* 0xa0 */
	u8 sys_desc_table[0x140];
	u32 alt_mem_k;		/* 0x1e0 */
	u8 reserved5[4];	/* 0x1e4 */
	u8 e820_map_nr;		/* 0x1e8 */
	u8 reserved6[9];	/* 0x1e9 */
	u16 mount_root_rdonly;	/* 0x1f2 */
	u8 reserved7[4];	/* 0x1f4 */
	u16 ramdisk_flags;	/* 0x1f8 */
#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000
	u8 reserved8[2];	/* 0x1fa */
	u16 orig_root_dev;	/* 0x1fc */
	u8 reserved9[1];	/* 0x1fe */
	u8 aux_device_info;	/* 0x1ff */
	u8 reserved10[2];	/* 0x200 */
	u8 param_block_signature[4];	/* 0x202 */
	u16 param_block_version;	/* 0x206 */
	u8 reserved11[8];	/* 0x208 */
	u8 loader_type;		/* 0x210 */
#define LOADER_TYPE_LOADLIN         1
#define LOADER_TYPE_BOOTSECT_LOADER 2
#define LOADER_TYPE_SYSLINUX        3
#define LOADER_TYPE_ETHERBOOT       4
#define LOADER_TYPE_KERNEL          5
	u8 loader_flags;	/* 0x211 */
	u8 reserved12[2];	/* 0x212 */
	u32 kernel_start;	/* 0x214 */
	u32 initrd_start;	/* 0x218 */
	u32 initrd_size;	/* 0x21c */
	u8 reserved12_5[8];	/* 0x220 */
	u32 cmd_line_ptr;	/* 0x228 */
	u32 initrd_addr_max;	/* 0x22c */
	u32 kernel_alignment;	/* 0x230 */
	u8 relocatable_kernel;	/* 0x234 */
	u8 reserved13[155];		/* 0x22c */
	struct e820entry e820_map[E820MAX];	/* 0x2d0 */
	u8 reserved16[688];	/* 0x550 */
#define COMMAND_LINE_SIZE 256
	/* Command line is copied here by 32-bit i386/kernel/head.S.
	 * So I will follow the boot protocol, rather than putting it
	 * directly here. --ts1 */
	u8 command_line[COMMAND_LINE_SIZE];	/* 0x800 */
	u8 reserved17[1792];	/* 0x900 - 0x1000 */
};

u64 forced_memsize;

/* Load the first part the file and check if it's Linux */
static u32 load_linux_header(struct linux_header *hdr)
{
	u32 kern_addr = 0;
	int load_high;

	if (file_read(hdr, sizeof *hdr) != sizeof *hdr) {
		printf("Can't read Linux header\n");
		return 0;
	}

	if (hdr->boot_sector_magic != 0xaa55) {
		printf("Not a Linux kernel image\n");
		return 0;
	}

	/* Linux is found. Print some information */
	if (memcmp(hdr->header_magic, "HdrS", 4) != 0) {
		/* This may be floppy disk image or something.
		 * Perform a simple (incomplete) sanity check. */
		if (hdr->setup_sects >= 16 || file_size() - (hdr->setup_sects << 9) >= 512 << 10) {
			printf("This looks like a bootdisk image but not like Linux...\n");
			return 0;
		}

		printf("Possible very old Linux");
		/* This kernel does not even have a protocol version.
		 * Force the value. */
		hdr->protocol_version = 0;	/* pre-2.00 */
	} else {
		printf("Found Linux");
	}

	if (hdr->protocol_version >= 0x200 && hdr->kver_addr) {
		char kver[256];
		file_seek(hdr->kver_addr + 0x200);
		if (file_read(kver, sizeof kver) != 0) {
			kver[255] = 0;
			printf(" version %s", kver);
		}
	}
	debug(" (protocol %#x)", hdr->protocol_version);

	load_high = 0;
	if (hdr->protocol_version >= 0x200) {
		debug(" (loadflags %#x)", hdr->loadflags);
		load_high = hdr->loadflags & 1;
	}

	/* determine kernel load address */
	if (hdr->protocol_version >= 0x20a) {
		if (hdr->pref_address >> 32) {
			debug(" (ignoring 64bit pref_address)");
		} else {
			kern_addr = hdr->pref_address;
		}
	}

	if (hdr->protocol_version >= 0x205 && hdr->relocatable_kernel) {
		printf(" relocatable");
	}

	if (load_high) {
		printf(" bzImage");
		if (kern_addr == 0)
			kern_addr = 0x100000;
	} else {
		printf(" zImage or Image");
		if (kern_addr == 0)
			kern_addr = 0x1000;
	}

	printf(".\n");

	return kern_addr;
}

/* Set up parameters for 32-bit kernel */
static void
init_linux_params(struct linux_params *params, struct linux_header *hdr)
{
	debug("Setting up paramters at %#lx\n", virt_to_phys(params));
	memset(params, 0, sizeof *params);

	/* Copy some useful values from header */
	params->mount_root_rdonly = hdr->root_flags;
	params->orig_root_dev = hdr->root_dev;

	/* Video parameters.
	 * This assumes we have VGA in standard 80x25 text mode,
	 * just like our vga.c does.
	 * Cursor position is filled later to allow some more printf's.
	 */
	params->orig_video_mode = 3;
	params->orig_video_cols = 80;
	params->orig_video_lines = 25;
	params->orig_video_isVGA = 1;
	params->orig_video_points = 16;

	params->loader_type = 0xff;	/* Unregistered Linux loader */

	/* copy alignment fields for relocatable kernels */
	if (hdr->protocol_version >= 0x205) {
		params->relocatable_kernel = hdr->relocatable_kernel;
		params->kernel_alignment = hdr->kernel_alignment;
	}
}

/* Memory map */
static void set_memory_size(struct linux_params *params)
{
	int i;
	uint64_t end;
	u32 ramtop = 0;
	struct e820entry *linux_map;
	struct sysinfo_t *info = &lib_sysinfo;
	struct memrange *filo_map;

	linux_map = params->e820_map;
	filo_map = info->memrange;
	for (i = 0; i < info->n_memranges; i++, linux_map++, filo_map++) {
		if (i < E820MAX) {
			/* Convert to BIOS e820 style */
			linux_map->addr = filo_map->base;
			linux_map->size = filo_map->size;
			linux_map->type = filo_map->type;
			debug("%016llx - %016llx (%d)\n", linux_map->addr,
			      linux_map->addr + linux_map->size,
			      linux_map->type);
			params->e820_map_nr = i + 1;
		}

		/* Find out top of RAM. XXX This ignores hole above 1MB */
		end = filo_map->base + filo_map->size;
		if (end < (1ULL << 32)) { /* don't count memory above 4GB */
			if (end > ramtop)
				ramtop = (u32) end;
		}
	}

	debug("ramtop=%#x\n", ramtop);
	/* Size of memory above 1MB in KB */
	params->alt_mem_k = (ramtop - (1 << 20)) >> 10;
	/* old style, 64MB max */
	if (ramtop >= (64 << 20))
		params->ext_mem_k = (63 << 10);
	else
		params->ext_mem_k = params->alt_mem_k;
	debug("ext_mem_k=%d, alt_mem_k=%d\n", params->ext_mem_k,
	      params->alt_mem_k);
}

/* Video mode */
static void set_video_mode(struct linux_params *params)
{
#if CONFIG_LP_COREBOOT_VIDEO_CONSOLE
	/* Are we running on a framebuffer console? */
	if (!lib_sysinfo.framebuffer)
		return;

	struct cb_framebuffer *fb = phys_to_virt(lib_sysinfo.framebuffer);

	params->lfb_width = fb->x_resolution;
	params->lfb_height = fb->y_resolution;
	params->lfb_depth = fb->bits_per_pixel;
	params->lfb_linelength = fb->bytes_per_line;
	params->lfb_base = fb->physical_address;

	// prolly not enough for the boot splash?!
	params->lfb_size =
	    (params->lfb_linelength * params->lfb_height + 65535) >> 16;
	params->red_size = fb->red_mask_size;
	params->red_pos = fb->red_mask_pos;
	params->green_size = fb->green_mask_size;
	params->green_pos = fb->green_mask_pos;
	params->blue_size = fb->blue_mask_size;
	params->blue_pos = fb->blue_mask_pos;
	params->rsvd_size = fb->reserved_mask_size;
	params->rsvd_pos = fb->reserved_mask_pos;
#endif
}

/*
 * Parse command line
 * Some parameters, like initrd=<file>, are not passed to kernel,
 * we are responsible to process them.
 * Parameters for kernel are copied to kern_cmdline. Returns name of initrd.
 */
static char *parse_command_line(const char *orig_cmdline,
				char *kern_cmdline)
{
	const char *start, *sep, *end, *val;
	char name[64];
	int len;
	int k_len;
	int to_kern;
	char *initrd = 0;
	int toolong = 0;

	forced_memsize = 0;

	if (!orig_cmdline) {
		*kern_cmdline = 0;
		return 0;
	}

	k_len = 0;
	debug("original command line: \"%s\"\n", orig_cmdline);
	debug("kernel command line at %#lx\n", virt_to_phys(kern_cmdline));

	start = orig_cmdline;
	while (*start == ' ')
		start++;
	while (*start) {
		end = strchr(start, ' ');
		if (!end)
			end = start + strlen(start);
		sep = strchr(start, '=');
		if (!sep || sep > end)
			sep = end;
		len = sep - start;
		if (len >= sizeof(name))
			len = sizeof(name) - 1;
		memcpy(name, start, len);
		name[len] = 0;

		if (*sep == '=') {
			val = sep + 1;
			len = end - val;
		} else {
			val = 0;
			len = 0;
		}

		/* Only initrd= and mem= are handled here. vga= is not,
		 * which I believe is a paramter to the realmode part of Linux,
		 * which we don't execute.
		 */
		if (strcmp(name, "initrd") == 0) {
			if (!val) {
				printf
				    ("Missing filename to initrd parameter\n");
			} else {
				initrd = malloc(len + 1);
				memcpy(initrd, val, len);
				initrd[len] = 0;
				debug("initrd=%s\n", initrd);
			}
			/* Don't pass this to kernel */
			to_kern = 0;
		} else if (strcmp(name, "mem") == 0) {
			if (!val) {
				printf
				    ("Missing value for mem parameter\n");
			} else {
				forced_memsize =
				    strtoull_with_suffix(val,
							 (char **) &val,
							 0);
				if (forced_memsize == 0)
					printf
					    ("Invalid mem option, ignored\n");
				if (val != end) {
					printf
					    ("Garbage after mem=<size>, ignored\n");
					forced_memsize = 0;
				}
				debug("mem=%Lu\n", forced_memsize);
			}
			/* mem= is for both loader and kernel */
			to_kern = 1;
		} else {
			to_kern = 1;
		}

		if (to_kern) {
			/* Copy to kernel command line buffer */
			if (k_len != 0)
				kern_cmdline[k_len++] = ' ';	/* put separator */
			len = end - start;
			if (k_len + len >= COMMAND_LINE_SIZE) {
				len = COMMAND_LINE_SIZE - k_len - 1;
				if (!toolong) {
					printf
					    ("Kernel command line is too long; truncated to "
					     "%d bytes\n",
					     COMMAND_LINE_SIZE - 1);
					toolong = 1;
				}
			}
			memcpy(kern_cmdline + k_len, start, len);
			k_len += len;
		}

		start = end;
		while (*start == ' ')
			start++;
	}
	kern_cmdline[k_len] = 0;
	debug("kernel command line (%d bytes): \"%s\"\n", k_len,
	      kern_cmdline);

	return initrd;
}

/* Set command line location */
static void set_command_line_loc(struct linux_params *params,
				 struct linux_header *hdr)
{
	if (hdr->protocol_version >= 0x202) {
		/* new style */
		params->cmd_line_ptr = COMMAND_LINE_LOC;
	} else {
		/* old style */
		params->cl_magic = CL_MAGIC_VALUE;
		params->cl_offset = COMMAND_LINE_LOC - LINUX_PARAM_LOC;
	}
}

/* Load 32-bit part of kernel */
static int load_linux_kernel(struct linux_header *hdr, u32 kern_addr)
{
	u32 kern_offset, kern_size;

	if (hdr->setup_sects == 0)
		hdr->setup_sects = 4;
	kern_offset = (hdr->setup_sects + 1) * 512;
	file_seek(kern_offset);
	kern_size = file_size() - kern_offset;
	debug("offset=%#x addr=%#x size=%#x\n", kern_offset, kern_addr,
	      kern_size);

	if (using_devsize) {
		printf("Attempt to load up to end of device as kernel; "
		       "specify the image size\n");
		return 0;
	}

	printf("Loading kernel... ");
	if (file_read(phys_to_virt(kern_addr), kern_size) != kern_size) {
		printf("Can't read kernel\n");
		return 0;
	}
	printf("ok\n");

	return kern_size;
}

static int load_initrd(struct linux_header *hdr,
		       u32 kern_end, struct linux_params *params,
		       const char *initrd_file)
{
	u32 max;
	u32 start, end, size;
	uint64_t forced;
	extern char _start[];
#if 0
	extern char _end[];
#endif

	if (!file_open(initrd_file)) {
		printf("Can't open initrd: %s\n", initrd_file);
		return -1;
	}
	if (using_devsize) {
		printf("Attempt to load up to end of device as initrd; "
		       "specify the image size\n");
		return -1;
	}
	size = file_size();


	/* Find out the kernel's restriction on how high the initrd can be
	 * placed */
	if (hdr->protocol_version >= 0x203)
		max = hdr->initrd_addr_max;
	else
		max = 0x38000000;	/* Hardcoded value for older kernels */

	/* FILO itself is at the top of RAM. (relocated)
	 * So, try putting initrd just below us. */
	end = virt_to_phys(_start - 1);
	if (end > max)
		end = max;

	/* If "mem=" option is given, we have to put the initrd within
	 * the specified range. */
	if (forced_memsize) {
		forced = forced_memsize;
		if (forced > max)
			forced = max;
		/* If the "mem=" is lower, it's easy */
		if (forced <= end)
			end = forced;
#if 0
		else {
			/* Otherwise, see if we can put it above us.
			 * 
			 *   This would be a good idea if we could easily find
			 *   out where the memory hole lives.
			 *   
			 *   There's nothing wrong with the initrd living below
			 *   FILO. (stepan)
			 *
			 *   The problems is even a 64bit kernel will live in
			 *   32bit address space, so if you have a lot of
			 *   memory and specify mem=xG with x>4, the maximum
			 *   allowed initrd address (2.6.x sets this to
			 *   0xffffffff) will be used for the high limit.
			 *   (offset 22c in vmlinuz)
			 *
			 *   you might want to enable this if you limit memory
			 *   with mem=yG with y<4.
			 */
			if (virt_to_phys(_end) + size <= forced)
				end = forced;	/* Ok */
		}
#endif
	}

	start = end - size;
	start &= ~0xfff;	/* page align */
	end = start + size;

	debug("start=%#x end=%#x\n", start, end);

	if (start < kern_end) {
		printf("Initrd is too big to fit in memory\n");
		return -1;
	}

	printf("Loading initrd... ");
	if (file_read(phys_to_virt(start), size) != size) {
		printf("Can't read initrd\n");
		return -1;
	}
	printf("ok\n");

	params->initrd_start = start;
	params->initrd_size = size;

	return 0;
}

static void hardware_setup(void)
{
	/* Disable nmi */
	outb(0x80, 0x70);

	/* Make sure any coprocessor is properly reset.. */
	outb(0, 0xf0);
	outb(0, 0xf1);

	/* we're getting screwed again and again by this problem of the 8259.
	 * so we're going to leave this lying around for inclusion into crt0.S
	 * on an as-needed basis. 
	 *
	 * well, that went ok, I hope. Now we have to reprogram the interrupts
	 * :-(
	 * we put them right after the intel-reserved hardware interrupts, at
	 * int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
	 * messed this up with the original PC, and they haven't been able to
	 * rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
	 * which is used for the internal hardware interrupts as well. We just
	 * have to reprogram the 8259's, and it isn't fun.
	 */

	outb(0x11, 0x20);	/* initialization sequence to 8259A-1 */
	outb(0x11, 0xA0);	/* and to 8259A-2 */

	outb(0x20, 0x21);	/* start of hardware int's (0x20) */
	outb(0x28, 0xA1);	/* start of hardware int's 2 (0x28) */

	outb(0x04, 0x21);	/* 8259-1 is master */
	outb(0x02, 0xA1);	/* 8259-2 is slave */

	outb(0x01, 0x21);	/* 8086 mode for both */
	outb(0x01, 0xA1);

	outb(0xFF, 0xA1);	/* mask off all interrupts for now */
	outb(0xFB, 0x21);	/* mask all irq's but irq2 which is cascaded */

#ifdef CONFIG_FLASHROM_LOCKDOWN
	/* lockdown flashROM */
	extern int flashrom_lockdown;
	extern int intel_lockdown_flash(void);
	extern int amd_lockdown_flash(void);

	if (flashrom_lockdown) {
		printf("Locking system flash memory...\n");
		if (intel_lockdown_flash() == 0) {
			printf("done (Intel)\n");
		} else if (amd_lockdown_flash() == 0) {
			printf("done (AMD)\n");
		} else {
			printf("FAILED!\n");
			delay(5);
		}
	} else {
		printf("Leaving system flash memory unlocked...\n");
	}
#endif
}

/* Start Linux */
static int start_linux(u32 kern_addr, struct linux_params *params)
{
	struct segment_desc *linux_gdt;
	struct context *ctx;
#ifdef CONFIG_LP_VGA_VIDEO_CONSOLE
	unsigned int cursor_x, cursor_y, cursor_en;
#endif
#ifdef CONFIG_PCMCIA_CF
	unsigned char *cf_bar;
	int i;
#endif

	ctx = init_context(phys_to_virt(STACK_LOC), 4096, 0);

	/* Linux expects GDT being in low memory */
	linux_gdt = phys_to_virt(GDT_LOC);
	memset(linux_gdt, 0, 13 * sizeof(struct segment_desc));
	/* Normal kernel code/data segments */
	linux_gdt[2] = gdt[FLAT_CODE];
	linux_gdt[3] = gdt[FLAT_DATA];
	/* 2.6 kernel uses 12 and 13, but head.S uses backward-compatible
	 * segments (2 and 3), so it SHOULD not be a problem. 
	 * However, some distro kernels (eg. RH9) with backported threading 
	 * patch use 12 and 13 also when booting... */
	linux_gdt[12] = gdt[FLAT_CODE];
	linux_gdt[13] = gdt[FLAT_DATA];
	ctx->gdt_base = GDT_LOC;
	ctx->gdt_limit = 14 * 8 - 1;
	ctx->cs = 0x10;
	ctx->ds = 0x18;
	ctx->es = 0x18;
	ctx->fs = 0x18;
	ctx->gs = 0x18;
	ctx->ss = 0x18;

	/* Parameter location */
	ctx->esi = virt_to_phys(params);

	/* Entry point */
	ctx->eip = kern_addr;

	/* set this field in any case to support relocatable kernels */
	params->kernel_start = kern_addr;

	debug("EIP=%#x\n", kern_addr);
	printf("Jumping to entry point...\n");

#ifdef CONFIG_LP_VGA_VIDEO_CONSOLE
	/* Update VGA cursor position.
	 * This must be here because the printf changes the value! */
	video_console_get_cursor(&cursor_x, &cursor_y, &cursor_en);
	params->orig_x = cursor_x;
	params->orig_y = cursor_y;
#endif

#ifdef CONFIG_PCMCIA_CF
	cf_bar = phys_to_virt(pci_read_config32(PCI_DEV(0, 0xa, 1), 0x10));
	for (i = 0x836; i < 0x840; i++) {
		cf_bar[i] = 0;
	}
#endif
	/* Go... */
	ctx = switch_to(ctx);

	/* It's impossible but... */
	printf("Returned with EAX=%#x\n", ctx->eax);

	return ctx->eax;
}

int linux_load(const char *file, const char *cmdline)
{
	struct linux_header hdr;
	struct linux_params *params;
	u32 kern_addr, kern_size;
	char *initrd_file = 0;

	if (!file_open(file))
		return -1;

	kern_addr = load_linux_header(&hdr);
	if (kern_addr == 0) {
		file_close();
		return LOADER_NOT_SUPPORT;
	}

	params = phys_to_virt(LINUX_PARAM_LOC);
	init_linux_params(params, &hdr);
	set_memory_size(params);
	set_video_mode(params);
	initrd_file =
	    parse_command_line(cmdline, phys_to_virt(COMMAND_LINE_LOC));
	set_command_line_loc(params, &hdr);

	kern_size = load_linux_kernel(&hdr, kern_addr);
	if (kern_size == 0) {
		if (initrd_file)
			free(initrd_file);
		file_close();
		return -1;
	}

	if (initrd_file) {
		if (load_initrd(&hdr, kern_addr + kern_size,
					params, initrd_file) != 0) {
			free(initrd_file);
			file_close();
			return -1;
		}
		free(initrd_file);
	}

	file_close();
#if defined(CONFIG_LP_USB)
	usb_exit();
#endif

	hardware_setup();

	start_linux(kern_addr, params);
	return 0;
}
