/*
 * This file is part of FILO.
 *
 *  Copyright (C) 1999,2000,2001,2002,2004  Free Software Foundation, Inc.
 *  Copyright (C) 2005-2010 coresystems GmbH
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

#include <libpayload-config.h>
#include <libpayload.h>
#include <getopt.h>

#include <config.h>
#include <fs.h>
#include <lib.h>
#include <grub/shared.h>
#include <timer.h>
#ifdef CONFIG_USE_MD5_PASSWORDS
#include <grub/md5.h>
#endif
#include <pci.h>
#include <csl.h>
#include <sbs.h>

/* The default entry.  */
int default_entry = 0;

int current_entryno;

// from disk_io:
int buf_drive = -1;
unsigned long current_drive = GRUB_INVALID_DRIVE;
// from common.c:
unsigned long saved_drive;
unsigned long saved_partition;
unsigned long saved_mem_upper;
// other..
unsigned long install_partition = 0x20000;
unsigned long boot_drive = 0;
char config_file[128] = "\0";

/* indicator if we encountered a 'configfile' command and have to restart */
int reload_configfile = 0;

kernel_t kernel_type;

/* The fallback entry.  */
int fallback_entryno;
int fallback_entries[MAX_FALLBACK_ENTRIES];

int grub_timeout = -1;

/* The password.  */
char *password = NULL;
/* The password type.  */
password_t password_type;
/* The flag for indicating that the user is authoritative.  */
int auth = 0;

/* -------- FILO logic -------- */
#define BOOT_LINE_LENGTH 1024
char boot_line[BOOT_LINE_LENGTH] = { 0 };
char root_device[16] = { 0 };
/* ---------------------------- */

/* temporary space for run time checks */
static char temp_space[BOOT_LINE_LENGTH];
char initrd_space[BOOT_LINE_LENGTH]="\0";

int show_menu = 1;

static int last_normal_color = -1, last_highlight_color = -1;

/* Initialize the data for builtins.  */
void init_builtins(void)
{
	kernel_type = KERNEL_TYPE_NONE;
}

/* Initialize the data for the configuration file.  */
void init_config(void)
{
	password = NULL;
	fallback_entryno = -1;
	fallback_entries[0] = -1;
	grub_timeout = -1;
	reload_configfile = 0;
}

int check_password(char *entered, char *expected, password_t type)
{
	switch (type) {
	case PASSWORD_PLAIN:
		return strcmp(entered, expected);
#ifdef CONFIG_USE_MD5_PASSWORDS
	case PASSWORD_MD5:
		return check_md5_password(entered, expected);
#endif
	default:
		/* unsupported password type: be secure */
		return 1;
	}
}

/* TODO: handle \" */
int to_argc_argv(char *args, char ***argvp)
{
	/* allocate enough space to point to all arguments:
	 * args only contains the arguments (no command name), so
	 * - add 1 for argv[0] (= NULL)
	 * - add strlen(args)/2 + 1 (handles worst case scenario: "a b")
	 * - add 1 for argv[argc] (= NULL)
	 */
	char **argv = malloc(sizeof(char*)*(strlen(args)/2+3));
	int argc = 0;
	argv[argc++] = NULL;
	while (*args) {
		int skipbytes = 0;
		int quoted = 0;

		/* register token */
		argv[argc++] = args;

		/* skip over token */
		while (*args) {
			if (!quoted && isblank(*args)) {
				break;
			}
			/* if quote, skip current byte */
			if (*args == '"') {
				quoted = !quoted;
				skipbytes++;
			} else {
				/* otherwise, proceed */
				args++;
			}
			/* make up for skipped quotes */
			*args = args[skipbytes];
		}

		/* terminate token, but don't skip over trailing NUL */
		if (*args != '\0') {
			*args++ = '\0';
			args += skipbytes;
		}

		/* skip any whitespace before next token */
		while ((*args != '\0') && (isblank(*args))) {
			args++;
		}
	}
	argv[argc] = NULL;
	*argvp = argv;
	return argc;
}

/* boot */
static int boot_func(char *arg, int flags)
{
	int boot(const char *line);
	int ret;

	if(!boot_line[0]) {
		errnum = ERR_BOOT_COMMAND;
		return 1;
	}

	/* Set color back to black and white, or Linux booting will look
	 * very funny.
	 */
	console_setcolor((COLOR_BLACK << 4) | COLOR_WHITE,
			 (COLOR_WHITE << 4) | COLOR_BLACK);

	cls();

	if (initrd_space[0]) {
		strncat(boot_line, " initrd=", BOOT_LINE_LENGTH);
		strncat(boot_line, initrd_space, BOOT_LINE_LENGTH);
	}

	grub_printf("\nBooting '%s'\n", boot_line);

	ret = boot(boot_line);

	/* If we regain control, something went wrong. */

	/* The menu color was changed and we failed to boot, so we
	 * need to restore the colors in order to make the menu look as
	 * it did before.
	 */
	if (last_normal_color != -1) {
		console_setcolor(last_normal_color, last_highlight_color);
	}

	/* If no loader felt responsible for this image format, it's
	 * a bad file format, otherwise we don't really know.
	 */
	if (ret == LOADER_NOT_SUPPORT)
		errnum = ERR_EXEC_FORMAT;
	else
		errnum = ERR_BOOT_FAILURE;

	return 1;
}

static struct builtin builtin_boot = {
	"boot",
	boot_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"boot",
	"Boot the OS/chain-loader which has been loaded."
};


/* color */
/* Set new colors used for the menu interface. Support two methods to
 *    specify a color name: a direct integer representation and a symbolic
 *       color name. An example of the latter is "blink-light-gray/blue".  */
static int color_func(char *arg, int flags)
{
	char *normal;
	char *highlight;
	int new_normal_color;
	int new_highlight_color;
	static char *color_list[16] = {
		"black",
		"red",
		"green",
		"brown",
		"blue",
		"magenta",
		"cyan",
		"light-gray",
		"dark-gray",
		"light-red",
		"light-green",
		"yellow",
		"light-blue",
		"light-magenta",
		"light-cyan",
		"white"
	};

	auto int color_number(char *str);

	/* Convert the color name STR into a VGA color number.  */
	auto int color_number(char *str) {
		char *ptr;
		int i;
		int color = 0;

		/* Find the separator.  */
		for (ptr = str; *ptr && *ptr != '/'; ptr++);

		/* If not found, return -1.  */
		if (!*ptr)
			return -1;

		/* Terminate the string STR.  */
		*ptr++ = 0;

		/* If STR contains the prefix "blink-", then set the `blink' bit in COLOR.  */
		if (substring("blink-", str) <= 0) {
			color = 0x80;
			str += 6;
		}

		/* Search for the foreground color name.  */
		for (i = 0; i < 16; i++)
			if (strcmp(color_list[i], str) == 0) {
				color |= i;
				break;
			}

		if (i == 16)
			return -1;

		str = ptr;
		nul_terminate(str);

		/* Search for the background color name.  */
		for (i = 0; i < 8; i++)
			if (strcmp(color_list[i], str) == 0) {
				color |= (i <<4);
				break;
			}

		if (i == 8)
			return -1;

		return color;
	}

	normal = arg;
	highlight = skip_to(0, arg);

	new_normal_color = color_number(normal);
	if (new_normal_color < 0 && !safe_parse_maxint(&normal, &new_normal_color)) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	/* The second argument is optional, so set highlight_color to inverted NORMAL_COLOR.  */
	if (!*highlight)
		new_highlight_color = ((new_normal_color >> 4)
				       | ((new_normal_color & 0xf) << 4));
	else {
		new_highlight_color = color_number(highlight);
		if (new_highlight_color < 0 && !safe_parse_maxint(&highlight, &new_highlight_color)) {
			errnum = ERR_BAD_ARGUMENT;
			return 1;
		}
	}

	console_setcolor(new_normal_color, new_highlight_color);

	// keep the state so we can restore after a failed boot
	last_normal_color = new_normal_color;
	last_highlight_color = new_highlight_color;

	return 0;
}

