/* char_io.c - basic console input and output */
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
#include <version.h>
#include <grub/shared.h>

char *err_list[] = {
	[ERR_NONE] = 0,
	[ERR_BAD_ARGUMENT] = "Invalid argument",
	[ERR_BAD_FILENAME] = "Filename must be either an absolute pathname or blocklist",
	[ERR_BAD_FILETYPE] = "Bad file or directory type",
	[ERR_BAD_GZIP_DATA] = "Bad or corrupt data while decompressing file",
	[ERR_BAD_GZIP_HEADER] = "Bad or incompatible header in compressed file",
	[ERR_BAD_PART_TABLE] = "Partition table invalid or corrupt",
	[ERR_BAD_VERSION] = "Mismatched or corrupt version of stage1/stage2",
	[ERR_BELOW_1MB] = "Loading below 1MB is not supported",
	[ERR_BOOT_COMMAND] = "Kernel must be loaded before booting",
	[ERR_BOOT_FAILURE] = "Unknown boot failure",
	[ERR_BOOT_FEATURES] = "Unsupported Multiboot features requested",
	[ERR_DEV_FORMAT] = "Unrecognized device string",
	[ERR_DEV_NEED_INIT] = "Device not initialized yet",
	[ERR_DEV_VALUES] = "Invalid device requested",
	[ERR_EXEC_FORMAT] = "Invalid or unsupported executable format",
	[ERR_FILELENGTH] = "Filesystem compatibility error, cannot read whole file",
	[ERR_FILE_NOT_FOUND] = "File not found",
	[ERR_FSYS_CORRUPT] = "Inconsistent filesystem structure",
	[ERR_FSYS_MOUNT] = "Cannot mount selected partition",
	[ERR_GEOM] = "Selected cylinder exceeds maximum supported by BIOS",
	[ERR_NEED_LX_KERNEL] = "Linux kernel must be loaded before initrd",
	[ERR_NEED_MB_KERNEL] = "Multiboot kernel must be loaded before modules",
	[ERR_NO_DISK] = "Selected disk does not exist",
	[ERR_NO_DISK_SPACE] = "No spare sectors on the disk",
	[ERR_NO_PART] = "No such partition",
	[ERR_NUMBER_OVERFLOW] = "Overflow while parsing number",
	[ERR_NUMBER_PARSING] = "Error while parsing number",
	[ERR_OUTSIDE_PART] = "Attempt to access block outside partition",
	[ERR_PRIVILEGED] = "Must be authenticated",
	[ERR_READ] = "Disk read error",
	[ERR_SYMLINK_LOOP] = "Too many symbolic links",
	[ERR_UNALIGNED] = "File is not sector aligned",
	[ERR_UNRECOGNIZED] = "Unrecognized command",
	[ERR_WONT_FIT] = "Selected item cannot fit into memory",
	[ERR_WRITE] = "Disk write error",
};

int max_lines = 24;
int count_lines = -1;
int use_pager = 1;

void print_error(void)
{
	if (errnum > ERR_NONE && errnum < MAX_ERR_NUM)
		grub_printf("\nError %u: %s\n", errnum, err_list[errnum]);
}

char *convert_to_ascii(char *buf, int c, ...)
{
	unsigned long num = *((&c) + 1), mult = 10;
	char *ptr = buf;

	if (c == 'x' || c == 'X')
		mult = 16;

	if ((num & 0x80000000uL) && c == 'd') {
		num = (~num) + 1;
		*(ptr++) = '-';
		buf++;
	}

	do {
		int dig = num % mult;
		*(ptr++) = ((dig > 9) ? dig + 'a' - 10 : '0' + dig);
	}
	while (num /= mult);

	/* reorder to correct direction!! */
	{
		char *ptr1 = ptr - 1;
		char *ptr2 = buf;
		while (ptr1 > ptr2) {
			int tmp = *ptr1;
			*ptr1 = *ptr2;
			*ptr2 = tmp;
			ptr1--;
			ptr2++;
		}
	}

	return ptr;
}

