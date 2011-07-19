/* Parts are : */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004  Free Software Foundation, Inc.
 *  Copyright (C) 2005-2008 coresystems GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libpayload.h>
#include <config.h>
#include <grub/shared.h>
#define current_slice 0

static int do_completion;
static int unique;
static char *unique_string;

static int incomplete, disk_choice;
static enum
{
	PART_UNSPECIFIED = 0,
	PART_DISK,
	PART_CHOSEN,
} part_choice;

int
real_open_partition (int flags)
{
	errnum = ERR_NONE;
	return 1;
}

int
open_partition (void)
{
	return real_open_partition (0);
}

char *
set_device (char *device)
{
	int result = 0;

	if (result) {
		return device + 1;
	} else {
		if (!*device)
			incomplete = 1;
		errnum = ERR_DEV_FORMAT;
	}
	return 0;
}

/*
 *  This lists the possible completions of a device string, filename, or
 *  any sane combination of the two.
 */

int print_completions(int is_filename, int is_completion)
{
	char *buf = (char *) COMPLETION_BUF;
	char *ptr = buf;

	unique_string = (char *) UNIQUE_BUF;
	*unique_string = 0;
	unique = 0;
	do_completion = is_completion;

	if (!is_filename) {
		/* Print the completions of builtin commands.  */
		struct builtin **builtin;

		if (!is_completion)
			grub_printf (" Possible commands are:");

		for (builtin = builtin_table; (*builtin); builtin++) {
			/* If *builtin cannot be run in the command-line, skip it. */
			if (!((*builtin)->flags & BUILTIN_CMDLINE))
				continue;
			if (substring (buf, (*builtin)->name) <= 0)
				print_a_completion ((*builtin)->name);
		}

		if (is_completion && *unique_string) {
			if (unique == 1) {
				char *u = unique_string + strlen (unique_string);
				*u++ = ' ';
				*u = 0;
			}
			strcpy (buf, unique_string);
		}

		if (!is_completion)
			grub_putchar ('\n');

		print_error();
		do_completion = 0;
		if (errnum)
			return -1;
		else
			return unique - 1;
	}

	if (*buf == '/' || (ptr = set_device (buf)) || incomplete) {
		errnum = 0;
		if (*buf == '(' && (incomplete || ! *ptr)) {
			if (!part_choice) {
				/* disk completions */
				int disk_no, i, j;

				if (!is_completion)
					grub_printf (" Possible disks are: ");

				if (!ptr
					|| *(ptr-1) != 'd'
					|| *(ptr-2) != 'n' /* netboot? */
					|| *(ptr-2) != 'c') {
					for (i = (ptr && (*(ptr-1) == 'd' && *(ptr-2) == 'h') ? 1:0);
					     i < (ptr && (*(ptr-1) == 'd' && *(ptr-2) == 'f') ?  1:2);
					     i++) {
						for (j = 0; j < 8; j++) {
							if ((disk_choice)) { // TODO check geometry
								char dev_name[8];
								sprintf (dev_name, "%cd%d", i ?  'h':'f', j);
								print_a_completion(dev_name);
							}
						}
					}
				}

#if 0
				if (cdrom_drive != GRUB_INVALID_DRIVE
				    && (disk_choice || cdrom_drive == current_drive)
				    && (!ptr
					|| *(ptr-1) == '('
					|| (*(ptr-1) == 'd' && *(ptr-2) == 'c')))
					print_a_completion("cd");
#endif

				if (is_completion && *unique_string) {
					ptr = buf;
					while (*ptr != '(')
						ptr--;
					ptr++;
					strcpy (ptr, unique_string);
					if (unique == 1) {
						ptr += strlen (ptr);
						if (*unique_string == 'h') {
							*ptr++ = ',';
							*ptr = 0;
						} else {
							*ptr++ = ')';
							*ptr = 0;
						}
					}
				}

				if (!is_completion)
					grub_putchar('\n');
			} else {
				/* Partition completions */
				if (part_choice == PART_CHOSEN
				    && open_partition()
				    && ! IS_PC_SLICE_TYPE_BSD(current_slice)) {
					unique = 1;
					ptr = buf + strlen(buf);
					if (*(ptr - 1) != ')') {
						*ptr++ = ')';
						*ptr = 0;
					}
				} else {
					if (!is_completion)
						grub_printf (" Possible partitions are:\n");
					real_open_partition(1);
					if (is_completion && *unique_string) {
						ptr = buf;
						while (*ptr++ != ',')
							;
						strcpy (ptr, unique_string);
					}
				}
			}
		} else if (ptr && *ptr == '/') {
			/* filename completions */
			if (!is_completion)
				grub_printf (" Possible files are:");
			dir(buf);

			if (is_completion && *unique_string) {
				ptr += strlen (ptr);
				while (*ptr != '/')
					ptr--;
				ptr++;

				strcpy(ptr, unique_string);

				if (unique == 1) {
					ptr += strlen (unique_string);

					/* Check if the file UNIQUE_STRING is a directory.  */
					*ptr = '/';
					*(ptr + 1) = 0;

					dir (buf);

					/* Restore the original unique value. */
					unique = 1;

					if (errnum) {
						/* regular file */
						errnum = 0;
						*ptr = ' ';
						*(ptr + 1) = 0;
					}
				}
			}

			if (!is_completion)
				grub_putchar ('\n');

		} else {
			errnum = ERR_BAD_FILENAME;
		}
	}

	print_error();
	do_completion = 0;
	if (errnum)
		return -1;
	else
		return unique - 1;
}