static struct builtin builtin_color = {
	"color",
	color_func,
	BUILTIN_CMDLINE | BUILTIN_MENU | BUILTIN_HELP_LIST,
	"color NORMAL [HIGHLIGHT]",
	"Change the menu colors. The color NORMAL is used for most"
	    " lines in the menu, and the color HIGHLIGHT is used to highlight the"
	    " line where the cursor points. If you omit HIGHLIGHT, then the"
	    " inverted color of NORMAL is used for the highlighted line."
	    " The format of a color is \"FG/BG\". FG and BG are symbolic color names."
	    " A symbolic color name must be one of these: black, blue, green,"
	    " cyan, red, magenta, brown, light-gray, dark-gray, light-blue,"
	    " light-green, light-cyan, light-red, light-magenta, yellow and white."
	    " But only the first eight names can be used for BG. You can prefix"
	    " \"blink-\" to FG if you want a blinking foreground color."
};

static int help_func(char *arg, int flags);
/* configfile */
static int configfile_func(char *arg, int flags)
{
	extern int is_opened, keep_cmdline_running;

	/* Check if the file ARG is present.  */
	copy_path_to_filo_bootline(arg, temp_space, 1, 0);
	if (temp_space[0]==0) {
		return help_func("configfile",0);
	}
	if (!file_open(temp_space)) {
		errnum = ERR_FILE_NOT_FOUND;
		return 1;
	}

	file_close();

	/* Copy ARG to CONFIG_FILE.  */
	copy_path_to_filo_bootline(arg, config_file, 1, 0);

	/* Force to load the configuration file.  */
	is_opened = 0;
	keep_cmdline_running = 0;
	reload_configfile = 1;

	/* Make sure that the user will not be authoritative.  */
	auth = 0;

	return 0;
}

static struct builtin builtin_configfile = {
	"configfile",
	configfile_func,
	BUILTIN_CMDLINE | BUILTIN_MENU | BUILTIN_HELP_LIST,
	"configfile FILE",
	"Load FILE as the configuration file."
};

/* default */
static int default_func(char *arg, int flags)
{
	unsigned char buf[1];
	if (get_option(buf, "boot_default"))
		buf[0] = 0xff;

	if ((unsigned char)buf[0] != 0xff) {
		printf("Default override by CMOS.\n");
		return 0;
	}

	if (!safe_parse_maxint(&arg, &default_entry))
		return 1;

	return 0;
}

static struct builtin builtin_default = {
	"default",
	default_func,
	BUILTIN_MENU,
#if 0
	"default [NUM]",
	"Set the default entry to entry number NUM (if not specified, it is"
	    " 0, the first entry) or the entry number saved by savedefault."
#endif
};

#if CONFIG_DEVELOPER_TOOLS
/* dumpmem */
static int dumpmem_func(char *arg, int flags)
{
	int ret = string_to_args("dumpmem", arg);
	unsigned int mem_base, mem_len;
	void *i;

	if(ret || (string_argc != 3)) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	// FIXME
	if (!safe_parse_maxint(&string_argv[1], (int *)&mem_base))
		return 1;
	if (!safe_parse_maxint(&string_argv[2], (int *)&mem_len))
		return 1;

	grub_printf("Dumping memory at 0x%08x (0x%x bytes)\n",
			mem_base, mem_len);

	for (i=phys_to_virt(mem_base); i<phys_to_virt(mem_base + mem_len); i++) {
		if (((unsigned long)i & 0x0f) == 0)
			grub_printf("\n%08x:", i);
		unsigned char val = *((unsigned char *)i);
		grub_printf(" %02x", val);
	}
	grub_printf("\n");

	return 0;
}

static struct builtin builtin_dumpmem = {
	"dumpmem",
	dumpmem_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"dumpmem",
	"Dump memory"
};

#if CONFIG_TARGET_I386
/* dumppm */
static int dumppm_func(char *arg, int flags)
{
	u16 pmbase;

	pmbase = pci_read_config16(PCI_DEV(0,0x1f, 0), 0x40) & 0xfffe;

	grub_printf("pmbase+0x0000: 0x%04x     (PM1_STS)\n", inw(pmbase+0x0000));
	grub_printf("pmbase+0x0002: 0x%04x     (PM1_EN)\n", inw(pmbase+0x0002));
	grub_printf("pmbase+0x0004: 0x%08x (PM1_CNT)\n", inl(pmbase+0x0004));
	grub_printf("pmbase+0x0008: 0x%08x (PM1_TMR)\n", inl(pmbase+0x0008));
	grub_printf("pmbase+0x0010: 0x%08x (PROC_CNT)\n", inl(pmbase+0x0010));
	grub_printf("pmbase+0x0020: 0x%08x (PM2_CNT)\n", inl(pmbase+0x0020));
	grub_printf("pmbase+0x0028: 0x%08x (GPE0_STS)\n", inl(pmbase+0x0028));
	grub_printf("pmbase+0x002c: 0x%08x (GPE0_EN)\n", inl(pmbase+0x002c));
	grub_printf("pmbase+0x0030: 0x%08x (SMI_EN)\n", inl(pmbase+0x0030));
	grub_printf("pmbase+0x0034: 0x%08x (SMI_STS)\n", inl(pmbase+0x0034));
	grub_printf("pmbase+0x0038: 0x%04x     (ALT_GP_SMI_EN)\n", inw(pmbase+0x0038));
	grub_printf("pmbase+0x003a: 0x%04x     (ALT_GP_SMI_STS)\n", inw(pmbase+0x003a));
	grub_printf("pmbase+0x0042: 0x%02x       (GPE_CNTL)\n", inb(pmbase+0x0042));
	grub_printf("pmbase+0x0044: 0x%04x     (DEVACT_STS)\n", inw(pmbase+0x0044));
	grub_printf("pmbase+0x0050: 0x%02x       (SS_CNT)\n", inb(pmbase+0x0050));
	grub_printf("pmbase+0x0054: 0x%08x (C3_RES)\n", inl(pmbase+0x0054));
#if 0
	// TCO
	grub_printf("pmbase+0x0060: 0x%04x     (TCO_RLD)\n", inw(pmbase+0x0060));
	grub_printf("pmbase+0x0062: 0x%02x       (TCO_DAT_IN)\n", inb(pmbase+0x0062));
	grub_printf("pmbase+0x0063: 0x%02x       (TCO_DAT_OUT)\n", inb(pmbase+0x0063));
	grub_printf("pmbase+0x0064: 0x%04x     (TCO1_STS)\n", inw(pmbase+0x0064));
	grub_printf("pmbase+0x0066: 0x%04x     (TCO2_STS)\n", inw(pmbase+0x0066));
	grub_printf("pmbase+0x0068: 0x%04x     (TCO1_CNT)\n", inw(pmbase+0x0068));
	grub_printf("pmbase+0x006a: 0x%04x     (TCO2_CNT)\n", inw(pmbase+0x006a));
	grub_printf("pmbase+0x006c: 0x%04x     (TCO_MESSAGE)\n", inw(pmbase+0x006c));
	grub_printf("pmbase+0x006e: 0x%02x       (TCO_WDCNT)\n", inb(pmbase+0x006e));
	grub_printf("pmbase+0x0070: 0x%02x       (TCO_SW_IRQ_GEN)\n", inb(pmbase+0x0070));
	grub_printf("pmbase+0x0072: 0x%04x     (TCO_TMR)\n", inw(pmbase+0x0072));
#endif
	return 0;
}