void grub_putstr(const char *str)
{
	while (*str)
		grub_putchar(*str++);
}

void grub_printf(const char *format, ...)
{
	int *dataptr = (int *) &format;
	char c, str[16];

	dataptr++;

	while ((c = *(format++)) != 0) {
		if (c != '%')
			grub_putchar(c);
		else
			switch (c = *(format++)) {
			case 'd':
			case 'x':
			case 'X':
			case 'u':
				*convert_to_ascii(str, c, *((unsigned long *)
							    dataptr++)) = 0;
				grub_putstr(str);
				break;

			case 'c':
				grub_putchar((*(dataptr++)) & 0xff);
				break;

			case 's':
				grub_putstr((char *) *(dataptr++));
				break;
			}
	}
	refresh();
}

void init_page(void)
{
	cls();
	grub_printf("\n                                  %s %s\n\n", 
			PROGRAM_NAME, PROGRAM_VERSION);
}

/* The number of the history entries.  */
static int num_history = 0;

/* Get the NOth history. If NO is less than zero or greater than or
   equal to NUM_HISTORY, return NULL. Otherwise return a valid string.  */
static char *get_history(int no)
{
	if (no < 0 || no >= num_history)
		return 0;

	return (char *) HISTORY_BUF + MAX_CMDLINE * no;
}

/* Add CMDLINE to the history buffer.  */
static void add_history(const char *cmdline, int no)
{
	memmove((char *) HISTORY_BUF + MAX_CMDLINE * (no + 1),
		     (char *) HISTORY_BUF + MAX_CMDLINE * no, MAX_CMDLINE * (num_history - no));
	strcpy((char *) HISTORY_BUF + MAX_CMDLINE * no, cmdline);
	if (num_history < HISTORY_SIZE)
		num_history++;
}

