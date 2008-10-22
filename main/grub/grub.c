/*
 *  This file is part of FILO.
 *
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2004,2005  Free Software Foundation, Inc.
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
#include <fs.h>
#include <lib.h>
#include <arch/timer.h>

extern char config_file[];

char PASSWORD_BUF[PASSWORD_BUFLEN]; /* The buffer for the password.  */
char DEFAULT_FILE_BUF[DEFAULT_FILE_BUFLEN]; /* THe buffer for the filename of "/boot/grub/default".  */
char CMDLINE_BUF[CMDLINE_BUFLEN]; /* The buffer for the command-line.  */
char HISTORY_BUF[HISTORY_BUFLEN]; /* The history buffer for the command-line. */
char COMPLETION_BUF[COMPLETION_BUFLEN]; /* The buffer for the completion.  */
char UNIQUE_BUF[UNIQUE_BUFLEN]; /* The buffer for the unique string.  */
char KILL_BUF[KILL_BUFLEN]; /* The kill buffer for the command-line.  */
char MENU_BUF[MENU_BUFLEN]; /* The buffer for the menu entries.  */
static char configs[16384];

int using_grub_interface = 0;

#define ENTER '\r'
#define ESCAPE '\x1b'

#ifndef CONFIG_MENULST_TIMEOUT
#define CONFIG_MENULST_TIMEOUT 0
#endif
#if !CONFIG_MENULST_TIMEOUT
#define menulst_delay() 0	/* success */
#endif

#if CONFIG_MENULST_TIMEOUT
static inline int menulst_delay(void)
{
	u64 timeout;
	int sec, tmp;
	char key;

	key = 0;

#ifdef CONFIG_MENULST_FILE
	printf("Press <Enter> for default menu.lst (%s), or <Esc> for prompt... ", CONFIG_MENULST_FILE);
#else
	printf("Press <Enter> for the FILO shell or <ESC> to enter a menu.lst path...");
#endif
	for (sec = CONFIG_MENULST_TIMEOUT; sec > 0 && key == 0; sec--) {
		printf("%d", sec);
		timeout = currticks() + TICKS_PER_SEC;
		while (currticks() < timeout) {
			if (havechar()) {
				key = getchar();
				if (key == ENTER || key == ESCAPE)
					break;
			}
		}
		for (tmp = sec; tmp; tmp /= 10)
			printf("\b \b");
	}
	if (key == 0) {
		printf("timed out\n");
		return 0;	/* success */
	} else {
		putchar('\n');
		if (key == ESCAPE)
			return -1;	/* canceled */
		else
			return 0;	/* default accepted */
	}
}
#endif				/* CONFIG_MENULST_TIMEOUT */