static struct builtin builtin_dumppm = {
	"dumppm",
	dumppm_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"dumppm",
	"Dump Powermanagement registers"
};
#endif
#endif

#if CONFIG_EXPERIMENTAL
#warning "FIND not implemented yet."
/* find */
/* Search for the filename ARG in all of partitions.  */
static int find_func(char *arg, int flags)
{
	char *filename = arg;
	int got_file = 0;

	// the grub find works like this:
	//
	// for all disks
	//   for all partitions on disk
	//     open file
	//     if file exists
	//        print partition name
	//        set got_file to 1
	//
	// dont they search all subdirectories? Thats a dumb find then.. :(

	/* We want to ignore any error here.  */
	errnum = ERR_NONE;

	if (got_file) {
		errnum = ERR_NONE;
		return 0;
	}

	errnum = ERR_FILE_NOT_FOUND;
	return 1;
}

static struct builtin builtin_find = {
	"find",
	find_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"find FILENAME",
	"Search for the filename FILENAME in all of partitions and print the list of"
	    " the devices which contain the file."
};
#endif

#ifdef CONFIG_FLASHROM_UNLOCK
/* flashrom_unlock */
/* Disable lockdown of flash memory on boot */
static int flashrom_unlock_func(char *arg, int flags)
{
	flashrom_lockdown = 0;

	return 0;
}

static struct builtin builtin_flashrom_unlock = {
	"flashrom_unlock",
	flashrom_unlock_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"flashrom_unlock"
	"Disable lockdown of flash ROM on boot."
};
#endif

#ifdef CONFIG_FLASHUPDATE
int flashupdate_func(char *arg, int flags);

static struct builtin builtin_flashupdate = {
	"flashupdate",
	flashupdate_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"flashupdate DEVICE",
	"Update flash ROM from a file loaded from DEVICE (selected by the user)."
};
#endif

/* help */
#define MAX_SHORT_DOC_LEN       39
#define MAX_LONG_DOC_LEN        66

static int help_func(char *arg, int flags)
{
	int all = 0;

	if (memcmp(arg, "--all", sizeof("--all") - 1) == 0) {
		all = 1;
		arg = skip_to(0, arg);
	}

	if (!*arg) {
		/* Invoked with no argument. Print the list of the short docs.  */
		struct builtin **builtin;
		int left = 1;

		for (builtin = builtin_table; *builtin != 0; builtin++) {
			int len;
			int i;

			/* If this cannot be used in the command-line interface,
			   skip this.  */
			if (!((*builtin)->flags & BUILTIN_CMDLINE))
				continue;

			/* If this doesn't need to be listed automatically and "--all"
			   is not specified, skip this.  */
			if (!all && !((*builtin)->flags & BUILTIN_HELP_LIST))
				continue;

			len = strlen((*builtin)->short_doc);
			/* If the length of SHORT_DOC is too long, truncate it.  */
			if (len > MAX_SHORT_DOC_LEN - 1)
				len = MAX_SHORT_DOC_LEN - 1;

			for (i = 0; i < len; i++)
				grub_putchar((*builtin)->short_doc[i]);

			for (; i < MAX_SHORT_DOC_LEN; i++)
				grub_putchar(' ');


			if (!left)
				grub_putchar('\n');

			left = !left;
		}

		/* If the last entry was at the left column, no newline was printed
		   at the end.  */
		if (!left)
			grub_putchar('\n');
	} else {
		/* Invoked with one or more patterns.  */
		do {
			struct builtin **builtin;
			char *next_arg;

			/* Get the next argument.  */
			next_arg = skip_to(0, arg);

			/* Terminate ARG.  */
			nul_terminate(arg);

			for (builtin = builtin_table; *builtin; builtin++) {
				/* Skip this if this is only for the configuration file.  */
				if (!((*builtin)->flags & BUILTIN_CMDLINE))
					continue;

				if (substring(arg, (*builtin)->name) < 1) {
					char *doc = (*builtin)->long_doc;

					/* At first, print the name and the short doc.  */
					grub_printf("%s: %s\n", (*builtin)->name, (*builtin)->short_doc);

					/* Print the long doc.  */
					while (*doc) {
						int len = strlen(doc);
						int i;

						/* If LEN is too long, fold DOC.  */
						if (len > MAX_LONG_DOC_LEN) {
							/* Fold this line at the position of a space.  */
							for (len = MAX_LONG_DOC_LEN; len > 0; len--)
								if (doc[len - 1] == ' ')
									break;
						}

						grub_putstr("    ");
						for (i = 0; i < len; i++)
							grub_putchar(*doc++);
						grub_putchar('\n');
					}
				}
			}

			arg = next_arg;
		}
		while (*arg);
	}
	refresh();
	return 0;
}

static struct builtin builtin_help = {
	"help",
	help_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"help [--all] [PATTERN ...]",
	"Display helpful information about builtin commands. Not all commands"
	    " aren't shown without the option `--all'."
};

/* hiddenmenu */
static int hiddenmenu_func(char *arg, int flags)
{
	show_menu = 0;
	return 0;
}

static struct builtin builtin_hiddenmenu = {
	"hiddenmenu",
	hiddenmenu_func,
	BUILTIN_MENU,
#if 0
	"hiddenmenu",
	"Hide the menu."
#endif
};

/**
 * @param arg         linux style driver specifier
 * @param drivername  driver name (out)
 * @param disk        disk number (out)
 * @return length of parsed string
 */