static int real_get_cmdline(char *prompt, char *cmdline, int maxlen, int echo_char, int readline)
{
	/* This is a rather complicated function. So explain the concept.

	   A command-line consists of ``section''s. A section is a part of the
	   line which may be displayed on the screen, but a section is never
	   displayed with another section simultaneously.

	   Each section is basically 77 or less characters, but the exception
	   is the first section, which is 78 or less characters, because the
	   starting point is special. See below.

	   The first section contains a prompt and a command-line (or the
	   first part of a command-line when it is too long to be fit in the
	   screen). So, in the first section, the number of command-line
	   characters displayed is 78 minus the length of the prompt (or
	   less). If the command-line has more characters, `>' is put at the
	   position 78 (zero-origin), to inform the user of the hidden
	   characters.

	   Other sections always have `<' at the first position, since there
	   is absolutely a section before each section. If there is a section
	   after another section, this section consists of 77 characters and
	   `>' at the last position. The last section has 77 or less
	   characters and doesn't have `>'.

	   Each section other than the last shares some characters with the
	   previous section. This region is called ``margin''. If the cursor
	   is put at the magin which is shared by the first section and the
	   second, the first section is displayed. Otherwise, a displayed
	   section is switched to another section, only if the cursor is put
	   outside that section.  */

	/* XXX: These should be defined in shared.h, but I leave these here,
	   until this code is freezed.  */
#define CMDLINE_WIDTH	78
#define CMDLINE_MARGIN	10

	int xpos, lpos, c, section;
	/* The length of PROMPT.  */
	int plen;
	/* The length of the command-line.  */
	int llen;
	/* The index for the history.  */
	int history = -1;
	/* The working buffer for the command-line.  */
	char *buf = (char *) CMDLINE_BUF;
	/* The kill buffer.  */
	char *kill_buf = (char *) KILL_BUF;

	/* Nested function definitions for code simplicity.  */

	/* The forward declarations of nested functions are prefixed
	 * with `auto'. */
	auto void cl_refresh(int full, int len);
	auto void cl_backward(int count);
	auto void cl_forward(int count);
	auto void cl_insert(const char *str);
	auto void cl_delete(int count);
	auto void cl_init(void);

	/* Move the cursor backward.  */
	void cl_backward(int count) {
		lpos -= count;

		/* If the cursor is in the first section, display the first section
		   instead of the second.  */
		if (section == 1 && plen + lpos < CMDLINE_WIDTH)
			cl_refresh(1, 0);
		else if (xpos - count < 1)
			cl_refresh(1, 0);
		else {
			xpos -= count;

			gotoxy(xpos, getxy() & 0xFF);
		}
		refresh();
	}

	/* Move the cursor forward.  */
	void cl_forward(int count) {
		lpos += count;

		/* If the cursor goes outside, scroll the screen to the right.  */
		if (xpos + count >= CMDLINE_WIDTH)
			cl_refresh(1, 0);
		else {
			xpos += count;

			gotoxy(xpos, getxy() & 0xFF);
		}
		refresh();
	}

	/* Refresh the screen. If FULL is true, redraw the full line, otherwise,
	   only LEN characters from LPOS.  */
	void cl_refresh(int full, int len) {
		int i;
		int start;
		int pos = xpos;

		if (full) {
			/* Recompute the section number.  */
			if (lpos + plen < CMDLINE_WIDTH)
				section = 0;
			else
				section = ((lpos + plen - CMDLINE_WIDTH)
					   / (CMDLINE_WIDTH - 1 - CMDLINE_MARGIN) + 1);

			/* From the start to the end.  */
			len = CMDLINE_WIDTH;
			pos = 0;
			grub_putchar('\r');

			/* If SECTION is the first section, print the prompt, otherwise,
			   print `<'.  */
			if (section == 0) {
				grub_printf("%s", prompt);
				len -= plen;
				pos += plen;
			} else {
				grub_putchar('<');
				len--;
				pos++;
			}
		}

		/* Compute the index to start writing BUF and the resulting position
		   on the screen.  */
		if (section == 0) {
			int offset = 0;

			if (!full)
				offset = xpos - plen;

			start = 0;
			xpos = lpos + plen;
			start += offset;
		} else {
			int offset = 0;

			if (!full)
				offset = xpos - 1;

			start = ((section - 1) * (CMDLINE_WIDTH - 1 - CMDLINE_MARGIN)
				 + CMDLINE_WIDTH - plen - CMDLINE_MARGIN);
			xpos = lpos + 1 - start;
			start += offset;
		}

		/* Print BUF. If ECHO_CHAR is not zero, put it instead.  */
		for (i = start; i < start + len && i < llen; i++) {
			if (!echo_char)
				grub_putchar(buf[i]);
			else
				grub_putchar(echo_char);

			pos++;
		}

		/* Fill up the rest of the line with spaces.  */
		for (; i < start + len; i++) {
			grub_putchar(' ');
			pos++;
		}
		/* If the cursor is at the last position, put `>' or a space,
		   depending on if there are more characters in BUF.  */
		if (pos == CMDLINE_WIDTH) {
			if (start + len < llen)
				grub_putchar('>');
			else
				grub_putchar(' ');

			pos++;
		}

		/* Back to XPOS.  */
		gotoxy(xpos, getxy() & 0xFF);

		refresh();
	}

	/* Initialize the command-line.  */
	void cl_init(void) {
#ifdef CONFIG_NEWLINE_BEFORE_EACH_PROMPT
		/* Distinguish us from other lines and error messages!  */
		grub_putchar('\n');
#endif

		/* Print full line and set position here.  */
		cl_refresh(1, 0);
	}

	/* Insert STR to BUF.  */
	void cl_insert(const char *str) {
		int l = strlen(str);

		if (llen + l < maxlen) {
			if (lpos == llen)
				memmove(buf + lpos, str, l + 1);
			else {
				memmove(buf + lpos + l, buf + lpos, llen - lpos + 1);
				memmove(buf + lpos, str, l);
			}

			llen += l;
			lpos += l;
			if (xpos + l >= CMDLINE_WIDTH)
				cl_refresh(1, 0);
			else if (xpos + l + llen - lpos > CMDLINE_WIDTH)
				cl_refresh(0, CMDLINE_WIDTH - xpos);
			else
				cl_refresh(0, l + llen - lpos);
		}
	}

	/* Delete COUNT characters in BUF.  */
	void cl_delete(int count) {
		memmove(buf + lpos, buf + lpos + count, llen - count + 1);
		llen -= count;

		if (xpos + llen + count - lpos > CMDLINE_WIDTH)
			cl_refresh(0, CMDLINE_WIDTH - xpos);
		else
			cl_refresh(0, llen + count - lpos);
	}

	plen = strlen(prompt);
	llen = strlen(cmdline);

	if (maxlen > MAX_CMDLINE) {
		maxlen = MAX_CMDLINE;
		if (llen >= MAX_CMDLINE) {
			llen = MAX_CMDLINE - 1;
			cmdline[MAX_CMDLINE] = 0;
		}
	}
	lpos = llen;
	strcpy(buf, cmdline);

	cl_init();

	while ((c = ASCII_CHAR(getkey())) != '\n' && c != '\r') {
		/* If READLINE is non-zero, handle readline-like key bindings.  */
		if (readline) {
			switch (c) {
			case 9:	/* TAB lists completions */
				{
					int i;
					/* POS points to the first space after a command.  */
					int pos = 0;
					int ret;
					char *completion_buffer = (char *) COMPLETION_BUF;
					int equal_pos = -1;
					int is_filename;

					/* Find the first word.  */
					while (buf[pos] == ' ')
						pos++;
					while (buf[pos] && buf[pos] != '=' && buf[pos] != ' ')
						pos++;

					is_filename = (lpos > pos);

					/* Find the position of the equal character after a
					   command, and replace it with a space.  */
					for (i = pos; buf[i] && buf[i] != ' '; i++)
						if (buf[i] == '=') {
							equal_pos = i;
							buf[i] = ' ';
							break;
						}

					/* Find the position of the first character in this
					   word.  */
					for (i = lpos; i > 0 && buf[i - 1] != ' '; i--);

					/* Copy this word to COMPLETION_BUFFER and do the
					   completion.  */
					memmove(completion_buffer, buf + i, lpos - i);
					completion_buffer[lpos - i] = 0;
					ret = print_completions(is_filename, 1);
					errnum = ERR_NONE;

					if (ret >= 0) {
						/* Found, so insert COMPLETION_BUFFER.  */
						cl_insert(completion_buffer + lpos - i);

						if (ret > 0) {
							/* There are more than one candidates, so print
							   the list.  */
							grub_putchar('\n');
							print_completions(is_filename, 0);
							errnum = ERR_NONE;
						}
					}

					/* Restore the command-line.  */
					if (equal_pos >= 0)
						buf[equal_pos] = '=';

					if (ret)
						cl_init();
				}

				break;
			case 1:	/* C-a go to beginning of line */
				cl_backward(lpos);
				break;
			case 5:	/* C-e go to end of line */
				cl_forward(llen - lpos);
				break;
			case 6:	/* C-f forward one character */
				if (lpos < llen)
					cl_forward(1);
				break;
			case 2:	/* C-b backward one character */
				if (lpos > 0)
					cl_backward(1);
				break;
			case 21:	/* C-u kill to beginning of line */
				if (lpos == 0)
					break;
				/* Copy the string being deleted to KILL_BUF.  */
				memmove(kill_buf, buf, lpos);
				kill_buf[lpos] = 0;
				{
					/* XXX: Not very clever.  */

					int count = lpos;

					cl_backward(lpos);
					cl_delete(count);
				}
				break;
			case 11:	/* C-k kill to end of line */
				if (lpos == llen)
					break;
				/* Copy the string being deleted to KILL_BUF.  */
				memmove(kill_buf, buf + lpos, llen - lpos + 1);
				cl_delete(llen - lpos);
				break;
			case 25:	/* C-y yank the kill buffer */
				cl_insert(kill_buf);
				break;
			case 16:	/* C-p fetch the previous command */
				{
					char *p;

					if (history < 0)
						/* Save the working buffer.  */
						strcpy(cmdline, buf);
					else if (strcmp(get_history(history), buf) != 0)
						/* If BUF is modified, add it into the history list.  */
						add_history(buf, history);

					history++;
					p = get_history(history);
					if (!p) {
						history--;
						break;
					}

					strcpy(buf, p);
					llen = strlen(buf);
					lpos = llen;
					cl_refresh(1, 0);
				}
				break;
			case 14:	/* C-n fetch the next command */
				{
					char *p;

					if (history < 0) {
						break;
					} else if (strcmp(get_history(history), buf) != 0)
						/* If BUF is modified, add it into the history list.  */
						add_history(buf, history);

					history--;
					p = get_history(history);
					if (!p)
						p = cmdline;

					strcpy(buf, p);
					llen = strlen(buf);
					lpos = llen;
					cl_refresh(1, 0);
				}
				break;
			}
		}

		/* ESC, C-d and C-h are always handled. Actually C-d is not
		   functional if READLINE is zero, as the cursor cannot go
		   backward, but that's ok.  */
		switch (c) {
		case 27:	/* ESC immediately return 1 */
			return 1;
		case 4:	/* C-d delete character under cursor */
			if (lpos == llen)
				break;
			cl_delete(1);
			break;
		case 8:	/* C-h backspace */
		case 127:	/* also backspace */
			if (lpos > 0) {
				cl_backward(1);
				cl_delete(1);
			}
			break;
		default:	/* insert printable character into line */
			if (c >= ' ' && c <= '~') {
				char str[2];

				str[0] = c;
				str[1] = 0;
				cl_insert(str);
			}
		}
	}

	grub_putchar('\n');

	/* If ECHO_CHAR is NUL, remove the leading spaces.  */
	lpos = 0;
	if (!echo_char)
		while (buf[lpos] == ' ')
			lpos++;

	/* Copy the working buffer to CMDLINE.  */
	memmove(cmdline, buf + lpos, llen - lpos + 1);

	/* If the readline-like feature is turned on and CMDLINE is not
	   empty, add it into the history list.  */
	if (readline && lpos < llen)
		add_history(cmdline, 0);

	refresh();

	return 0;
}