void manual_grub_menulst(void)
{
	char line[256];

	/* If Escape key is pressed already, skip autoboot */
	if (havechar() && getchar() == ESCAPE)
		return;

	if (menulst_delay() == 0) {
#ifdef CONFIG_MENULST_FILE
		printf("menu: %s\n", CONFIG_MENULST_FILE);
		strcpy(config_file, CONFIG_MENULST_FILE);
#endif
	} else {
		/* The above didn't work, ask user */
		while (havechar())
			getchar();

#ifdef CONFIG_MENULST_FILE
		strncpy(line, CONFIG_MENULST_FILE, sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
#else
		line[0] = '\0';
#endif
		for (;;) {
			printf("menu: ");
			getline(line, sizeof line);

			if (strcmp(line, "quit") == 0)
				break;

			if (line[0]) {
				copy_path_to_filo_bootline(line, config_file, 0);
				break;
			}
		}
	}


}

int probe_menulst(char *bootdevice, char *filename)
{
	char menulst[256];

	strcpy(menulst, bootdevice);
	strncat(menulst, filename, 256);
	/* Set string to zero: */
	config_file[0] = 0;
	copy_path_to_filo_bootline(menulst, config_file, 0);
	if (file_open(config_file)) {
		/* We found a config file. Bail out */
		/* The valid config file name stays in config_file[] */
		file_close();
		return 1;
	}

	return 0;
}

void grub_menulst(void)
{
	char bootdevices[256];
	char *running = bootdevices;
	char *bootdevice;

	if (get_option(bootdevices, "boot_devices"))
		goto old;

	printf("boot_devices = '%s'\n", bootdevices);

	do {
		bootdevice = strsep(&running, ";");
		if (bootdevice && *bootdevice) {
			if (probe_menulst(bootdevice, "/filo.lst"))
				return;
			if (probe_menulst(bootdevice, "/boot/filo.lst"))
				return;
			if (probe_menulst(bootdevice, "/menu.lst"))
				return;
			if (probe_menulst(bootdevice, "/boot/menu.lst"))
				return;
		}
	} while (bootdevice);

old:
	manual_grub_menulst();

}

/* Define if there is user specified preset menu string */
/* #undef PRESET_MENU_STRING */

#if defined(PRESET_MENU_STRING)

static const char *preset_menu = PRESET_MENU_STRING;

static int preset_menu_offset;

static int open_preset_menu(void)
{
	preset_menu_offset = 0;
	return preset_menu != 0;
}

static int read_from_preset_menu(char *buf, int maxlen)
{
	int len = strlen(preset_menu + preset_menu_offset);

	if (len > maxlen)
		len = maxlen;

	memmove(buf, preset_menu + preset_menu_offset, len);
	preset_menu_offset += len;

	return len;
}

static void close_preset_menu(void)
{
	/* Disable the preset menu.  */
	preset_menu = 0;
}

#else				/* ! PRESET_MENU_STRING */

#define open_preset_menu()	0
#define read_from_preset_menu(buf, maxlen)	0
#define close_preset_menu()

#endif				/* ! PRESET_MENU_STRING */

static char *get_entry(char *list, int num, int nested)
{
	int i;

	for (i = 0; i < num; i++) {
		do {
			while (*(list++));
		}
		while (nested && *(list++));
	}

	return list;
}

/* Print an entry in a line of the menu box.  */
static void print_entry(int y, int highlight, char *entry)
{
	int x;

	console_setcolorstate(COLOR_STATE_NORMAL);

	if (highlight)
		console_setcolorstate(COLOR_STATE_HIGHLIGHT);

	gotoxy(2, y);
	grub_putchar(' ');
	for (x = 3; x < 75; x++) {
		if (*entry && x <= 72) {
			if (x == 72)
				grub_putchar(DISP_RIGHT);
			else
				grub_putchar(*entry++);
		} else
			grub_putchar(' ');
	}
	gotoxy(74, y);

	console_setcolorstate(COLOR_STATE_STANDARD);
	refresh();
}

/* Print entries in the menu box.  */
static void print_entries(int y, int size, int first, int entryno, char *menu_entries)
{
	int i;

	gotoxy(77, y + 1);

	if (first)
		grub_putchar(DISP_UP);
	else
		grub_putchar(' ');

	menu_entries = get_entry(menu_entries, first, 0);

	for (i = 0; i < size; i++) {
		print_entry(y + i + 1, entryno == i, menu_entries);

		while (*menu_entries)
			menu_entries++;

		if (*(menu_entries - 1))
			menu_entries++;
	}

	gotoxy(77, y + size);

	if (*menu_entries)
		grub_putchar(DISP_DOWN);
	else
		grub_putchar(' ');

	gotoxy(74, y + entryno + 1);
	refresh();
}

static void print_border(int y, int size)
{
	int i;

	console_setcolorstate(COLOR_STATE_NORMAL);

	gotoxy(1, y);

	grub_putchar(DISP_UL);
	for (i = 0; i < 73; i++)
		grub_putchar(DISP_HORIZ);
	grub_putchar(DISP_UR);

	i = 1;
	while (1) {
		gotoxy(1, y + i);

		if (i > size)
			break;

		grub_putchar(DISP_VERT);
		gotoxy(75, y + i);
		grub_putchar(DISP_VERT);

		i++;
	}

	grub_putchar(DISP_LL);
	for (i = 0; i < 73; i++)
		grub_putchar(DISP_HORIZ);
	grub_putchar(DISP_LR);

	console_setcolorstate(COLOR_STATE_STANDARD);
}

static void run_menu(char *menu_entries, char *config_entries, int num_entries, char *heap, int entryno)
{
	int c, time1, time2 = -1, first_entry = 0;
	char *cur_entry = 0;

	/*
	 *  Main loop for menu UI.
	 */

      restart:
	while (entryno > 11) {
		first_entry++;
		entryno--;
	}

	/* If the timeout was expired or wasn't set, force to show the menu
	   interface. */
	if (grub_timeout < 0)
		show_menu = 1;

	/* If SHOW_MENU is false, don't display the menu until ESC is pressed.  */
	if (!show_menu) {
		/* Get current time.  */
		while ((time1 = getrtsecs()) == 0xFF);

		while (1) {
			/* Check if ESC is pressed.  */
			if (checkkey() != -1 && ASCII_CHAR(getkey()) == '\e') {
				grub_timeout = -1;
				show_menu = 1;
				break;
			}

			/* If GRUB_TIMEOUT is expired, boot the default entry.  */
			if (grub_timeout >= 0 && (time1 = getrtsecs()) != time2 && time1 != 0xFF) {
				if (grub_timeout <= 0) {
					grub_timeout = -1;
					goto boot_entry;
				}

				time2 = time1;
				grub_timeout--;

				/* Print a message.  */
				grub_printf("\rPress `ESC' to enter the menu... %d   ", grub_timeout);
			}
		}
	}

	/* Only display the menu if the user wants to see it. */
	if (show_menu) {
		init_page();
		setcursor(0);

		print_border(3, 12);

		grub_printf("\n\
      Use the %c and %c keys to select which entry is highlighted.\n", DISP_UP, DISP_DOWN);

		if (!auth && password) {
			grub_printf("\
      Press enter to boot the selected OS or \'p\' to enter a\n\
      password to unlock the next set of features.");
		} else {
			if (config_entries)
				grub_printf("\
      Press enter to boot the selected OS, \'e\' to edit the\n\
      commands before booting, \'a\' to modify the kernel arguments\n\
      before booting, or \'c\' for a command-line.");
			else
				grub_printf("\
      Press \'b\' to boot, \'e\' to edit the selected command in the\n\
      boot sequence, \'c\' for a command-line, \'o\' to open a new line\n\
      after (\'O\' for before) the selected line, \'d\' to remove the\n\
      selected line, or escape to go back to the main menu.");
		}

		print_entries(3, 12, first_entry, entryno, menu_entries);
	}

	/* XXX using RT clock now, need to initialize value */
	while ((time1 = getrtsecs()) == 0xFF);

	while (1) {
		/* Initialize to NULL just in case...  */
		cur_entry = NULL;

		if (grub_timeout >= 0 && (time1 = getrtsecs()) != time2 && time1 != 0xFF) {
			if (grub_timeout <= 0) {
				grub_timeout = -1;
				break;
			}

			/* else not booting yet! */
			time2 = time1;

			gotoxy(3, 22);
			grub_printf("The highlighted entry will be booted automatically in %d seconds.    ",
			     grub_timeout);
			gotoxy(74, 4 + entryno);

			grub_timeout--;
		}

		/* Check for a keypress, however if TIMEOUT has been expired
		   (GRUB_TIMEOUT == -1) relax in GETKEY even if no key has been
		   pressed.  
		   This avoids polling (relevant in the grub-shell and later on
		   in grub if interrupt driven I/O is done).  */
		if (checkkey() >= 0 || grub_timeout < 0) {
			/* Key was pressed, show which entry is selected before GETKEY,
			   since we're comming in here also on GRUB_TIMEOUT == -1 and
			   hang in GETKEY */

			c = ASCII_CHAR(getkey());

			if (grub_timeout >= 0) {
				gotoxy(3, 22);
				grub_printf("                                                                    ");
				grub_timeout = -1;
				fallback_entryno = -1;
				gotoxy(74, 4 + entryno);
			}

			/* On serial console, arrow keys might not work,
			 * therefore accept '^' and 'v' as replacement keys.
			 */
			if (c == 16 || c == '^') {
				if (entryno > 0) {
					print_entry(4 + entryno, 0,
						    get_entry(menu_entries, first_entry + entryno, 0));
					entryno--;
					print_entry(4 + entryno, 1,
						    get_entry(menu_entries, first_entry + entryno, 0));
				} else if (first_entry > 0) {
					first_entry--;
					print_entries(3, 12, first_entry, entryno, menu_entries);
				}
			} else if ((c == 14 || c == 'v')
				   && first_entry + entryno + 1 < num_entries) {
				if (entryno < 11) {
					print_entry(4 + entryno, 0,
						    get_entry(menu_entries, first_entry + entryno, 0));
					entryno++;
					print_entry(4 + entryno, 1,
						    get_entry(menu_entries, first_entry + entryno, 0));
				} else if (num_entries > 12 + first_entry) {
					first_entry++;
					print_entries(3, 12, first_entry, entryno, menu_entries);
				}
			} else if (c == 7) {
				/* Page Up */
				first_entry -= 12;
				if (first_entry < 0) {
					entryno += first_entry;
					first_entry = 0;
					if (entryno < 0)
						entryno = 0;
				}
				print_entries(3, 12, first_entry, entryno, menu_entries);
			} else if (c == 3) {
				/* Page Down */
				first_entry += 12;
				if (first_entry + entryno + 1 >= num_entries) {
					first_entry = num_entries - 12;
					if (first_entry < 0)
						first_entry = 0;
					entryno = num_entries - first_entry - 1;
				}
				print_entries(3, 12, first_entry, entryno, menu_entries);
			}

			if (config_entries) {
				if ((c == '\n') || (c == '\r') || (c == 6))
					break;
			} else {
				if ((c == 'd') || (c == 'o') || (c == 'O')) {
					print_entry(4 + entryno, 0,
						    get_entry(menu_entries, first_entry + entryno, 0));

					/* insert after is almost exactly like insert before */
					if (c == 'o') {
						/* But `o' differs from `O', since it may causes
						   the menu screen to scroll up.  */
						if (entryno < 11)
							entryno++;
						else
							first_entry++;

						c = 'O';
					}

					cur_entry = get_entry(menu_entries, first_entry + entryno, 0);

					if (c == 'O') {
						memmove(cur_entry + 2, cur_entry, ((int) heap) - ((int)cur_entry));

						cur_entry[0] = ' ';
						cur_entry[1] = 0;

						heap += 2;

						num_entries++;
					} else if (num_entries > 0) {
						char *ptr = get_entry(menu_entries,
								      first_entry + entryno + 1,
								      0);

						memmove(cur_entry, ptr, ((int) heap) - ((int) ptr));
						heap -= (((int) ptr) - ((int) cur_entry));

						num_entries--;

						if (entryno >= num_entries)
							entryno--;
						if (first_entry && num_entries < 12 + first_entry)
							first_entry--;
					}

					print_entries(3, 12, first_entry, entryno, menu_entries);
				}

				cur_entry = menu_entries;
				if (c == 27)
					return;
				if (c == 'b')
					break;
			}

			if (!auth && password) {
				if (c == 'p') {
					/* Do password check here! */
					char entered[32];
					char *pptr = password;

					gotoxy(1, 21);

					/* Wipe out the previously entered password */
					memset(entered, 0, sizeof(entered));
					get_cmdline(" Password: ", entered, 31, '*', 0);

					while (!isspace(*pptr) && *pptr)
						pptr++;

					/* Make sure that PASSWORD is NUL-terminated.  */
					*pptr++ = 0;

					if (!check_password(entered, password, password_type)) {
						char *new_file = config_file;
						while (isspace(*pptr))
							pptr++;

						/* If *PPTR is NUL, then allow the user to use
						   privileged instructions, otherwise, load
						   another configuration file.  */
						if (*pptr != 0) {
							while ((*(new_file++)
								= *(pptr++))
							       != 0);

							/* Make sure that the user will not have
							   authority in the next configuration.  */
							auth = 0;
							return;
						} else {
							/* Now the user is superhuman.  */
							auth = 1;
							goto restart;
						}
					} else {
						grub_printf("Failed!\n      Press any key to continue...");
						getkey();
						goto restart;
					}
				}
			} else {
				if (c == 'e') {
					int new_num_entries = 0, i = 0;
					char *new_heap;

					if (config_entries) {
						new_heap = heap;
						cur_entry = get_entry(config_entries, first_entry + entryno, 1);
					} else {
						/* safe area! */
						new_heap = heap + NEW_HEAPSIZE + 1;
						cur_entry = get_entry(menu_entries, first_entry + entryno, 0);
					}

					do {
						while ((*(new_heap++) = cur_entry[i++]) != 0);
						new_num_entries++;
					}
					while (config_entries && cur_entry[i]);

					/* this only needs to be done if config_entries is non-NULL,
					   but it doesn't hurt to do it always */
					*(new_heap++) = 0;

					if (config_entries)
						run_menu(heap, NULL, new_num_entries, new_heap, 0);
					else {
						/* flush color map */
						grub_printf(" ");
						cls();
						print_cmdline_message(CMDLINE_EDIT_MODE);

						new_heap = heap + NEW_HEAPSIZE + 1;

						if (!get_cmdline
						    (CONFIG_PROMPT " edit> ", new_heap, NEW_HEAPSIZE + 1, 0, 1)) {
							int j = 0;

							/* get length of new command */
							while (new_heap[j++]);

							if (j < 2) {
								j = 2;
								new_heap[0]
								    = ' ';
								new_heap[1]
								    = 0;
							}

							/* align rest of commands properly */
							memmove(cur_entry + j, cur_entry + i, (int) heap - ((int) cur_entry + i));

							/* copy command to correct area */
							memmove(cur_entry, new_heap, j);

							heap += (j - i);
						}
					}

					goto restart;
				}
				if (c == 'c') {
					extern int keep_cmdline_running;
					enter_cmdline(heap, 0);
					if (keep_cmdline_running)
						goto restart;
					else
						return;
				}
				if (config_entries && c == 'a') {
					int new_num_entries = 0, i = 0, j;
					int needs_padding, amount;
					char *new_heap;
					char *entries;
					char *entry_copy;
					char *append_line;
					char *start;

					entry_copy = new_heap = heap;
					cur_entry = get_entry(config_entries, first_entry + entryno, 1);

					do {
						while ((*(new_heap++) = cur_entry[i++]) != 0);
						new_num_entries++;
					}
					while (config_entries && cur_entry[i]);

					/* this only needs to be done if config_entries is non-NULL,
					   but it doesn't hurt to do it always */
					*(new_heap++) = 0;

					new_heap = heap + NEW_HEAPSIZE + 1;

					entries = entry_copy;
					while (*entries) {
						if ((strstr(entries, "kernel") == entries)
						    && isspace(entries[6]))
							break;

						while (*entries)
							entries++;
						entries++;
					}

					if (!*entries)
						goto restart;

					start = entries + 6;

					/* skip the white space */
					while (*start && isspace(*start))
						start++;
					/* skip the kernel name */
					while (*start && !isspace(*start))
						start++;

					/* skip the white space */
					needs_padding = (!*start || !isspace(*start));
					while (*start && isspace(*start))
						start++;

					append_line = new_heap;
					strcpy(append_line, start);

					cls();
					print_cmdline_message(CMDLINE_EDIT_MODE);

					if (get_cmdline(CONFIG_PROMPT " append> ", append_line, NEW_HEAPSIZE + 1, 0, 1))
						goto restart;

					/* have new args; append_line points to the
					   new args and start points to the old
					   args */

					i = strlen(start);
					j = strlen(append_line);

					if (i > (j + needs_padding))
						amount = i;
					else
						amount = j + needs_padding;

					/* align rest of commands properly */
					memmove(start + j + needs_padding,
						start + i, ((int) append_line) - ((int) start) - (amount));

					if (needs_padding)
						*start = ' ';

					/* copy command to correct area */
					memmove(start + needs_padding, append_line, j);

					/* set up this entry to boot */
					config_entries = NULL;
					cur_entry = entry_copy;
					heap = new_heap;

					break;
				}
			}
		}
	}

	/* Attempt to boot an entry.  */

      boot_entry:

	cls();
	setcursor(1);

	while (1) {
		if (config_entries)
			grub_printf("  Booting \'%s\'\n\n", get_entry(menu_entries, first_entry + entryno, 0));
		else
			grub_printf("  Booting command-list\n\n");

		if (!cur_entry)
			cur_entry = get_entry(config_entries, first_entry + entryno, 1);

		/* Set CURRENT_ENTRYNO for the command "savedefault".  */
		current_entryno = first_entry + entryno;
		if (run_script(cur_entry, heap)) {
			if (fallback_entryno >= 0) {
				cur_entry = NULL;
				first_entry = 0;
				entryno = fallback_entries[fallback_entryno];
				fallback_entryno++;
				if (fallback_entryno >= MAX_FALLBACK_ENTRIES || fallback_entries[fallback_entryno] < 0)
					fallback_entryno = -1;
			} else
				break;
		} else
			break;
	}

	show_menu = 1;
	goto restart;
}


static int get_line_from_config(char *cmdline, int maxlen, int read_from_file)
{
	int pos = 0, literal = 0, comment = 0;
	char c;			/* since we're loading it a byte at a time! */

	while (1) {
		if (read_from_file) {
			if (!file_read(&c, 1))
				break;
		} else {
			if (!read_from_preset_menu(&c, 1))
				break;
		}

		/* Skip all carriage returns.  */
		if (c == '\r')
			continue;

		/* Replace tabs with spaces.  */
		if (c == '\t')
			c = ' ';

		/* The previous is a backslash, then...  */
		if (literal) {
			/* If it is a newline, replace it with a space and continue.  */
			if (c == '\n') {
				c = ' ';

				/* Go back to overwrite a backslash.  */
				if (pos > 0)
					pos--;
			}

			literal = 0;
		}

		/* translate characters first! */
		if (c == '\\' && !literal)
			literal = 1;

		if (comment) {
			if (c == '\n')
				comment = 0;
		} else if (!pos) {
			if (c == '#')
				comment = 1;
			else if ((c != ' ') && (c != '\n'))
				cmdline[pos++] = c;
		} else {
			if (c == '\n')
				break;

			if (pos < maxlen)
				cmdline[pos++] = c;
		}
	}

	cmdline[pos] = 0;

	return pos;
}

int is_opened = 0, is_preset = 0;

/* This is the starting function in C.  */
void grub_main(void)
{
	int config_len, menu_len, num_entries;
	char *config_entries, *menu_entries;
	char *kill_buf = (char *) KILL_BUF;

	auto void reset(void);
	void reset(void) {
		count_lines = -1;
		config_len = 0;
		menu_len = 0;
		num_entries = 0;
		config_entries = (char *) configs;
		memset(configs, 0, 16384);
		menu_entries = MENU_BUF;
		memset(MENU_BUF, 0, MENU_BUFLEN);
		init_config();
	}

	/* Initialize TinyCurses */
	initscr();
	cbreak();
	noecho();
	nonl();
	scrollok(stdscr, TRUE);
	keypad(stdscr, TRUE);
	wtimeout(stdscr, 100);
	endwin();
	using_grub_interface = 1;

	console_setcolor((COLOR_BLACK << 4) | COLOR_WHITE,
			 (COLOR_WHITE << 4) | COLOR_BLACK);

	/* Initialize the kill buffer.  */
	*kill_buf = 0;

	/* Never return.  */
	for (;;) {
		char *default_file = (char *) DEFAULT_FILE_BUF;
		int i;

		reset();

		/* Here load the configuration file.  */

		/* Get a saved default entry if possible.  */
		saved_entryno = 0;
		*default_file = 0;

		strncat(default_file, config_file, DEFAULT_FILE_BUFLEN);
		for (i = strlen(default_file); i >= 0; i--)
			if (default_file[i] == '/') {
				i++;
				break;
			}
		default_file[i] = 0;
		strncat(default_file + i, "default", DEFAULT_FILE_BUFLEN - i);
		if (file_open(default_file)) {
			char buf[10];	/* This is good enough.  */
			char *p = buf;
			int len;

			len = file_read(buf, sizeof(buf));
			if (len > 0) {
				buf[sizeof(buf) - 1] = 0;
				safe_parse_maxint(&p, &saved_entryno);
			}

			file_close();
		}

		errnum = ERR_NONE;

		do {
			/* STATE 0:  Before any title command.
			   STATE 1:  In a title command.
			   STATE >1: In a entry after a title command.  */
			int state = 0, prev_config_len = 0, prev_menu_len = 0;
			char *cmdline;

			/* Try the preset menu first. This will succeed at most once,
			   because close_preset_menu disables the preset menu.  */
			is_opened = is_preset = open_preset_menu();
			if (!is_opened) {
				is_opened = file_open(config_file);
				errnum = ERR_NONE;
			}

			if (!is_opened) {
				grub_printf("Could not open menu.lst file '%s'. Entering command line.\n", config_file);
				// memset(myheap, 0, 256);
				// run_script("terminal console\n\0", myheap);
				break;
			}

			/* This is necessary, because the menu must be overrided.  */
			reset();

			cmdline = (char *) CMDLINE_BUF;
			while (get_line_from_config(cmdline, NEW_HEAPSIZE, !is_preset)) {
				struct builtin *builtin;

				/* Get the pointer to the builtin structure.  */
				builtin = find_command(cmdline);
				errnum = 0;
				if (!builtin)
					/* Unknown command. Just skip now.  */
					continue;

				if (builtin->flags & BUILTIN_TITLE) {
					char *ptr;

					/* the command "title" is specially treated.  */
					if (state > 1) {
						/* The next title is found.  */
						num_entries++;
						config_entries[config_len++]
						    = 0;
						prev_menu_len = menu_len;
						prev_config_len = config_len;
					} else {
						/* The first title is found.  */
						menu_len = prev_menu_len;
						config_len = prev_config_len;
					}

					/* Reset the state.  */
					state = 1;

					/* Copy title into menu area.  */
					ptr = skip_to(1, cmdline);
					while ((menu_entries[menu_len++] = *(ptr++)) != 0);
				} else if (!state) {
					/* Run a command found is possible.  */
					if (builtin->flags & BUILTIN_MENU) {
						char *arg = skip_to(1,
								    cmdline);
						(builtin->func) (arg, BUILTIN_MENU);
						errnum = 0;
					} else
						/* Ignored.  */
						continue;
				} else {
					char *ptr = cmdline;

					state++;
					/* Copy config file data to config area.  */
					while ((config_entries[config_len++] = *ptr++) != 0);
				}
			}

			if (state > 1) {
				/* Finish the last entry.  */
				num_entries++;
				config_entries[config_len++] = 0;
			} else {
				menu_len = prev_menu_len;
				config_len = prev_config_len;
			}

			menu_entries[menu_len++] = 0;
			config_entries[config_len++] = 0;
			memmove(config_entries + config_len, menu_entries, menu_len);
			menu_entries = config_entries + config_len;

			/* Make sure that all fallback entries are valid.  */
			if (fallback_entryno >= 0) {
				for (i = 0; i < MAX_FALLBACK_ENTRIES; i++) {
					if (fallback_entries[i] < 0)
						break;
					if (fallback_entries[i] >= num_entries) {
						memmove (fallback_entries + i,
						     fallback_entries + i + 1, ((MAX_FALLBACK_ENTRIES - i - 1)
										* sizeof(int)));
						i--;
					}
				}

				if (fallback_entries[0] < 0)
					fallback_entryno = -1;
			}

			/* Check if the default entry is present. Otherwise reset
			 * it to fallback if fallback is valid, or to DEFAULT_ENTRY 
			 * if not.
			 */
			if (default_entry >= num_entries) {
				if (fallback_entryno >= 0) {
					default_entry = fallback_entries[0];
					fallback_entryno++;
					if (fallback_entryno >= MAX_FALLBACK_ENTRIES ||
							fallback_entries[fallback_entryno] < 0)
						fallback_entryno = -1;
				} else {
					default_entry = 0;
				}
			}

			if (is_preset)
				close_preset_menu();
			else
				file_close();

		} while (is_preset);

		if (!num_entries) {
			/* If no acceptable config file, goto command-line, starting
			   heap from where the config entries would have been stored
			   if there were any.  */
			enter_cmdline(config_entries, 1);
		} else {
			/* Run menu interface.  */
			run_menu(menu_entries, config_entries, num_entries, menu_entries + menu_len, default_entry);
		}
	}
}
