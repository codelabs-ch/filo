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

/* ELF Boot loader
 * As we have seek, this implementation can be straightforward.
 * 2003-07 by SONE Takeshi
 */

#include <libpayload.h>
#include <config.h>
#include <timer.h>
#include <sys_info.h>
#include <loader.h>
#include <elf.h>
#include <arch/elf.h>
#include <elf_boot.h>
#include <ipchecksum.h>
#include <fs.h>
#define DEBUG_THIS CONFIG_DEBUG_ELFBOOT
#include <debug.h>

extern unsigned int start_elf(unsigned long entry_point, unsigned long param);
extern char _start, _end;

static char *image_name, *image_version;

static int check_mem_ranges(Elf_phdr *phdr, int phnum)
{
    int i, j;
    unsigned long start, end;
    unsigned long prog_start, prog_end;
    struct memrange *mem;
    struct sysinfo_t *info = &lib_sysinfo;

    prog_start = virt_to_phys(&_start);
    prog_end = virt_to_phys(&_end);

    for (i = 0; i < phnum; i++) {
	if (phdr[i].p_type != PT_LOAD)
	    continue;
	start = phdr[i].p_paddr;
	end = start + phdr[i].p_memsz;
	if (start < prog_start && end > prog_start)
	    goto conflict;
	if (start < prog_end && end > prog_end)
	    goto conflict;
	for (j = 0; j < info->n_memranges; j++) {
	    mem = &info->memrange[j];
	    if (mem->base <= start && mem->base + mem->size >= end)
		break;
	}
	if (j >= info->n_memranges)
	    goto badseg;
    }
    return 1;

conflict:
    printf("%s occupies [%#lx-%#lx]\n", program_name, prog_start, prog_end);

badseg:
    printf("Segment %d [%#lx-%#lx] doesn't fit into memory\n", i, start, end-1);
    return 0;
}

static unsigned long process_image_notes(Elf_phdr *phdr, int phnum,
	unsigned short *sum_ptr)
{
    int i;
    char *buf = NULL;
    int retval = 0;
    unsigned long addr, end;
    Elf_Nhdr *nhdr;
    const char *name;
    void *desc;

    for (i = 0; i < phnum; i++) {
	if (phdr[i].p_type != PT_NOTE)
	    continue;
	buf = malloc(phdr[i].p_filesz);
	file_seek(phdr[i].p_offset);
	if (file_read(buf, phdr[i].p_filesz) != phdr[i].p_filesz) {
	    printf("Can't read note segment\n");
	    goto out;
	}
	addr = (unsigned long) buf;
	end = addr + phdr[i].p_filesz;
	while (addr < end) {
	    nhdr = (Elf_Nhdr *) addr;
	    addr += sizeof(Elf_Nhdr);
	    name = (const char *) addr;
	    addr += (nhdr->n_namesz+3) & ~3;
	    desc = (void *) addr;
	    addr += (nhdr->n_descsz+3) & ~3;

	    if (nhdr->n_namesz==sizeof(ELF_NOTE_BOOT)
		    && memcmp(name, ELF_NOTE_BOOT, sizeof(ELF_NOTE_BOOT))==0) {
		if (nhdr->n_type == EIN_PROGRAM_NAME) {
		    image_name = calloc(1, nhdr->n_descsz + 1);
		    memcpy(image_name, desc, nhdr->n_descsz);
		}
		if (nhdr->n_type == EIN_PROGRAM_VERSION) {
		    image_version = calloc(1, nhdr->n_descsz + 1);
		    memcpy(image_version, desc, nhdr->n_descsz);
		}
		if (nhdr->n_type == EIN_PROGRAM_CHECKSUM) {
		    *sum_ptr = *(unsigned short *) desc;
		    debug("Image checksum: %#04x\n", *sum_ptr);
		    /* Where in the file */
		    retval = phdr[i].p_offset
			+ (unsigned long) desc - (unsigned long) buf;
		}
	    }
	}
    }
out:
    if (buf)
	free(buf);
    return retval;
}