/* Don't use this with a MAXLEN greater than 1600 or so!  The problem
   is that GET_CMDLINE depends on the everything fitting on the screen
   at once.  So, the whole screen is about 2000 characters, minus the
   PROMPT, and space for error and status lines, etc.  MAXLEN must be
   at least 1, and PROMPT and CMDLINE must be valid strings (not NULL
   or zero-length).

   If ECHO_CHAR is nonzero, echo it instead of the typed character. */
int get_cmdline(char *prompt, char *cmdline, int maxlen, int echo_char, int readline)
{
	int old_cursor;
	int ret;

	old_cursor = setcursor(1);

	/* Because it is hard to deal with different conditions simultaneously,
	   less functional cases are handled here. Assume that TERM_NO_ECHO
	   implies TERM_NO_EDIT.  */
	if (terminal_flags & (TERM_NO_ECHO | TERM_NO_EDIT)) {
		char *p = cmdline;
		int c;

		/* Make sure that MAXLEN is not too large.  */
		if (maxlen > MAX_CMDLINE)
			maxlen = MAX_CMDLINE;

		/* Print only the prompt. The contents of CMDLINE is simply discarded,
		   even if it is not empty.  */
		grub_printf("%s", prompt);

		/* Gather characters until a newline is gotten.  */
		while ((c = ASCII_CHAR(getkey())) != '\n' && c != '\r') {
			/* Return immediately if ESC is pressed.  */
			if (c == 27) {
				setcursor(old_cursor);
				return 1;
			}

			/* Printable characters are added into CMDLINE.  */
			if (c >= ' ' && c <= '~') {
				if (!(terminal_flags & TERM_NO_ECHO))
					grub_putchar(c);

				/* Preceding space characters must be ignored.  */
				if (c != ' ' || p != cmdline)
					*p++ = c;
			}
		}

		*p = 0;

		if (!(terminal_flags & TERM_NO_ECHO))
			grub_putchar('\n');

		setcursor(old_cursor);
		refresh();
		return 0;
	}

	/* Complicated features are left to real_get_cmdline.  */
	ret = real_get_cmdline(prompt, cmdline, maxlen, echo_char, readline);
	setcursor(old_cursor);
	refresh();
	return ret;
}

