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
#include <unistd.h>
#include <stdint.h>
#define STDINT_H
#include "elf.h"
#include "ebchecksum.h"

int verbose = 1;
typedef int (*checksum_func_t)();

checksum_func_t identify_elf(FILE *fp, const char *filename)
{
    unsigned char ident[EI_NIDENT];
    int class, data;

    rewind(fp);
    if (fread(&ident, sizeof ident, 1, fp) != 1) {
	perror("Can't read ELF identify");
	return NULL;
    }

    if (ident[EI_MAG0] != ELFMAG0
     || ident[EI_MAG1] != ELFMAG1
     || ident[EI_MAG2] != ELFMAG2
     || ident[EI_MAG3] != ELFMAG3
     || ident[EI_VERSION] != EV_CURRENT) {
	fprintf(stderr, "%s: Not an ELF file\n", filename);
	return NULL;
    }

    class = ident[EI_CLASS];
    if (class != ELFCLASS32 && class != ELFCLASS64) {
	fprintf(stderr, "%s: Unsupported ELF class: %d\n", filename, class);
	return NULL;
    }

    data = ident[EI_DATA];
    if (data != ELFDATA2LSB && data != ELFDATA2MSB) {
	fprintf(stderr, "%s: Unsupported ELF data type: %d\n", filename, data);
	return NULL;
    }

    if (class==ELFCLASS32) {
	if (data==ELFDATA2LSB)
	    return checksum_elf32le;
	else
	    return checksum_elf32be;
    } else {
	if (data==ELFDATA2LSB)
	    return checksum_elf64le;
	else
	    return checksum_elf64be;
    }
}

void usage(const char *progname)
{
    printf("Usage: %s [-w] IMAGE\n"
	    "\n"
	    "IMAGE:\tBootable ELF image file\n"
	    "-w:\tWrite checksum to the ELFBoot note in the IMAGE\n"
	    "\n"
	    "This program computes checksum of a bootable ELF image,\n"
	    "compares it against the value from the ELFBoot note,\n"
	    "and optionally writes the correct value to the ELFBoot note.\n",
	    progname);
}

int main(int argc, char *argv[])
{
    int write_checksum = 0;
    int done_opt = 0;
    const char *filename;
    int retval = 2;
    FILE *fp;
    checksum_func_t checksum_func;

    while (!done_opt) {
	switch (getopt(argc, argv, "wqv")) {
	case 'w':
	    write_checksum = 1;
	    break;
	case 'q':
	    verbose = 0;
	    break;
	case 'v':
	    verbose++;
	    break;
	case EOF:
	    done_opt = 1;
	    break;
	default:
	    usage(argv[0]);
	    return retval;
	}
    }

    if (optind != argc-1) {
	usage(argv[0]);
	return retval;
    }
    filename = argv[optind];

    fp = fopen(filename, write_checksum? "r+" : "r");
    if (!fp) {
	perror(filename);
	goto out;
    }
    checksum_func = identify_elf(fp, filename);
    if (checksum_func == NULL)
	goto out;

    retval = checksum_func(fp, filename, write_checksum);

out:
    if (fp)
	fclose(fp);
    return retval;
}
