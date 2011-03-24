/* shared.h - definitions used in all GRUB-specific code */
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

#ifndef GRUB_SHARED_HEADER
#define GRUB_SHARED_HEADER

/*
 *  Integer sizes
 */

#define MAXINT     0x7FFFFFFF

/* Maximum command line size. Before you blindly increase this value,
   see the comment in char_io.c (get_cmdline).  */
#define MAX_CMDLINE 1600
#define NEW_HEAPSIZE 1500

/* The buffer for the password.  */
extern char PASSWORD_BUF[];
#define PASSWORD_BUFLEN		0x200

/* THe buffer for the filename of "/boot/grub/default".  */
extern char DEFAULT_FILE_BUF[];
#define DEFAULT_FILE_BUFLEN	0x60

/* The buffer for the command-line.  */
extern char CMDLINE_BUF[];
#define CMDLINE_BUFLEN		MAX_CMDLINE

/* The history buffer for the command-line.  */
extern char HISTORY_BUF[];
#define HISTORY_SIZE		5
#define HISTORY_BUFLEN		(MAX_CMDLINE * HISTORY_SIZE)

/* The buffer for the completion.  */
extern char COMPLETION_BUF[];
#define COMPLETION_BUFLEN	MAX_CMDLINE

/* The buffer for the unique string.  */
extern char UNIQUE_BUF[];
#define UNIQUE_BUFLEN		MAX_CMDLINE

/* The buffer for the menu entries.  */
extern char MENU_BUF[];
#define MENU_BUFLEN		0x1000

/* The kill buffer */
extern char KILL_BUF[];
#define KILL_BUFLEN		MAX_CMDLINE

/*
 *  GRUB specific information
 *    (in LSB order)
 */

#define GRUB_INVALID_DRIVE      0xFF

/* Codes for getchar. */
#define ASCII_CHAR(x)   ((x) & 0xFF)

# include <curses.h>

/* Special graphics characters for IBM displays. */
#define DISP_UL		218
#define DISP_UR		191
#define DISP_LL		192
#define DISP_LR		217
#define DISP_HORIZ	196
#define DISP_VERT	179
#define DISP_LEFT	0x1b
#define DISP_RIGHT	0x1a
#define DISP_UP		0x18
#define DISP_DOWN	0x19

/* Error codes (descriptions are in common.c) */
typedef enum
{
  ERR_NONE = 0,
  ERR_BAD_FILENAME,
  ERR_BAD_FILETYPE,
  ERR_BAD_GZIP_DATA,
  ERR_BAD_GZIP_HEADER,
  ERR_BAD_PART_TABLE,
  ERR_BAD_VERSION,
  ERR_BELOW_1MB,
  ERR_BOOT_COMMAND,
  ERR_BOOT_FAILURE,
  ERR_BOOT_FEATURES,
  ERR_DEV_FORMAT,
  ERR_DEV_VALUES,
  ERR_EXEC_FORMAT,
  ERR_FILELENGTH,
  ERR_FILE_NOT_FOUND,
  ERR_FSYS_CORRUPT,
  ERR_FSYS_MOUNT,
  ERR_GEOM,
  ERR_NEED_LX_KERNEL,
  ERR_NEED_MB_KERNEL,
  ERR_NO_DISK,
  ERR_NO_PART,
  ERR_NUMBER_PARSING,
  ERR_OUTSIDE_PART,
  ERR_READ,
  ERR_SYMLINK_LOOP,
  ERR_UNRECOGNIZED,
  ERR_WONT_FIT,
  ERR_WRITE,
  ERR_BAD_ARGUMENT,
  ERR_UNALIGNED,
  ERR_PRIVILEGED,
  ERR_DEV_NEED_INIT,
  ERR_NO_DISK_SPACE,
  ERR_NUMBER_OVERFLOW,

  MAX_ERR_NUM
} grub_error_t;

extern char config_file[];

/* GUI interface variables. */
# define MAX_FALLBACK_ENTRIES	8
extern int fallback_entries[MAX_FALLBACK_ENTRIES];
extern int fallback_entryno;
extern int default_entry;
extern int current_entryno;

/* The constants for password types.  */
typedef enum
{
  PASSWORD_PLAIN,
  PASSWORD_MD5,
  PASSWORD_UNSUPPORTED
} password_t;

extern char *password;
extern password_t password_type;
extern int auth;
extern char commands[];

/* For `more'-like feature.  */
extern int max_lines;
extern int count_lines;
extern int use_pager;

/*
 *  Error variables.
 */

extern grub_error_t errnum;
extern char *err_list[];

/* Terminal */
extern int terminal_flags;