int safe_parse_maxint(char **str_ptr, int *myint_ptr)
{
	char *ptr = *str_ptr;
	int myint = 0;
	int mult = 10, found = 0;

	/*
	 *  Is this a hex number?
	 */
	if (*ptr == '0' && tolower(*(ptr + 1)) == 'x') {
		ptr += 2;
		mult = 16;
	}

	while (1) {
		/* A bit tricky. This below makes use of the equivalence:
		   (A >= B && A <= C) <=> ((A - B) <= (C - B))
		   when C > B and A is unsigned.  */
		unsigned int digit;

		digit = tolower(*ptr) - '0';
		if (digit > 9) {
			digit -= 'a' - '0';
			if (mult == 10 || digit > 5)
				break;
			digit += 10;
		}

		found = 1;
		if (myint > ((MAXINT - digit) / mult)) {
			errnum = ERR_NUMBER_OVERFLOW;
			return 0;
		}
		myint = (myint * mult) + digit;
		ptr++;
	}

	if (!found) {
		errnum = ERR_NUMBER_PARSING;
		return 0;
	}

	*str_ptr = ptr;
	*myint_ptr = myint;

	return 1;
}

int terminal_flags;

static color_state console_color_state = COLOR_STATE_STANDARD;

void console_setcolorstate(color_state state)
{
	console_color_state = state;
}

