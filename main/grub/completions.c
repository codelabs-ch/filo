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

static int do_completion;
static int unique;
static char *unique_string;


/* If DO_COMPLETION is true, just print NAME. Otherwise save the unique
   part into UNIQUE_STRING.  */
void print_a_completion(char *name)
{
	/* If NAME is "." or "..", do not count it.  */
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return;

	if (do_completion) {
		char *buf = unique_string;

		if (!unique)
			while ((*buf++ = *name++));
		else {
			while (*buf && (*buf == *name)) {
				buf++;
				name++;
			}
			/* mismatch, strip it.  */
			*buf = '\0';
		}
	} else
		grub_printf(" %s", name);

	unique++;
}

/*
 *  This lists the possible completions of a device string, filename, or
 *  any sane combination of the two.
 */

int print_completions(int is_filename, int is_completion)
{
#if CONFIG_EXPERIMENTAL
	char *buf = (char *) COMPLETION_BUF;
	char *ptr = buf;

	unique_string = (char *) UNIQUE_BUF;
	*unique_string = 0;
	unique = 0;
	do_completion = is_completion;

#warning FIXME implement print_completions
	// FIXME: This function is a dummy, returning an error.
	errnum = ERR_BAD_FILENAME;


	print_error();
	do_completion = 0;
	if (errnum)
		return -1;
	else
		return unique - 1;
#else
	errnum = ERR_BAD_FILENAME;
	print_error();
	return -1;
#endif
}