static
int parse_linux_style_driver(char *arg, char *drivername, int *disk)
{
	int i = 0;

	*disk = -1;
	drivername[0] = '\0';
	while ((i < 16) && (isalpha(arg[i]))) {
		drivername[i] = arg[i];
		i++;
	}

	if (i > 0) {
		drivername[--i] = '\0';
		*disk = arg[i]-'a';
		i++;
	}
	return i;
}

/**
 * @param arg  source pointer with grub device names
 * @param path destination pointer (will be filled with filo device names)
 * @param use_rootdev values other than zero mean the root device set by the "root"
 * command is taken into regard here. This has to be zero when calling from root_func.
 */

void copy_path_to_filo_bootline(char *arg, char *path, int use_rootdev, int append)
{
	char devicename[16];
	char drivername[16];
	int disk, part;
	unsigned long addr;
	int i;

	memset(devicename, 0, 16);
	memset(drivername, 0, 16);
	disk = -1;
	part = -1;
	addr = -1;

	if (arg[0] == '(') {
		// grub style device specifier
		i = 1;
		/* Read until we encounter a number, a comma or a closing
		 * bracket
		 */
		while ((i <= 16) && (isalpha(arg[i]))) {
			drivername[i - 1] = arg[i];
			i++;
		}

		if (isdigit(arg[i])) {
			char *postnum;
			disk = strtoul(arg+i, &postnum, 10);
			i = postnum - arg;
		}

		if (arg[i] == ',') {
			char *postnum;
			part = strtoul(arg+i+1, &postnum, 10) + 1;
			i = postnum - arg;
		}

		if (arg[i] == '@') {
			char *postnum;
			addr = strtoul(arg+i+1, &postnum, 0);
			i = postnum - arg;
		}

		if (arg[i] == ')') i++;

		arg += i;
	} else if ((use_rootdev == 0) || (strchr(arg, ':') != NULL)) {
		// linux-style device specifier or
		// leading device name required (assume it's linux-style then)
		i = parse_linux_style_driver(arg, drivername, &disk);

		if (isdigit(arg[i])) {
			char *postnum;
			part = strtoul(arg+i, &postnum, 10);
			i = postnum - arg;
		}

		if (arg[i] == '@') {
			char *postnum;
			addr = strtoul(arg+i+1, &postnum, 0);
			i = postnum - arg;
		}

		if (arg[i] == ':') i++;
		arg += i;
	}

	if ((disk == -1) && (part != -1) && (strlen(drivername) == 0)) {
		// special case for partition-only identifiers:
		// take driver and disk number from root_device
		i = parse_linux_style_driver(root_device, drivername, &disk);
	}

	if (!append) path[0] = 0;
	if ((use_rootdev == 1) && (strlen(drivername) == 0)) {
		strlcat(path, root_device, BOOT_LINE_LENGTH);
	} else {
		char buffer[32];
		strlcat(path, drivername, BOOT_LINE_LENGTH);
		if (disk != -1) {
			snprintf(buffer, 31, "%c", 'a'+disk);
			strlcat(path, buffer, BOOT_LINE_LENGTH);
		}
		if (part != -1) {
			snprintf(buffer, 31, "%d", part);
			strlcat(path, buffer, BOOT_LINE_LENGTH);
		}
		if (addr != -1) {
			snprintf(buffer, 31, "@0x%lx", addr);
			strlcat(path, buffer, BOOT_LINE_LENGTH);
		}
		buffer[0]=':';
		buffer[1]='\0';
		strlcat(path, buffer, BOOT_LINE_LENGTH);
	}
	strlcat(path, arg, BOOT_LINE_LENGTH);
}

/* initrd */
static int initrd_func(char *arg, int flags)
{
	copy_path_to_filo_bootline(arg, initrd_space, 1, 0);
	if (!file_open(initrd_space)) {
		initrd_space[0]=0; // Erase string
		errnum = ERR_FILE_NOT_FOUND;
		file_close();
		return 1;
	}

	file_close();
	return 0;
}

static struct builtin builtin_initrd = {
	"initrd",
	initrd_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"initrd FILE [ARG ...]",
	"Load an initial ramdisk FILE for a Linux format boot image and set the"
	    " appropriate parameters in the Linux setup area in memory."
};