void console_setcolor(int normal_color, int highlight_color)
{
	init_pair(1, normal_color & 0xf, (normal_color >> 4) & 0xf);
	init_pair(2, highlight_color & 0xf, (highlight_color >> 4) & 0xf);

	/* Make curses update the whole screen */
	redrawwin(stdscr);
	refresh();
}

/* The store for ungetch simulation. This is necessary, because
   ncurses-1.9.9g is still used in the world and its ungetch is
   completely broken.  */
static int save_char = ERR;

static int console_translate_key(int c)
{
	switch (c) {
	case KEY_LEFT:
		return 2;
	case KEY_RIGHT:
		return 6;
	case KEY_UP:
		return 16;
	case KEY_DOWN:
		return 14;
	case KEY_DC:
		return 4;
	case KEY_BACKSPACE:
		return 8;
	case KEY_HOME:
		return 1;
	case KEY_END:
		return 5;
	case KEY_PPAGE:
		return 7;
	case KEY_NPAGE:
		return 3;
	case KEY_ENTER:
		return 13;
	default:
		break;
	}

	return c;
}

/* like 'getkey', but doesn't wait, returns -1 if nothing available */
int checkkey(void)
{
	int c;

	/* Check for SAVE_CHAR. This should not be true, because this
	   means checkkey is called twice continuously.  */
	if (save_char != ERR)
		return save_char;

	c = getch();
	/* If C is not ERR, then put it back in the input queue.  */
	if (c != ERR)
		save_char = c;
	return console_translate_key(c);

}

