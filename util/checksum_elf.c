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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#if defined(__GLIBC__)
#define USE_BSD
#include <endian.h>
#include <byteswap.h>
#else
#include "byteorder.h"
#endif
#define STDINT_H
#include "elf.h"
#include "elf_boot.h"
#include "ipchecksum.h"
#include "ebchecksum.h"

#if TARGET_DATA != ELFDATA2LSB && TARGET_DATA != ELFDATA2MSB
#error Invalid TARGET_DATA 
#endif

#if TARGET_CLASS == ELFCLASS32
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Phdr Elf32_Phdr
# if TARGET_DATA == ELFDATA2LSB
#  define checksum_elf checksum_elf32le
# else
#  define checksum_elf checksum_elf32be
# endif
#elif TARGET_CLASS == ELFCLASS64
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Phdr Elf64_Phdr
# if TARGET_DATA == ELFDATA2LSB
#  define checksum_elf checksum_elf64le
# else
#  define checksum_elf checksum_elf64be
# endif
#else
# error Invalid TARGET_CLASS 
#endif

#define HOST_DATA (BYTE_ORDER==BIG_ENDIAN ? ELFDATA2MSB : ELFDATA2LSB)
#if HOST_DATA != TARGET_DATA
# define BSWAP(x) \
    sizeof(x)==1 ? (x) \
    : sizeof(x)==2 ? bswap_16(x) \
    : sizeof(x)==4 ? bswap_32(x) \
    : sizeof(x)==8 ? bswap_64(x) \
    : *(int*)0 /* Error, do a segfault to catch */
#else
# define BSWAP(x) (x) /* nop */
#endif

static int process_note_segment(FILE *fp, const char *filename,
	off_t note_off, unsigned long note_size, 
	unsigned short *image_sum_ptr, off_t *sum_offset_ptr)
{
    int retval = -1;
    void *notes = NULL;
    unsigned long note_addr, note_end;
    Elf_Nhdr *nhdr;
    unsigned int namesz, descsz, type;
    char *name;
    void *desc;

    notes = malloc(note_size);
    if (!notes) {
	perror("Can't allocate memory for notes");
	goto out;
    }
    if (fseek(fp, note_off, SEEK_SET) != 0) {
	perror("Can't seek to notes");
	goto out;
    }
    if (fread(notes, note_size, 1, fp) != 1) {
	perror("Can't read notes");
	goto out;
    }

    /* Find note for ELFBoot checksum */
    retval = 0;
    note_addr = (unsigned long) notes;
    note_end = note_addr + note_size;
    while (note_addr < note_end) {
	nhdr = (Elf_Nhdr *) note_addr;
	namesz = BSWAP(nhdr->n_namesz);
	descsz = BSWAP(nhdr->n_descsz);
	type = BSWAP(nhdr->n_type);
	note_addr += sizeof(Elf_Nhdr);
	name = (char *) note_addr;
	note_addr += (namesz + 3) & ~3;
	desc = (void *) note_addr;
	note_addr += (descsz + 3) & ~3;

	if (namesz != sizeof(ELF_NOTE_BOOT)
		|| memcmp(name, ELF_NOTE_BOOT, sizeof(ELF_NOTE_BOOT)) != 0)
	    continue;

	if (verbose >= 2) {
	    if (type == EIN_PROGRAM_NAME)
		printf("Program name: %.*s\n", (int) descsz, (char *) desc);
	    else if (type == EIN_PROGRAM_VERSION)
		printf("Program version: %.*s\n", (int) descsz, (char *) desc);
	}
	if (type == EIN_PROGRAM_CHECKSUM) {
	    retval = 1;
	    *image_sum_ptr = BSWAP(*(unsigned short *) desc);
	    if (verbose >= 2)
		printf("Image checksum: %#04x\n", *image_sum_ptr);
	    /* Where in the file */
	    *sum_offset_ptr = note_off
		+ (unsigned long) desc - (unsigned long) notes;
	}
    }
out:
    if (notes)
	free(notes);
    return retval;
}

static int do_checksum(FILE *fp, const char *filename, Elf_Ehdr *ehdr, Elf_Phdr *phdr)
{
    char buf[8192];
    unsigned short sum, part_sum;
    off_t offset;
    unsigned long phsize;
    unsigned long seg_size, to_read, size_read;
    int i;

    sum = ipchksum(ehdr, sizeof *ehdr);
    offset = sizeof ehdr;

    phsize = BSWAP(ehdr->e_phnum) * sizeof(*phdr);
    part_sum = ipchksum(phdr, phsize);
    sum = add_ipchksums(offset, sum, part_sum);
    offset += phsize;
    for (i = 0; i < (BSWAP(ehdr->e_phnum)); i++) {
	if (BSWAP(phdr[i].p_type) != PT_LOAD)
	    continue;

	if (fseek(fp, BSWAP(phdr[i].p_offset), SEEK_SET) != 0) {
	    perror("Can't seek to program segment");
	    return -1;
	}
	seg_size = BSWAP(phdr[i].p_filesz);
	while (seg_size > 0) {
	    to_read = seg_size;
	    if (to_read > sizeof buf)
		to_read = sizeof buf;
	    size_read = fread(buf, 1, to_read, fp);
	    if (size_read <= 0) {
		perror("Can't read program segment");
		return -1;
	    }
	    part_sum = ipchksum(buf, size_read);
	    sum = add_ipchksums(offset, sum, part_sum);
	    offset += size_read;
	    seg_size -= size_read;
	}
	/* Simulate cleared memory */
	memset(buf, 0, sizeof buf);
	seg_size = BSWAP(phdr[i].p_memsz) - BSWAP(phdr[i].p_filesz);
	while (seg_size > 0) {
	    size_read = seg_size;
	    if (size_read > sizeof buf)
		size_read = sizeof buf;
	    part_sum = ipchksum(buf, size_read);
	    sum = add_ipchksums(offset, sum, part_sum);
	    offset += size_read;
	    seg_size -= size_read;
	}
    }
    if (verbose >= 2)
	printf("Computed checksum: %#04x\n", sum);

    return sum;
}