#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_TARGET_I386
/* io */
static int io_func(char *arg, int flags)
{
	char *walk = arg;
	unsigned int port = 0;
	unsigned int value = 0;
	unsigned int write_mode = 0;
	unsigned int maxval=0xff, len=1;

	while ((*walk != 0) && (*walk != '.') && (*walk != '=')) {
		port *= 16;
		port += hex2bin(*walk);
		walk++;
	}
	if (port > 0xffff) {
		grub_printf("port too high\n");
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	if (*walk == '.') {
		walk++;
		switch (*walk) {
		case 'l':
		case 'L':
			len = 4;
			maxval = 0xffffffff;
			break;
		case 'w':
		case 'W':
			len=2;
			maxval = 0xffff;
			break;
		case 'b':
		case 'B':
			len=1;
			maxval = 0xff;
			break;
		default:
			grub_printf("width must be b, w, or l\n");
			errnum = ERR_BAD_ARGUMENT;
			return 1;
		}
		walk++;
	}

	if (*walk == '=') {
		while (*walk!=0 && *walk != '.') {
			value *= 16;
			value += hex2bin(*walk);
			walk++;
		}

		if (value > maxval) {
			grub_printf("value too big.\n");
			errnum = ERR_BAD_ARGUMENT;
			return 1;
		}

		write_mode = 1;
	}

	if (write_mode) {
		grub_printf ("out");
		switch (len) {
		case 1:
			grub_printf("b 0x%02x -> 0x%04x\n", value, port);
			outb(value, port);
			break;
		case 2:
			grub_printf("w 0x%04x -> 0x%04x\n", value, port);
			outw(value, port);
			break;
		case 4:
			grub_printf("l 0x%08x -> 0x%04x\n", value, port);
			outl(value, port);
			break;
		}
	} else {
		grub_printf ("in");
		switch (len) {
		case 1:
			value = inb(port);
			grub_printf("b 0x%04x: 0x%02x\n", port, value);
			break;
		case 2:
			value = inw(port);
			grub_printf("w 0x%04x: 0x%04x\n", port, value);
			break;
		case 4:
			value = inl(port);
			grub_printf("l 0x%04x: 0x%08x\n", port, value);
			break;
		}
	}

	return 0;
}

static struct builtin builtin_io = {
	"io",
	io_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"io port[.bwl][=val]",
	"Read/write IO ports."
};
#endif
#endif

/* kernel */
static int kernel_func(char *arg, int flags)
{
	int i;

	kernel_type = KERNEL_TYPE_NONE;

	/* Get the real boot line and extract the kernel name */
	copy_path_to_filo_bootline(arg, temp_space, 1, 0);
	i=0; while ((temp_space[i] != 0) && (temp_space[i]!=' ')) i++;
	temp_space[i] = 0;

	if (!file_open(temp_space)) {
		errnum = ERR_FILE_NOT_FOUND;
		file_close();
		return 1;
	}

	file_close();

	/* Needed to pass grub checks */
	kernel_type = KERNEL_TYPE_LINUX;

	copy_path_to_filo_bootline(arg, boot_line, 1, 0);

	return 0;
}

static struct builtin builtin_kernel = {
	"kernel",
	kernel_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"kernel [--no-mem-option] [--type=TYPE] FILE [ARG ...]",
	"Attempt to load the primary boot image from FILE. The rest of the"
	    " line is passed verbatim as the \"kernel command line\".  Any modules"
	    " must be reloaded after using this command. The option --type is used"
	    " to suggest what type of kernel to be loaded. TYPE must be either of"
	    " \"netbsd\", \"freebsd\", \"openbsd\", \"linux\", \"biglinux\" and"
	    " \"multiboot\". The option --no-mem-option tells GRUB not to pass a" " Linux's mem option automatically."
};

/* lock */
static int lock_func(char *arg, int flags)
{
	if (!auth && password) {
		errnum = ERR_PRIVILEGED;
		return 1;
	}

	return 0;
}

static struct builtin builtin_lock = {
	"lock",
	lock_func,
	BUILTIN_CMDLINE,
	"lock",
	"Break a command execution unless the user is authenticated."
};

#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_SUPPORT_PCI
static int lspci_indent = 0;
static void lspci_scan_bus(int bus)
{
	int slot, func;
	unsigned int val;
	unsigned char hdr;
	int i;

	for (slot = 0; slot < 0x20; slot++) {
		for (func = 0; func < 8; func++) {
			pcidev_t dev = PCI_DEV(bus, slot, func);

			val = pci_read_config32(dev, REG_VENDOR_ID);

			/* Nobody home. */
			if (val == 0xffffffff || val == 0x00000000 ||
			    val == 0x0000ffff || val == 0xffff0000)
				continue;

			for (i=0; i<lspci_indent; i++)
				grub_printf("|  ");
			grub_printf("|- %02x:%02x.%x [%04x:%04x]\n", bus, slot, func,
					val & 0xffff, val >> 16);

			/* If this is a bridge, then follow it. */
			hdr = pci_read_config8(dev, REG_HEADER_TYPE);
			hdr &= 0x7f;
			if (hdr == HEADER_TYPE_BRIDGE ||
			    hdr == HEADER_TYPE_CARDBUS) {
				unsigned int busses;

				busses = pci_read_config32(dev, REG_PRIMARY_BUS);
				lspci_indent++;
				lspci_scan_bus((busses >> 8) & 0xff);
				lspci_indent--;
			}
		}
	}
}

static void lspci_configspace(pcidev_t dev)
{
	unsigned char cspace[256];
	int i, x, y;

	for (i = 0; i < 256; i ++)
		cspace[i] = pci_read_config8(dev, i);

	for (y = 0; y < 16; y++) {
		grub_printf("%x0:", y);
		for (x = 0; x < 16; x++)
			grub_printf(" %02x", cspace[(y * 16) + x]);
		grub_printf("\n");
	}

	grub_printf("\n");
}

static int lspci_func(char *arg, int flags)
{
	char *walk = arg;
	int bus, slot, fn;

	if(strlen(walk)) {
		pcidev_t dev;

		if((walk[1] != ':') && (walk[2] =! ':'))
			goto out;
		if(walk[1] == ':') {
			bus = hex2bin(walk[0]);
			walk+=2;
		} else {
			bus = (hex2bin(walk[0]) * 16) + hex2bin(walk[1]);
			walk+=3;
		}
		if((walk[1] != '.') && (walk[2] =! '.'))
			goto out;

		if(walk[1] == '.') {
			slot = hex2bin(walk[0]);
			walk+=2;
		} else {
			slot = (hex2bin(walk[0]) * 16) + hex2bin(walk[1]);
			walk+=3;
		}
		if (!walk[0])
			goto out;

		fn=hex2bin(walk[0]);

		grub_printf("Dumping %x:%x.%x\n", bus, slot, fn);

		dev = PCI_DEV(bus, slot, fn);
		lspci_configspace(dev);
		return 0;
	}
out:
	lspci_scan_bus(0);
	return 0;
}

static struct builtin builtin_lspci = {
	"lspci",
	lspci_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"lspci <device>",
	"Show PCI devices or dump PCI config space"
};
#endif
#endif

#ifdef CONFIG_USE_MD5_PASSWORDS
/* md5crypt */
static int md5crypt_func(char *arg, int flags)
{
	char crypted[36];
	char key[32];
	unsigned int seed;
	int i;
	const char *const seedchars = "./0123456789ABCDEFGHIJKLMNOPQRST" "UVWXYZabcdefghijklmnopqrstuvwxyz";

	/* First create a salt.  */

	/* The magical prefix.  */
	memset(crypted, 0, sizeof(crypted));
	memmove(crypted, "$1$", 3);

	/* Create the length of a salt.  */
	seed = currticks();

	/* Generate a salt.  */
	for (i = 0; i < 8 && seed; i++) {
		/* FIXME: This should be more random.  */
		crypted[3 + i] = seedchars[seed & 0x3f];
		seed >>= 6;
	}

	/* A salt must be terminated with `$', if it is less than 8 chars.  */
	crypted[3 + i] = '$';

#ifdef CONFIG_DEBUG_MD5CRYPT
	grub_printf("salt = %s\n", crypted);
#endif

	/* Get a password.  */
	memset(key, 0, sizeof(key));
	get_cmdline("Password: ", key, sizeof(key) - 1, '*', 0);

	/* Crypt the key.  */
	make_md5_password(key, crypted);

	grub_printf("Encrypted: %s\n", crypted);
	return 0;
}

static struct builtin builtin_md5crypt = {
	"md5crypt",
	md5crypt_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"md5crypt",
	"Generate a password in MD5 format."
};
#endif				/* CONFIG_USE_MD5_PASSWORDS */

#if CONFIG_DEVELOPER_TOOLS
#if CONFIG_TARGET_I386
/* nvram */
static int nvram_func(char *arg, int flags)
{
#define RTC_BOOT_BYTE 48 // Hard coded in coreboot
	u8 rtc_boot_byte;
	// bit len  name
	//  0   1   boot_option
	//  1   1   last_boot
	//  4   4   reboot_bits

	rtc_boot_byte = nvram_read(RTC_BOOT_BYTE);

	if (memcmp(arg, "normal", 6) == 0) {
		rtc_boot_byte &= 0x03;	// drop reboot_bits
		rtc_boot_byte |= 1;	// normal
		nvram_write(rtc_boot_byte, RTC_BOOT_BYTE);
		return 0;
	}

	if (memcmp(arg, "fallback", 8) == 0) {
		rtc_boot_byte &= 0x03;	// drop reboot_bits
		rtc_boot_byte &= ~1;	// fallback
		nvram_write(rtc_boot_byte, RTC_BOOT_BYTE);
		return 0;
	}

	// TODO not really default, but rather "null everything out and fix the
	// checksum"
	if (memcmp(arg, "default", 7) == 0) {
		int i;
		int range_start = lib_sysinfo.cmos_range_start / 8;
		int range_end = lib_sysinfo.cmos_range_end / 8;
		for (i= range_start; i<range_end; i++)
			nvram_write(0, i);
		fix_options_checksum();
		return 0;
	}


	if (!strlen(arg)) {
		grub_printf("NVRAM Settings:\n");
		grub_printf("  boot option: %s\n",
			(rtc_boot_byte & (1 << 0)) ? "Normal" : "Fallback");
		grub_printf("  last boot:   %s\n",
			(rtc_boot_byte & (1 << 1)) ? "Normal" : "Fallback");
		grub_printf("  reboot_bits: %d\n", (rtc_boot_byte >> 4));
		return 0;
	}

	errnum = ERR_BAD_ARGUMENT;
	return 1;
}

static struct builtin builtin_nvram = {
	"nvram",
	nvram_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_NO_ECHO,
	"nvram [normal|fallback]",
	"Change the coreboot nvram to boot the normal or fallback"
	    "image on the next boot."
};
#endif
#endif

/* password */
static int password_func(char *arg, int flags)
{
	int len;
	password_t type = PASSWORD_PLAIN;

#ifdef CONFIG_USE_MD5_PASSWORDS
	if (memcmp(arg, "--md5", 5) == 0) {
		type = PASSWORD_MD5;
		arg = skip_to(0, arg);
	}
#endif
	if (memcmp(arg, "--", 2) == 0) {
		type = PASSWORD_UNSUPPORTED;
		arg = skip_to(0, arg);
	}

	if ((flags & (BUILTIN_CMDLINE | BUILTIN_SCRIPT)) != 0) {
		/* Do password check! */
		char entered[32];

		/* Wipe out any previously entered password */
		entered[0] = 0;
		get_cmdline("Password: ", entered, 31, '*', 0);

		nul_terminate(arg);
		if (check_password(entered, arg, type) != 0) {
			errnum = ERR_PRIVILEGED;
			return 1;
		}
	} else {
		len = strlen(arg);

		/* PASSWORD NUL NUL ... */
		if (len + 2 > PASSWORD_BUFLEN) {
			errnum = ERR_WONT_FIT;
			return 1;
		}

		/* Copy the password and clear the rest of the buffer.  */
		password = (char *) PASSWORD_BUF;
		memmove(password, arg, len);
		memset(password + len, 0, PASSWORD_BUFLEN - len);
		password_type = type;
	}
	return 0;
}

static struct builtin builtin_password = {
	"password",
	password_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_NO_ECHO,
	"password [--md5] PASSWD [FILE]",
	"If used in the first section of a menu file, disable all"
	    " interactive editing control (menu entry editor and"
	    " command line). If the password PASSWD is entered, it loads the"
	    " FILE as a new config file and restarts the GRUB Stage 2. If you"
	    " omit the argument FILE, then GRUB just unlocks privileged"
	    " instructions.  You can also use it in the script section, in"
	    " which case it will ask for the password, before continueing."
	    " The option --md5 tells GRUB that PASSWD is encrypted with" " md5crypt."
};

/* pause */
static int pause_func(char *arg, int flags)
{
	grub_printf("%s\n", arg);

	/* If ESC is returned, then abort this entry.  */
	if (ASCII_CHAR(getkey()) == 27)
		return 1;

	return 0;
}

static struct builtin builtin_pause = {
	"pause",
	pause_func,
	BUILTIN_CMDLINE | BUILTIN_NO_ECHO,
	"pause [MESSAGE ...]",
	"Print MESSAGE, then wait until a key is pressed."
};

static int poweroff_func(char *arg, int flags)
{
	void __attribute__((weak)) platform_poweroff(void);
	if (platform_poweroff)
		platform_poweroff();
	else
		grub_printf("Poweroff not supported.\n");

	// Will (hopefully) never return;
	return 0;
}

static struct builtin builtin_poweroff = {
	"poweroff",
	poweroff_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"poweroff",
	"Power off the system."
};

#ifdef CONFIG_DEVELOPER_TOOLS
static int probe_func(char *arg, int flags)
{
#if CONFIG_IDE_DISK
	int i;

	for (i=0; i<8; i++)
		ide_probe(i);
#elif CONFIG_IDE_NEW_DISK
	int i;

	for (i=0; i<8; i++)
		ide_probe_verbose(i);
#else
	grub_printf("No IDE driver.\n");
#endif

	return 0;
}

static struct builtin builtin_probe = {
	"probe",
	probe_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"probe",
	"Probe IDE drives"
};
#endif

static int reboot_func(char *arg, int flags)
{
	void __attribute__((weak)) platform_reboot(void);

	if (platform_reboot)
		platform_reboot();
	else
		grub_printf("Reboot not supported.\n");

	// Will (hopefully) never return;
	return 0;
}

static struct builtin builtin_reboot = {
	"reboot",
	reboot_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"reboot",
	"Reboot the system."
};

static int root_func(char *arg, int flags)
{
	int len;

	copy_path_to_filo_bootline(arg, root_device, 0, 0);

	/* The following code handles an extra case
	 * where the user specifies "root hde1" without
	 * a trailing colon.
	 */
	len=strlen(root_device);
	if(root_device[len - 1] != ':') {
		root_device[len] = ':';
		root_device[len + 1] = 0;
	}

	return 0;
}

static struct builtin builtin_root = {
	"root",
	root_func,
	BUILTIN_CMDLINE | BUILTIN_MENU | BUILTIN_HELP_LIST,
	"root [DEVICE]",
	"Set the current \"root device\" to the device DEVICE."
};

void __attribute__((weak))  serial_hardware_init(int port, int speed, int
		word_bits, int parity, int stop_bits);

/* serial */
static int serial_func(char *arg, int flags)
{
	unsigned short serial_port[] = {0x3f8, 0x2f8, 0x3e8, 0x2e8 };
	unsigned short port = 0x3f8;
	unsigned int speed = 9600;
	int word_len = 8;
	int parity = 0;
	int stop_bit_len = 1;

	/* Process GNU-style long options.
	   FIXME: We should implement a getopt-like function, to avoid
	   duplications.  */
	while (1) {
		if (memcmp(arg, "--unit=", sizeof("--unit=") - 1) == 0) {
			char *p = arg + sizeof("--unit=") - 1;
			int unit;

			if (!safe_parse_maxint(&p, &unit))
				return 1;

			if (unit < 0 || unit > 3) {
				errnum = ERR_DEV_VALUES;
				return 1;
			}

			port = serial_port[unit];
		} else if (memcmp(arg, "--speed=", sizeof("--speed=") - 1) == 0) {
			char *p = arg + sizeof("--speed=") - 1;
			int num;

			if (!safe_parse_maxint(&p, &num))
				return 1;

			speed = (unsigned int) num;
		} else if (memcmp(arg, "--port=", sizeof("--port=") - 1)
			   == 0) {
			char *p = arg + sizeof("--port=") - 1;
			int num;

			if (!safe_parse_maxint(&p, &num))
				return 1;

			port = (unsigned short) num;
		} else if (memcmp(arg, "--word=", sizeof("--word=") - 1)
			   == 0) {
			char *p = arg + sizeof("--word=") - 1;
			int len;

			if (!safe_parse_maxint(&p, &len))
				return 1;

			switch (len) {
			case 5 ... 8:
				word_len = len;
				break;
			default:
				errnum = ERR_BAD_ARGUMENT;
				return 1;
			}
		} else if (memcmp(arg, "--stop=", sizeof("--stop=") - 1)
			   == 0) {
			char *p = arg + sizeof("--stop=") - 1;
			int len;

			if (!safe_parse_maxint(&p, &len))
				return 1;

			switch (len) {
			case 1 ... 2:
				stop_bit_len = len;
				break;
			default:
				errnum = ERR_BAD_ARGUMENT;
				return 1;
			}
		} else if (memcmp(arg, "--parity=", sizeof("--parity=") - 1) == 0) {
			char *p = arg + sizeof("--parity=") - 1;

			if (memcmp(p, "no", sizeof("no") - 1) == 0)
				parity = 0;
			else if (memcmp(p, "odd", sizeof("odd") - 1)
				 == 0)
				parity = 1;
			else if (memcmp(p, "even", sizeof("even") - 1)
				 == 0)
				parity = 2;
			else {
				errnum = ERR_BAD_ARGUMENT;
				return 1;
			}
		} else
			break;

		arg = skip_to(0, arg);
	}

	/* Initialize the serial unit.  */
	if (serial_hardware_init)
		serial_hardware_init(port, speed, word_len, parity, stop_bit_len);
	else
		grub_printf("This version of FILO does not have serial console support.\n");

	return 0;
}

static struct builtin builtin_serial = {
	"serial",
	serial_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"serial [--unit=UNIT] [--port=PORT] [--speed=SPEED] [--word=WORD] [--parity=PARITY] [--stop=STOP] [--device=DEV]",
	"Initialize a serial device. UNIT is a digit that specifies which serial"
	    " device is used (e.g. 0 == COM1). If you need to specify the port number,"
	    " set it by --port. SPEED is the DTE-DTE speed. WORD is the word length,"
	    " PARITY is the type of parity, which is one of `no', `odd' and `even'."
	    " STOP is the length of stop bit(s). The option --device can be used only"
	    " in the grub shell, which specifies the file name of a tty device. The"
	    " default values are COM1, 9600, 8N1."
};

/* initialize sbs */
static int
sbs_init_func(char *arg, int flags)
{
	grub_printf("SBS - overriding CSL file ops...\n");
	csl_fs_ops.open  = sbs_open;
	csl_fs_ops.read  = sbs_read;
	csl_fs_ops.size  = sbs_size;
	csl_fs_ops.close = sbs_close;

	return 0;
}

static struct builtin builtin_sbs_init =
{
	"sbs_init",
	sbs_init_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"sbs_init",
	"Initialize Signed Block Stream (SBS) processing."
};

#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_SUPPORT_PCI
static int setpci_func(char *arg, int flags)
{
	char *walk = arg;
	int bus, slot, fn;
	pcidev_t dev;
	unsigned int reg=0;
	unsigned int len=1, maxval=0xff, value=0;
	int write_mode = 0;

	// setpci bus:dev.fn reg.[bwl][=val]

	if(!strlen(arg)) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	if((walk[1] != ':') && (walk[2] =! ':')) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	if(walk[1] == ':') {
		bus = hex2bin(walk[0]);
		walk+=2;
	} else {
		bus = (hex2bin(walk[0]) * 16) + hex2bin(walk[1]);
		walk+=3;
	}
	if((walk[1] != '.') && (walk[2] =! '.')) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	if(walk[1] == '.') {
		slot = hex2bin(walk[0]);
		walk+=2;
	} else {
		slot = (hex2bin(walk[0]) * 16) + hex2bin(walk[1]);
		walk+=3;
	}
	if (!walk[0]) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	fn=hex2bin(walk[0]);

	dev = PCI_DEV(bus, slot, fn);

	walk++;
	if (walk[0] != ' ') {
		grub_printf("No register specified\n");
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	while (*walk!=0 && *walk != '.' && *walk != ':' ) {
		reg *= 16;
		reg += hex2bin(*walk);
		walk++;
	}

	if (reg > 0xff) {
		grub_printf("Only 256 byte config space supported.\n");
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}

	if (*walk == '.') {
		walk++;
		switch (*walk) {
		case 'l':
		case 'L':
			len = 4;
			maxval = 0xffffffff;
			break;
		case 'w':
		case 'W':
			len=2;
			maxval = 0xffff;
			break;
		case 'b':
		case 'B':
			len=1;
			maxval = 0xff;
			break;
		default:
			grub_printf("width must be b, w, or l\n");
			errnum = ERR_BAD_ARGUMENT;
			return 1;
		}
		walk++;
	}

	if (*walk == '=') {
		while (*walk!=0 && *walk != '.') {
			value *= 16;
			value += hex2bin(*walk);
			walk++;
		}

		if (value > maxval) {
			grub_printf("value too big.\n");
			errnum = ERR_BAD_ARGUMENT;
			return 1;
		}

		write_mode = 1;
	}

	if (write_mode) {
		grub_printf ("pci_write_config");
		switch (len) {
		case 1:
			grub_printf("8 0x%02x -> %x:%x.%x [%02x]\n", value, bus, slot, fn, reg);
			pci_write_config8(dev, reg, value);
			break;
		case 2:
			grub_printf("16 0x%04x -> %x:%x.%x [%02x]\n", value, bus, slot, fn, reg);
			pci_write_config16(dev, reg, value);
			break;
		case 4:
			grub_printf("32 0x%08x -> %x:%x.%x [%02x]\n", value, bus, slot, fn, reg);
			pci_write_config32(dev, reg, value);
			break;
		}
	} else {
		grub_printf ("pci_read_config");
		switch (len) {
		case 1:
			value = pci_read_config8(dev, reg);
			grub_printf("8 %x:%x.%x [%02x] -> %02x\n", bus, slot, fn, reg, value);
			break;
		case 2:
			value = pci_read_config16(dev, reg);
			grub_printf("16 %x:%x.%x [%02x] -> %04x\n", bus, slot, fn, reg, value);
			break;
		case 4:
			value = pci_read_config32(dev, reg);
			grub_printf("32 %x:%x.%x [%02x] -> %08x\n", bus, slot, fn, reg, value);
			break;
		}
	}

	return 0;
}

static struct builtin builtin_setpci = {
	"setpci",
	setpci_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"setpci <device>[.bwl][=value]",
	"Show/change PCI config space values"
};
#endif
#endif

/* terminal */
static int terminal_func(char *arg, int flags)
{
	int use_serial = 0, use_vga = 0;
	int terminal_changed = 0;
	/* The index of the default terminal in TERM_TABLE.  */
	int lines = 0;
	unsigned long term_flags = 0;

	/* Get GNU-style long options.  */
	while (1) {
		if (memcmp(arg, "--no-echo", sizeof("--no-echo") - 1) == 0) {
			/* ``--no-echo'' implies ``--no-edit''.  */
			term_flags |= (TERM_NO_ECHO | TERM_NO_EDIT);
		} else if (memcmp(arg, "--no-edit", sizeof("--no-edit") - 1) == 0) {
			term_flags |= TERM_NO_EDIT;
		} else if (memcmp(arg, "--lines=", sizeof("--lines=") - 1) == 0) {
			char *val = arg + sizeof("--lines=") - 1;

			if (!safe_parse_maxint(&val, &lines))
				return 1;

			/* Probably less than four is meaningless....  */
			if (lines < 4) {
				errnum = ERR_BAD_ARGUMENT;
				return 1;
			}
		} else {
			while (*arg) {
				char *next = skip_to(0, arg);

				nul_terminate(arg);

				/* We also accept "terminal console" as GRUB
				 * heritage.
				 */
				if (strcmp(arg, "serial") == 0) {
					use_serial = 1;
					terminal_changed = 1;
				} else if (strcmp(arg, "console") == 0) {
					use_vga = 1;
					terminal_changed = 1;
				} else if (strcmp(arg, "vga") == 0) {
					use_vga = 1;
					terminal_changed = 1;
				} else {
					errnum = ERR_BAD_ARGUMENT;
					return 1;
				}

				arg = next;
				break;
			}
			if (!*arg)
				break;
			continue;
		}

		arg = skip_to(0, arg);
	}

	if (terminal_changed) {
		curses_enable_serial(use_serial);
		curses_enable_vga(use_vga);
		terminal_flags = term_flags;
	}

	if (lines)
		max_lines = lines;
	else
		/* 25 would be a good default value.  */
		max_lines = 25;

	/* If no argument is specified, show current setting.  */
	if (! *arg) {
		grub_printf("Serial console terminal %s.\n",
				curses_serial_enabled()?"enabled":"disabled");
		grub_printf("VGA console terminal %s.\n",
				curses_vga_enabled()?"enabled":"disabled");
		grub_printf("Flags:%s%s\n",
			    terminal_flags & TERM_NO_EDIT ? " (no edit)" : "",
			    terminal_flags & TERM_NO_ECHO ? " (no echo)" : "");
	}

	return 0;
}

static struct builtin builtin_terminal = {
	"terminal",
	terminal_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"terminal [--no-echo] [--no-edit] [--timeout=SECS] [--lines=LINES] [--silent] [console] [serial]",
	"Select a terminal. When multiple terminals are specified, wait until"
	    " you push any key to continue. If both console and serial are specified,"
	    " the terminal to which you input a key first will be selected. If no"
	    " argument is specified, print current setting. If you specify --no-echo,"
	    " input characters won't be echoed."
	    " If you specify --no-edit, the BASH-like editing feature will be disabled."
	    " If --timeout is present, this command will wait at most for SECS"
	    " seconds. The option --lines specifies the maximum number of lines."
};

/* timeout */
static int timeout_func(char *arg, int flags)
{
	if (!safe_parse_maxint(&arg, &grub_timeout))
		return 1;

	return 0;
}

static struct builtin builtin_timeout = {
	"timeout",
	timeout_func,
	BUILTIN_MENU,
#if 0
	"timeout SEC",
	"Set a timeout, in SEC seconds, before automatically booting the"
	    " default entry (normally the first entry defined)."
#endif
};


static int keymap_func(char *arg, int flags)
{
#if IS_ENABLED(CONFIG_LP_PC_KEYBOARD)
	if (keyboard_set_layout(arg)) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}
	return 0;
#else
	errnum = ERR_BAD_ARGUMENT;
	return 1;
#endif
}

static struct builtin builtin_keymap = {
	"keymap",
	keymap_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"keymap LANGCODE",
	"Select a keymap to use. Currently only 'us' and 'de' are supported."
};

static int title_func(char *arg, int flags)
{
	/* This function is not actually used at least currently.  */
	return 0;
}

static struct builtin builtin_title = {
	"title",
	title_func,
	BUILTIN_TITLE,
#if 0
	"title [NAME ...]",
	"Start a new boot entry, and set its name to the contents of the"
	    " rest of the line, starting with the first non-space character."
#endif
};

static int cat_func(char *arg, int flags)
{
	char buf[4096];
	int len;

	copy_path_to_filo_bootline(arg, temp_space, 1, 0);
	if (temp_space[0]==0) {
		return help_func("cat",0);
	}
	if (!file_open(temp_space)) {
		errnum = ERR_FILE_NOT_FOUND;
		return 1;
	}

	while ((len = file_read(buf, sizeof(buf))) != 0) {
		int cnt;
		for (cnt = 0; cnt < len; cnt++) {
			grub_putchar(buf[cnt]);
		}
	}

	file_close();

	return 0;
}

static struct builtin builtin_cat = {
	"cat",
	cat_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"cat FILENAME",
	"Print the content of FILENAME to the terminal."
};

/* README !!! XXX !!! This list has to be alphabetically ordered !!! */

struct builtin *builtin_table[] = {
	&builtin_boot,
	&builtin_cat,
	&builtin_color,
	&builtin_configfile,
	&builtin_default,
#ifdef CONFIG_DEVELOPER_TOOLS
	&builtin_dumpmem,
#ifdef CONFIG_TARGET_I386
	&builtin_dumppm,
#endif
#endif
#ifdef CONFIG_EXPERIMENTAL
	&builtin_find,
#endif
#ifdef CONFIG_FLASHROM_UNLOCK
	&builtin_flashrom_unlock,
#endif
#ifdef CONFIG_FLASHUPDATE
	&builtin_flashupdate,
#endif
	&builtin_help,
	&builtin_hiddenmenu,
	&builtin_initrd,
#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_TARGET_I386
	&builtin_io,
#endif
#endif
	&builtin_kernel,
	&builtin_keymap,
	&builtin_lock,
#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_SUPPORT_PCI
	&builtin_lspci,
#endif
#endif
#ifdef CONFIG_USE_MD5_PASSWORDS
	&builtin_md5crypt,
#endif
#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_TARGET_I386
	&builtin_nvram,
#endif
#endif
	&builtin_password,
	&builtin_pause,
	&builtin_poweroff,
#ifdef CONFIG_DEVELOPER_TOOLS
	&builtin_probe,
#endif
	&builtin_reboot,
	&builtin_root,
	&builtin_sbs_init,
	&builtin_serial,
#ifdef CONFIG_DEVELOPER_TOOLS
#ifdef CONFIG_SUPPORT_PCI
	&builtin_setpci,
#endif
#endif
	&builtin_terminal,
	&builtin_timeout,
	&builtin_title,
	0
};