static int load_segments(Elf_phdr *phdr, int phnum,
	unsigned long checksum_offset)
{
    unsigned long bytes;
    int i;

    bytes = 0;
#if defined(DEBUG) && (DEBUG == 1)
    u64 start_time = timer_raw_value();
#endif
    for (i = 0; i < phnum; i++) {
	if (phdr[i].p_type != PT_LOAD)
	    continue;
	debug("segment %d addr:%#x file:%#x mem:%#x ",
		i, phdr[i].p_paddr, phdr[i].p_filesz, phdr[i].p_memsz);
	file_seek(phdr[i].p_offset);
	debug("loading... ");
	if (file_read(phys_to_virt(phdr[i].p_paddr), phdr[i].p_filesz)
		!= phdr[i].p_filesz) {
	    printf("Can't read program segment %d\n", i);
	    return 0;
	}
	bytes += phdr[i].p_filesz;
	debug("clearing... ");
	memset(phys_to_virt(phdr[i].p_paddr + phdr[i].p_filesz), 0,
		phdr[i].p_memsz - phdr[i].p_filesz);
	if (phdr[i].p_offset <= checksum_offset
		&& phdr[i].p_offset + phdr[i].p_filesz >= checksum_offset+2) {
	    debug("clearing checksum... ");
	    memset(phys_to_virt(phdr[i].p_paddr + checksum_offset
			- phdr[i].p_offset), 0, 2);
	}
	debug("ok\n");

    }
#if defined(DEBUG) && (DEBUG == 1)
    u64 time = (timer_raw_value() - start_time) / (timer_hz() * 1000);
    debug("Loaded %lu bytes in %lldms (%luKB/s)\n", bytes, time,
	    time? bytes/time : 0);
#endif
    return 1;
}

static int verify_image(Elf_ehdr *ehdr, Elf_phdr *phdr, int phnum,
	unsigned short image_sum)
{
    unsigned short sum, part_sum;
    unsigned long offset;
    int i;

    sum = 0;
    offset = 0;

    part_sum = ipchksum(ehdr, sizeof *ehdr);
    sum = add_ipchksums(offset, sum, part_sum);
    offset += sizeof *ehdr;

    part_sum = ipchksum(phdr, phnum * sizeof(*phdr));
    sum = add_ipchksums(offset, sum, part_sum);
    offset += phnum * sizeof(*phdr);

    for (i = 0; i < phnum; i++) {
	if (phdr[i].p_type != PT_LOAD)
	    continue;
	part_sum = ipchksum(phys_to_virt(phdr[i].p_paddr), phdr[i].p_memsz);
	sum = add_ipchksums(offset, sum, part_sum);
	offset += phdr[i].p_memsz;
    }

    if (sum != image_sum) {
	printf("Verify FAILED (image:%#04x vs computed:%#04x)\n",
		image_sum, sum);
	return 0;
    }
    return 1;
}

static inline unsigned int padded(unsigned int s)
{
    return ((s + 3) & ~3);
}

static Elf_Bhdr *add_boot_note(Elf_Bhdr *bhdr, const char *name,
	unsigned type, const char *desc, unsigned descsz)
{
    Elf_Nhdr nhdr;
    unsigned ent_size, new_size, pad;
    char *addr;

    if (!bhdr)
	return NULL;

    nhdr.n_namesz = name? strlen(name)+1 : 0;
    nhdr.n_descsz = descsz;
    nhdr.n_type = type;
    ent_size = sizeof(nhdr) + padded(nhdr.n_namesz) + padded(nhdr.n_descsz);
    if (bhdr->b_size + ent_size > 0xffff) {
	printf("Boot notes too big\n");
	free(bhdr);
	return NULL;
    }
    if (bhdr->b_size + ent_size > bhdr->b_checksum) {
	do {
	    new_size = bhdr->b_checksum * 2;
	} while (new_size < bhdr->b_size + ent_size);
	if (new_size > 0xffff)
	    new_size = 0xffff;
	debug("expanding boot note size to %u\n", new_size);
	bhdr = realloc(bhdr, new_size);
	bhdr->b_checksum = new_size;
    }

    addr = (char *) bhdr;
    addr += bhdr->b_size;
    memcpy(addr, &nhdr, sizeof(nhdr));
    addr += sizeof(nhdr);

    memcpy(addr, name, nhdr.n_namesz);
    addr += nhdr.n_namesz;
    pad = padded(nhdr.n_namesz) - nhdr.n_namesz;
    memset(addr, 0, pad);
    addr += pad;

    memcpy(addr, desc, nhdr.n_descsz);
    addr += nhdr.n_descsz;
    pad = padded(nhdr.n_descsz) - nhdr.n_descsz;
    memset(addr, 0, pad);
    addr += pad;

    bhdr->b_size += ent_size;
    bhdr->b_records++;
    return bhdr;
}

static inline Elf_Bhdr *add_note_string(Elf_Bhdr *bhdr, const char *name,
	unsigned type, const char *desc)
{
    return add_boot_note(bhdr, name, type, desc, strlen(desc) + 1);
}