/* These are used to represent the various color states we use */
typedef enum
{
  /* represents the color used to display all text that does not use the user
   * defined colors below
   */
  COLOR_STATE_STANDARD,
  /* represents the user defined colors for normal text */
  COLOR_STATE_NORMAL,
  /* represents the user defined colors for highlighted text */
  COLOR_STATE_HIGHLIGHT
} color_state;

/* Flags for representing the capabilities of a terminal.  */

/* Set when input characters shouldn't be echoed back.  */
#define TERM_NO_ECHO		(1 << 0)
/* Set when the editing feature should be disabled.  */
#define TERM_NO_EDIT		(1 << 1)

/* The console stuff.  */
void console_putchar (int c);
void console_setcolorstate (color_state state);
void console_setcolor (int normal_color, int highlight_color);

/* Clear the screen. */
void cls (void);

/* Turn on/off cursor. */
int setcursor (int on);

/* Get the current cursor position (where 0,0 is the top left hand
   corner of the screen).  Returns packed values, (RET >> 8) is x,
   (RET & 0xff) is y. */
int getxy (void);

/* Set the cursor position. */
void gotoxy (int x, int y);

/* Displays an ASCII character.  IBM displays will translate some
   characters to special graphical ones (see the DISP_* constants). */
void grub_putchar (int c);

/* Wait for a keypress, and return its packed BIOS/ASCII key code.
   Use ASCII_CHAR(ret) to extract the ASCII code. */
int getkey (void);

/* Like GETKEY, but doesn't block, and returns -1 if no keystroke is
   available. */
int checkkey (void);

/* Command-line interface functions. */

/* The flags for the builtins.  */
#define BUILTIN_CMDLINE		0x1	/* Run in the command-line.  */
#define BUILTIN_MENU		0x2	/* Run in the menu.  */
#define BUILTIN_TITLE		0x4	/* Only for the command title.  */
#define BUILTIN_SCRIPT		0x8	/* Run in the script.  */
#define BUILTIN_NO_ECHO		0x10	/* Don't print command on booting. */
#define BUILTIN_HELP_LIST	0x20	/* Show help in listing.  */

/* The table for a builtin.  */
struct builtin
{
  /* The command name.  */
  char *name;
  /* The callback function.  */
  int (*func) (char *, int);
  /* The combination of the flags defined above.  */
  int flags;
  /* The short version of the documentation.  */
  char *short_doc;
  /* The long version of the documentation.  */
  char *long_doc;
};

/* All the builtins are registered in this.  */
extern struct builtin *builtin_table[];

/* The constants for kernel types.  */
typedef enum
{
  KERNEL_TYPE_NONE,		/* None is loaded.  */
  KERNEL_TYPE_MULTIBOOT,	/* Multiboot.  */
  KERNEL_TYPE_LINUX,		/* Linux.  */
  KERNEL_TYPE_BIG_LINUX,	/* Big Linux.  */
  KERNEL_TYPE_FREEBSD,		/* FreeBSD.  */
  KERNEL_TYPE_NETBSD,		/* NetBSD.  */
  KERNEL_TYPE_CHAINLOADER	/* Chainloader.  */
}
kernel_t;

extern kernel_t kernel_type;
extern int show_menu;
extern int grub_timeout;

void init_builtins (void);
void init_config (void);
char *skip_to (int after_equal, char *cmdline);
struct builtin *find_command (char *command);
void enter_cmdline (char *heap, int forever);
int run_script (char *script, char *heap);

/* the flags for the cmdline message */
#define CMDLINE_FOREVER_MODE 0x0
#define CMDLINE_NORMAL_MODE 0x1
#define CMDLINE_EDIT_MODE 0x2

void print_cmdline_message (int type);

/* C library replacement functions with identical semantics. */
void grub_printf (const char *format,...);

/* misc */
void init_page (void);
void print_error (void);
int get_cmdline (char *prompt, char *cmdline, int maxlen,
		 int echo_char, int history);
int substring (const char *s1, const char *s2);
int nul_terminate (char *str);
int safe_parse_maxint (char **str_ptr, int *myint_ptr);
void grub_putstr (const char *str);

/* List the contents of the directory that was opened with GRUB_OPEN,
   printing all completions. */
int dir (const char *dirname);

/* Display device and filename completions. */
void print_a_completion (char *filename);
int print_completions (int is_filename, int is_completion);

int check_password(char *entered, char* expected, password_t type);

/* FILO specific stuff */
void copy_path_to_filo_bootline(char *arg, char *path, int use_rootdev, int append);

#endif /* ! GRUB_SHARED_HEADER */
