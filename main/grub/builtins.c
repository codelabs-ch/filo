/*
 * This file is part of FILO.
 *
 *  Copyright (C) 1999,2000,2001,2002,2004  Free Software Foundation, Inc.
 *  Copyright (C) 2005-2008 coresystems GmbH
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


#include <libpayload.h>
#include <config.h>
#include <fs.h>
#include <grub/shared.h>
#include <arch/timer.h>
#ifdef CONFIG_USE_MD5_PASSWORDS
#include <grub/md5.h>
#endif

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
int saved_entryno = 0;
char config_file[128] = "\0";

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

/* Initialize the data for builtins.  */
void init_builtins(void)
{
	kernel_type = KERNEL_TYPE_NONE;
}

/* Initialize the data for the configuration file.  */
void init_config(void)
{
	default_entry = 0;
	password = NULL;
	fallback_entryno = -1;
	fallback_entries[0] = -1;
	grub_timeout = -1;
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

/* boot */
static int boot_func(char *arg, int flags)
{
	void boot(const char *line);

	if(!boot_line[0]) {
		grub_printf("No kernel.\n");
		return 1;
	}

	cls();

	if (initrd_space[0]) {
		strncat(boot_line, " initrd=", BOOT_LINE_LENGTH);
		strncat(boot_line, initrd_space, BOOT_LINE_LENGTH);
	}

	grub_printf("\nBooting '%s'\n", boot_line);
	boot(boot_line);
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

	/* Convert the color name STR into the magical number.  */
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

		/* Search for the color name.  */
		for (i = 0; i < 16; i++)
			if (strcmp(color_list[i], str) == 0) {
				color |= (i << 4);
				break;
			}

		if (i == 16)
			return -1;

		str = ptr;
		nul_terminate(str);

		/* Search for the color name.  */
		for (i = 0; i < 16; i++)
			if (strcmp(color_list[i], str) == 0) {
				color |= i;
				break;
			}

		if (i == 16)
			return -1;

		return color;
	}

	normal = arg;
	highlight = skip_to(0, arg);

	new_normal_color = color_number(normal);
	if (new_normal_color < 0 && !safe_parse_maxint(&normal, &new_normal_color))
		return 1;

	/* The second argument is optional, so set highlight_color to inverted NORMAL_COLOR.  */
	if (!*highlight)
		new_highlight_color = ((new_normal_color >> 4)
				       | ((new_normal_color & 0xf) << 4));
	else {
		new_highlight_color = color_number(highlight);
		if (new_highlight_color < 0 && !safe_parse_maxint(&highlight, &new_highlight_color))
			return 1;
	}

	console_setcolor(new_normal_color, new_highlight_color);

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

/* configfile */
static int configfile_func(char *arg, int flags)
{
	extern int is_opened, keep_cmdline_running;

	/* Check if the file ARG is present.  */
	temp_space[0]=0;
	copy_path_to_filo_bootline(arg, temp_space, 1);
	if (!file_open(temp_space)) {
		errnum = ERR_FILE_NOT_FOUND;
		return 1;
	}

	file_close();

	/* Copy ARG to CONFIG_FILE.  */
	memset(config_file, 0, 128);
	copy_path_to_filo_bootline(arg, config_file, 1);

	/* Force to load the configuration file.  */
	is_opened = 0;
	keep_cmdline_running = 0;

	/* Make sure that the user will not be authoritative.  */
	auth = 0;

	return 0;
}

static struct builtin builtin_configfile = {
	"configfile",
	configfile_func,
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"configfile FILE",
	"Load FILE as the configuration file."
};

/* default */
static int default_func(char *arg, int flags)
{
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

						grub_printf("    ");
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
 * @param arg  source pointer with grub device names
 * @param path destination pointer (will be filled with filo device names)
 * @param use_rootdev values other than zero mean the root device set by the "root"
 * command is taken into regard here. This has to be zero when calling from root_func.
 */

void copy_path_to_filo_bootline(char *arg, char *path, int use_rootdev)
{
	char devicename[16];
	char drivername[16];
	int disk, part;
	int i, len;


	/* Clean up */
	memset(devicename, 0, 16);
	memset(drivername, 0, 16);

	/* Copy over the driver name: "hd", "ud", "sd" ... */
	if (arg[0] == '(') {
		i = 1;
		/* Read until we encounter a number, a comma or a closing
		 * bracket
		 */
		while ((i <= 16) && (arg[i]) && (!isdigit(arg[i])) && (arg[i] != ',')
		       && (arg[i] != ')')) {
			drivername[i - 1] = arg[i];
			i++;
		}
	}

	disk = -1;
	part = -1;

	len = strlen(drivername);
	if (len) {		/* We have a driver. No idea if it exists though */
		// The driver should decide this:
		len++;		// skip driver name + opening bracket

		// XXX put @ handling in here, too for flash@addr and mem@addr

		if (isdigit(arg[len])) {
			disk = arg[len] - '0';
			len++;
			if (isdigit(arg[len])) {	/* More than 9 drives? */
				/* ok, get one more number. No more than 99 drives */
				disk *= 10;
				disk += arg[len] - '0';
				len++;
			}
		}
		if (arg[len] == ',') {
			len++;
			part = arg[len] - '0';
			len++;
			if (isdigit(arg[len])) {	/* More than 9 partitions? */
				/* ok, get one more number. No more than 99
				 * partitions */
				part *= 10;
				part += arg[len] - '0';
				len++;
			}
		}
		if (arg[len] != ')') {
			grub_printf("Drive Error.\n");
			// set len = 0 --> just copy the drive name 
			len = 0;
		} else {
			len++;	// skip closing bracket
		}
	}

	if (disk == -1) {
		/* The user did specify a FILO name already */
		// grub_printf("No drive.\n");
		len = 0;	// just copy the drive name
	} else {
		if (part == -1) {	// No partition
			sprintf(devicename, "%s%c:", drivername, disk + 'a');
		} else {	// both disk and partition
			sprintf(devicename, "%s%c%d:", drivername, disk + 'a', part + 1);
		}
		strncat(path, devicename, BOOT_LINE_LENGTH);
		arg += len;	// skip original drive name
	}

	if (use_rootdev && !len) {	// No drive was explicitly specified
		if (strlen(root_device)) {	// But someone set a root device
			strncat(path, root_device, BOOT_LINE_LENGTH);
		}
	}

	/* Copy the rest over */
	strncat(path, arg, BOOT_LINE_LENGTH);
}

/* initrd */
static int initrd_func(char *arg, int flags)
{
	initrd_space[0]=0; // Erase string
	copy_path_to_filo_bootline(arg, initrd_space, 1);
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




/* kernel */
static int kernel_func(char *arg, int flags)
{
	int i;

	kernel_type = KERNEL_TYPE_NONE;

	/* clear out boot_line. Kernel is the first thing */
	boot_line[0] = 0;  // Erase string

	/* Get the real boot line and extract the kernel name */
	temp_space[0] = 0; // Erase string
	copy_path_to_filo_bootline(arg, temp_space, 1);
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

	copy_path_to_filo_bootline(arg, boot_line, 1);

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

void __attribute__((weak)) platform_poweroff(void)
{
	grub_printf("Poweroff not supported.\n");
}

static int poweroff_func(char *arg, int flags)
{
	platform_poweroff();

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

static int reboot_func(char *arg, int flags)
{
	void platform_reboot(void);

	platform_reboot();

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

	root_device[0] = 0; /* Clear root device */
	copy_path_to_filo_bootline(arg, root_device, 0);

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
	BUILTIN_CMDLINE | BUILTIN_HELP_LIST,
	"root [DEVICE]",
	"Set the current \"root device\" to the device DEVICE."
};

void __attribute__((weak))  serial_hardware_init(int port, int speed, int word_bits, int parity, int stop_bits)
{
	grub_printf("This version of FILO does not have serial console support.\n");
}

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
	serial_hardware_init(port, speed, word_len, parity, stop_bit_len);

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
	if (keyboard_set_layout(arg)) {
		errnum = ERR_BAD_ARGUMENT;
		return 1;
	}
	return 0;
}

static struct builtin builtin_keymap = {
	"keymap",
	keymap_func,
	BUILTIN_MENU | BUILTIN_CMDLINE | BUILTIN_HELP_LIST | BUILTIN_NO_ECHO,
	"keymap LANGCODE",
	"Select a keymap to use. Currently only 'en' and 'de' are supported."
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


/* README !!! XXX !!! This list has to be alphabetically ordered !!! */

struct builtin *builtin_table[] = {
	&builtin_boot,
	&builtin_color,
	&builtin_configfile,
	&builtin_default,
#ifdef CONFIG_EXPERIMENTAL
	&builtin_find,
#endif
	&builtin_help,
	&builtin_hiddenmenu,
	&builtin_initrd,
	&builtin_kernel,
	&builtin_keymap,
	&builtin_lock,
#ifdef CONFIG_USE_MD5_PASSWORDS
	&builtin_md5crypt,
#endif
	&builtin_password,
	&builtin_pause,
	&builtin_poweroff,
	&builtin_reboot,
	&builtin_root,
	&builtin_serial,
	&builtin_terminal,
	&builtin_timeout,
	&builtin_title,
	0
};