/* returns packed BIOS/ASCII code */
int getkey(void)
{
	int c;

	/* If checkkey has already got a character, then return it.  */
	if (save_char != ERR) {
		c = save_char;
		save_char = ERR;
		return console_translate_key(c);
	}

	wtimeout(stdscr, -1);
	c = getch();
	wtimeout(stdscr, 100);

	return console_translate_key(c);
}

/* Display an ASCII character.  */
void grub_putchar(int c)
{
	if (c == '\t') {
		int n;

		n = 8 - ((getxy() >> 8) & 3);
		while (n--)
			grub_putchar(' ');

		return;
	}

	if (c == '\n') {
		grub_putchar('\r');

		/* Internal `more'-like feature.  */
		if (count_lines >= 0) {
			count_lines++;
			if (count_lines >= max_lines - 2) {
				int tmp;

				/* It's important to disable the feature temporarily, because
				   the following grub_printf call will print newlines.  */
				count_lines = -1;

				console_setcolorstate(COLOR_STATE_HIGHLIGHT);

				grub_printf("\n[Hit return to continue]");

				console_setcolorstate(COLOR_STATE_NORMAL);

				do {
					tmp = ASCII_CHAR(getkey());
				}
				while (tmp != '\n' && tmp != '\r');
				grub_printf("\r                        \r");

				/* Restart to count lines.  */
				count_lines = 0;
				return;
			}
		}
	}

	console_putchar(c);
}

void console_putchar(int c)
{
	int x, y;
	/* displays an ASCII character.  IBM displays will translate some
	   characters to special graphical ones */

	/* Curses doesn't have VGA fonts.  */
	switch (c) {
	case DISP_UL:
		c = ACS_ULCORNER;
		break;
	case DISP_UR:
		c = ACS_URCORNER;
		break;
	case DISP_LL:
		c = ACS_LLCORNER;
		break;
	case DISP_LR:
		c = ACS_LRCORNER;
		break;
	case DISP_HORIZ:
		c = ACS_HLINE;
		break;
	case DISP_VERT:
		c = ACS_VLINE;
		break;
	case DISP_LEFT:
		c = ACS_LARROW;
		break;
	case DISP_RIGHT:
		c = ACS_RARROW;
		break;
	case DISP_UP:
		c = ACS_UARROW;
		break;
	case DISP_DOWN:
		c = ACS_DARROW;
		break;
	default:
		break;
	}

	/* In ncurses, a newline is treated badly, so we emulate it in our
	   own way.  */

	if (c == '\n') {
		getyx(stdscr, y, x);
		if (y + 1 == LINES) {
			scroll(stdscr);
			refresh();
		} else {
			move(y + 1, x);
		}
	} else if (c == '\r') {
		getyx(stdscr, y, x);
		move(y, 0);
	} else if (isprint(c)) {
		getyx(stdscr, y, x);
		if (x + 1 == COLS) {
			console_putchar('\r');
			console_putchar('\n');
		}
		if (console_color_state == COLOR_STATE_HIGHLIGHT)
			color_set(2, NULL);
		else
			color_set(1, NULL);
		addch(c);
	} else {
		addch(c);
	}
}

void gotoxy(int x, int y)
{
	move(y, x);
}

int getxy(void)
{
	unsigned int x, y;

	getyx(stdscr, y, x);

	return (x << 8) | (y & 0xff);
}

void cls(void)
{
	clear();
	move(0, 0);
}

static int cursor_state = 0;
int setcursor(int on)
{
	int old = cursor_state;

	cursor_state = on;
	curs_set(on);

	return old;
}

int substring(const char *s1, const char *s2)
{
	while (*s1 == *s2) {
		/* The strings match exactly. */
		if (!*(s1++))
			return 0;
		s2++;
	}

	/* S1 is a substring of S2. */
	if (*s1 == 0)
		return -1;

	/* S1 isn't a substring. */
	return 1;
}

/* Terminate the string STR with NUL.  */
int nul_terminate(char *str)
{
	int ch;

	while (*str && !isspace(*str))
		str++;

	ch = *str;
	*str = 0;
	return ch;
}