int checksum_elf(FILE *fp, const char *filename, int write_sum)
{
    int retval = 2;
    Elf_Ehdr ehdr;
    Elf_Phdr *phdr = NULL;
    off_t phoff;
    int phnum, phentsize;
    int i;
    int image_has_sum;
    unsigned short image_sum=0;
    int computed_sum;
    off_t sum_offset=0;

    if (fseek(fp, 0, SEEK_SET) != 0) {
	perror(filename);
	goto out;
    }
    if (fread(&ehdr, sizeof ehdr, 1, fp) != 1) {
	perror("Can't read ELF header");
	goto out;
    }
    if (BSWAP(ehdr.e_type) != ET_EXEC) {
	fprintf(stderr, "%s: Not executable\n", filename);
	goto out;
    }
    if (BSWAP(ehdr.e_version) != EV_CURRENT) {
	fprintf(stderr, "%s: Unsupported ELF version\n", filename);
	goto out;
    }

    phoff = BSWAP(ehdr.e_phoff);
    phnum = BSWAP(ehdr.e_phnum);
    if (phoff==0 || phnum==0) {
	fprintf(stderr, "%s: Program header not found\n", filename);
	goto out;
    }

    phentsize = BSWAP(ehdr.e_phentsize);
    if (phentsize != sizeof(*phdr)) {
	fprintf(stderr, "%s: Unsupported program header entry size\n",
		filename);
	goto out;
    }

    phdr = malloc(phnum * phentsize);
    if (!phdr) {
	perror("Can't allocate memory for program header");
	goto out;
    }
    if (fseek(fp, phoff, SEEK_SET) != 0) {
	perror("Can't seek to program header");
	goto out;
    }
    if (fread(phdr, phentsize, phnum, fp) != phnum) {
	perror("Can't read program header");
	goto out;
    }

    /* find checksum in the image */
    image_has_sum = 0;
    for (i = 0; i < phnum; i++) {
	if (BSWAP(phdr[i].p_type) == PT_NOTE) {
	    image_has_sum = process_note_segment(fp, filename,
		    BSWAP(phdr[i].p_offset),
		    BSWAP(phdr[i].p_filesz),
		    &image_sum, &sum_offset);
	    if (image_has_sum < 0) /* error */
		goto out;
	    if (image_has_sum)
		break;
	}
    }
    if (!image_has_sum) {
	fprintf(stderr, "%s: Image doesn't have ELF Boot checksum\n", filename);
	goto out;
    }

    /* See if checksum itself is summed */
    for (i = 0; i < phnum; i++) {
	if (BSWAP(phdr[i].p_type) == PT_LOAD) {
	    off_t start, end;
	    start = BSWAP(phdr[i].p_offset);
	    end = start + BSWAP(phdr[i].p_filesz);
	    if (sum_offset >= start && sum_offset < end) {
		/* It is. Computed checksum should be zero. */
		if (verbose >= 2)
		    printf("Checksum is in PT_LOAD segment, "
			    "computation result should be 0\n");
		image_sum = 0;
		break;
	    }
	}
    }

    /* OK, compute the sum */
    computed_sum = do_checksum(fp, filename, &ehdr, phdr);
    if (computed_sum < 0) /* error */
	goto out;
    
    if (write_sum && image_sum != computed_sum) {
	unsigned short target_sum;
	if (fseek(fp, sum_offset, SEEK_SET) != 0) {
	    perror("Can't seek to checksum offset");
	    goto out;
	}
	target_sum = BSWAP(computed_sum);
	if (fwrite(&target_sum, 2, 1, fp) != 1) {
	    perror("Can't write checksum");
	    goto out;
	}
	if (verbose >= 2)
	    printf("Checksum %#04x written\n", computed_sum);
	retval = 0;
    } else {
	if (image_sum == computed_sum) {
	    if (verbose)
		printf("Verified\n");
	    retval = 0;
	} else {
	    if (verbose)
		printf("Verify FAILED\n");
	    retval = 1;
	}
    }

out:
    if (phdr)
	free(phdr);
    return retval;
}