static Elf_Bhdr *build_boot_notes(const char *cmdline)
{
    Elf_Bhdr *bhdr;
    extern struct sys_info sys_info;

    bhdr = malloc(256);
    bhdr->b_signature = ELF_BHDR_MAGIC;
    bhdr->b_size = sizeof *bhdr;
    bhdr->b_checksum = 256; /* XXX cache the current buffer size here */
    bhdr->b_records = 0;

    if (sys_info.firmware)
    bhdr = add_note_string(bhdr, NULL, EBN_FIRMWARE_TYPE, sys_info.firmware);
    bhdr = add_note_string(bhdr, NULL, EBN_BOOTLOADER_NAME, program_name);
    bhdr = add_note_string(bhdr, NULL, EBN_BOOTLOADER_VERSION, program_version);
    if (cmdline)
	bhdr = add_note_string(bhdr, NULL, EBN_COMMAND_LINE, cmdline);
    if (!bhdr)
	return bhdr;
    bhdr->b_checksum = 0;
    bhdr->b_checksum = ipchksum(bhdr, bhdr->b_size);
    return bhdr;
}

int elf_load(const char *filename, const char *cmdline)
{
    Elf_ehdr ehdr;
    Elf_phdr *phdr = NULL;
    unsigned long phdr_size;
    unsigned long checksum_offset;
    unsigned short checksum=0;
    Elf_Bhdr *boot_notes = NULL;
    int retval = -1;
    int image_retval;
#ifdef CONFIG_PCMCIA_CF
    unsigned char *cf_bar;
    int i;
#endif

    image_name = image_version = 0;

    if (!file_open(filename))
	goto out;

    if (file_read(&ehdr, sizeof ehdr) != sizeof ehdr) {
	debug("Can't read ELF header\n");
	retval = LOADER_NOT_SUPPORT;
	goto out;
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0
	    || ehdr.e_ident[EI_MAG1] != ELFMAG1
	    || ehdr.e_ident[EI_MAG2] != ELFMAG2
	    || ehdr.e_ident[EI_MAG3] != ELFMAG3
	    || ehdr.e_ident[EI_CLASS] != ARCH_ELF_CLASS
	    || ehdr.e_ident[EI_DATA] != ARCH_ELF_DATA
	    || ehdr.e_ident[EI_VERSION] != EV_CURRENT
	    || ehdr.e_type != ET_EXEC
	    || !ARCH_ELF_MACHINE_OK(ehdr.e_machine)
	    || ehdr.e_version != EV_CURRENT
	    || ehdr.e_phentsize != sizeof(Elf_phdr)) {
	debug("Not a bootable ELF image\n");
	retval = LOADER_NOT_SUPPORT;
	goto out;
    }

    phdr_size = ehdr.e_phnum * sizeof *phdr;
    phdr = malloc(phdr_size);
    file_seek(ehdr.e_phoff);
    if (file_read(phdr, phdr_size) != phdr_size) {
	printf("Can't read program header\n");
	goto out;
    }

    if (!check_mem_ranges(phdr, ehdr.e_phnum))
	goto out;

    checksum_offset = process_image_notes(phdr, ehdr.e_phnum, &checksum);

    printf("Loading %s", image_name ? image_name : "image");
    if (image_version)
	printf(" version %s", image_version);
    printf("...\n");

    if (!load_segments(phdr, ehdr.e_phnum, checksum_offset))
	goto out;

    if (checksum_offset) {
	if (!verify_image(&ehdr, phdr, ehdr.e_phnum, checksum))
	    goto out;
    }

    file_close();

    boot_notes = build_boot_notes(cmdline);

#if CONFIG_PCMCIA_CF
    cf_bar = phys_to_virt(pci_read_config32(PCI_DEV(0, 0xa, 1), 0x10));
    for( i = 0x836 ; i < 0x840 ; i++){
        cf_bar[i] = 0;
    }
#endif

    debug("current time: %lu\n", currticks());

    debug("entry point is %#x\n", ehdr.e_entry);
    printf("Jumping to entry point...\n");
    image_retval = start_elf(ehdr.e_entry, virt_to_phys(boot_notes));

    console_init();
    printf("Image returned with return value %#x\n", image_retval);
    retval = 0;

out:
    if (phdr)
	free(phdr);
    if (boot_notes)
	free(boot_notes);
    if (image_name)
	free(image_name);
    if (image_version)
	free(image_version);
    file_close();
    return retval;
}
