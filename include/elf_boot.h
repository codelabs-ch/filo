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

#ifndef ELF_BOOT_H
#define ELF_BOOT_H

/* This defines the structure of a table of parameters useful for ELF
 * bootable images.  These parameters are all passed and generated
 * by the bootloader to the booted image.  For simplicity and
 * consistency the Elf Note format is reused.
 *
 * All of the information must be Position Independent Data.
 * That is it must be safe to relocate the whole ELF boot parameter
 * block without changing the meaning or correctness of the data.
 * Additionally it must be safe to permute the order of the ELF notes
 * to any possible permutation without changing the meaning or correctness
 * of the data.
 *
 */

#define ELF_BHDR_MAGIC		0x0E1FB007

#ifndef __ASSEMBLY__
typedef uint16_t Elf_Half;
typedef uint32_t Elf_Word;

/*
 * Elf boot notes...
 */

typedef struct Elf_Bhdr
{
	Elf_Word b_signature; /* "0x0E1FB007" */
	Elf_Word b_size;
	Elf_Half b_checksum;
	Elf_Half b_records;
} Elf_Bhdr;

/*
 * ELF Notes.
 */

typedef struct Elf_Nhdr
{
	Elf_Word n_namesz;		/* Length of the note's name.  */
	Elf_Word n_descsz;		/* Length of the note's descriptor.  */
	Elf_Word n_type;		/* Type of the note.  */
} Elf_Nhdr;

#endif /* __ASSEMBLY__ */

/* Standardized Elf image notes for booting... The name for all of these is ELFBoot */
#define ELF_NOTE_BOOT		"ELFBoot"

#define EIN_PROGRAM_NAME	0x00000001
/* The program in this ELF file */
#define EIN_PROGRAM_VERSION	0x00000002
/* The version of the program in this ELF file */
#define EIN_PROGRAM_CHECKSUM	0x00000003
/* ip style checksum of the memory image. */

/* Linux image notes for booting... The name for all of these is Linux */

#define LIN_COMMAND_LINE	0x00000001
/* The command line to pass to the loaded kernel. */
#define LIN_ROOT_DEV		0x00000002
/* The root dev to pass to the loaded kernel. */
#define LIN_RAMDISK_FLAGS	0x00000003
/* Various old ramdisk flags */
#define LIN_INITRD_START	0x00000004
/* Start of the ramdisk in bytes */
#define LIN_INITRD_SIZE		0x00000005
/* Size of the ramdisk in bytes */

/* Notes that are passed to a loaded image */
/* For the standard elf boot notes n_namesz must be zero */
#define EBN_FIRMWARE_TYPE	0x00000001
/* ASCIZ name of the platform firmware. */
#define EBN_BOOTLOADER_NAME	0x00000002
/* This specifies just the ASCIZ name of the bootloader */
#define EBN_BOOTLOADER_VERSION	0x00000003
/* This specifies the version of the bootloader as an ASCIZ string */
#define EBN_COMMAND_LINE	0x00000004
/* This specifies a command line that can be set by user interaction,
 * and is provided as a free form ASCIZ string to the loaded image.
 */
#define EBN_NOP			0x00000005
/* A note nop note has no meaning, useful for inserting explicit padding */
#define EBN_LOADED_IMAGE	0x00000006
/* An ASCIZ string naming the loaded image */

/* Etherboot specific notes */
#define EB_PARAM_NOTE		"Etherboot"
#define EB_IA64_SYSTAB		0x00000001
#define EB_IA64_MEMMAP		0x00000002
#define EB_IA64_FPSWA		0x00000003
#define EB_IA64_CONINFO		0x00000004
#define EB_BOOTP_DATA		0x00000005
#define EB_HEADER		0x00000006
#define EB_IA64_IMAGE_HANDLE	0x00000007
#define EB_I386_MEMMAP		0x00000008

#endif /* ELF_BOOT_H */
