/* cmdline.c - the device-independent GRUB text command line */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2004  Free Software Foundation, Inc.
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

/* Find the next word from CMDLINE and return the pointer. If
 * AFTER_EQUAL is non-zero, assume that the character `=' is treated as
 * a space. Caution: this assumption is for backward compatibility.
 */
char *skip_to(int after_equal, char *cmdline)
{
	/* Skip until we hit whitespace, or maybe an equal sign. */
	while (*cmdline && *cmdline != ' ' && *cmdline != '\t' && !(after_equal && *cmdline == '='))
		cmdline++;

	/* Skip whitespace, and maybe equal signs. */
	while (*cmdline == ' ' || *cmdline == '\t' || (after_equal && *cmdline == '='))
		cmdline++;

	return cmdline;
}

#ifndef CONFIG_NON_INTERACTIVE
/* Print a helpful message for the command-line interface.  */
void print_cmdline_message(int type)
{
#if 0
	// We don't have file completion (yet?)
	grub_printf
	    (" [ Minimal BASH-like line editing is supported.  For the first word, TAB\n"
	     "   lists possible command completions.  Anywhere else TAB lists the possible\n"
	     "   completions of a device/filename.");
#else
	grub_printf
	    (" [ Minimal BASH-like line editing is supported.  For the first word, TAB\n"
	     "   lists possible command completions.");
#endif

	if (type == CMDLINE_NORMAL_MODE)
		grub_printf("  ESC at any time exits.");

	if (type == CMDLINE_EDIT_MODE)
		grub_printf("  ESC at any time cancels.  ENTER \n" "   at any time accepts your changes.");

	grub_printf("]\n");

#ifndef CONFIG_NEWLINE_BEFORE_EACH_PROMPT
	grub_printf("\n");
#endif
}
#endif /* CONFIG_NON_INTERACTIVE */

/* Find the builtin whose command name is COMMAND and return the
 * pointer. If not found, return 0.
 */
struct builtin *find_command(char *command)
{
	char *ptr;
	char c;
	struct builtin **builtin;

	/* Find the first space and terminate the command name.  */
	ptr = command;
	while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '=')
		ptr++;

	c = *ptr;
	*ptr = 0;

	/* Seek out the builtin whose command name is COMMAND.  */
	for (builtin = builtin_table; *builtin != 0; builtin++) {
		int ret = strcmp(command, (*builtin)->name);

		if (ret == 0) {
			/* Find the builtin for COMMAND.  */
			*ptr = c;
			return *builtin;
		} else if (ret < 0)
			break;
	}

	/* Cannot find COMMAND.  */
	errnum = ERR_UNRECOGNIZED;
	*ptr = c;
	return 0;
}

int keep_cmdline_running;

/* Initialize the data for the command-line.  */
static void init_cmdline(void)
{
	/* Initialization.  */
	errnum = 0;
	count_lines = -1;

	keep_cmdline_running = 1;

	/* Initialize the data for the builtin commands.  */
	init_builtins();
}

#ifndef CONFIG_NON_INTERACTIVE
/* Enter the command-line interface. HEAP is used for the command-line
 * buffer. Return only if FOREVER is nonzero and get_cmdline returns
 * nonzero (ESC is pushed).
 */
void enter_cmdline(char *heap, int forever)
{
	/* Initialize the data and print a message. */
	init_cmdline();

	init_page();

	print_cmdline_message(forever ? CMDLINE_FOREVER_MODE : CMDLINE_NORMAL_MODE);

	while (keep_cmdline_running) {
		struct builtin *builtin;
		char *arg;

		*heap = 0;
		print_error();
		errnum = ERR_NONE;

		short col1, col2, col3, col4;
		pair_content(1, &col1, &col2);
		pair_content(2, &col3, &col4);
		/* reset to light-gray-on-black on console */
		console_setcolor(0x07, 0x70);

		/* Get the command-line with the minimal BASH-like interface. */
		if (get_cmdline(CONFIG_PROMPT "> ", heap, 2048, 0, 1)) {
			init_pair(1, col1, col2);
			init_pair(2, col3, col4);
			return;
		}

		/* If there was no command, grab a new one. */
		if (!heap[0])
			continue;

		/* Find a builtin. */
		builtin = find_command(heap);
		if (!builtin)
			continue;

		/* If BUILTIN cannot be run in the command-line, skip it. */
		if (!(builtin->flags & BUILTIN_CMDLINE)) {
			errnum = ERR_UNRECOGNIZED;
			continue;
		}

		/* Start to count lines, only if the internal pager is in use. */
		if (use_pager)
			count_lines = 0;

		/* Run BUILTIN->FUNC.  */
		arg = skip_to(1, heap);
		(builtin->func) (arg, BUILTIN_CMDLINE);

		/* Finish the line count.  */
		count_lines = -1;
	}
}
#endif /* CONFIG_NON_INTERACTIVE */

/* Run an entry from the script SCRIPT. HEAP is used for the
   command-line buffer. If an error occurs, return non-zero, otherwise
   return zero.  */
int run_script(char *script, char *heap)
{
	char *old_entry;
	char *cur_entry = script;

	/* Initialize the data.  */
	init_cmdline();

	while (1) {
		struct builtin *builtin;
		char *arg;

		print_error();

		if (errnum) {
			errnum = ERR_NONE;

			/* If a fallback entry is defined, don't prompt a user's
			   intervention.  */
			if (fallback_entryno < 0) {
				grub_printf("\nPress any key to continue...");
				(void)getkey();
			}

			return 1;
		}

		/* Copy the first string in CUR_ENTRY to HEAP.  */
		old_entry = cur_entry;
		while (*cur_entry++);

		memmove(heap, old_entry, (int) cur_entry - (int) old_entry);
		if (!*heap) {
			/* If there is no more command in SCRIPT...  */

			/* If any kernel is not loaded, just exit successfully.  */
			if (kernel_type == KERNEL_TYPE_NONE)
				return 0;

			/* Otherwise, the command boot is run implicitly.  */
			memmove(heap, "boot", 5);
		}

		/* Find a builtin.  */
		builtin = find_command(heap);
		if (!builtin) {
			grub_printf("%s\n", old_entry);
			continue;
		}

		if (!(builtin->flags & BUILTIN_NO_ECHO))
			grub_printf("%s\n", old_entry);

		/* If BUILTIN cannot be run in the command-line, skip it. */
		if (!(builtin->flags & BUILTIN_CMDLINE)) {
			errnum = ERR_UNRECOGNIZED;
			continue;
		}

		/* Run BUILTIN->FUNC.  */
		arg = skip_to(1, heap);
		(builtin->func) (arg, BUILTIN_SCRIPT);
	}
}
