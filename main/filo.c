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

#include <libpayload.h>
#include <config.h>
#include <version.h>
#include <lib.h>
#include <fs.h>
#include <sys_info.h>
#include <sound.h>
#include <arch/timer.h>

PAYLOAD_INFO(name, PROGRAM_NAME " " PROGRAM_VERSION);
PAYLOAD_INFO(listname, PROGRAM_NAME);
PAYLOAD_INFO(desc, "Bootloader");

#define ENTER '\r'
#define ESCAPE '\x1b'

#if !defined(CONFIG_AUTOBOOT_FILE)
#define autoboot() ((void) 0) /* nop */
#endif

#ifndef CONFIG_AUTOBOOT_DELAY
#define autoboot_delay() 0 /* success */
#endif

struct sys_info sys_info;

void relocate(void);

static void init(void)
{
    /* Set up the consoles. */
    console_init();
#ifdef CONFIG_VIDEO_CONSOLE
    video_console_init();
#endif

    /* Gather system information, and implicitly sets up timers */
    lib_get_sysinfo();

    printf("%s version %s\n", program_name, program_version);
    collect_sys_info(&sys_info);
    relocate();

#if defined(CONFIG_USB_DISK) || defined(CONFIG_USB_NEW_DISK)
    usb_initialize();
#endif

#ifdef CONFIG_SUPPORT_SOUND
    sound_init();
#endif
#ifdef CONFIG_SLOW_SATA
    delay(5);
#endif
}

void boot(const char *line)
{
    char *file, *param;

    /* Split filename and parameter */
    file = strdup(line);
    param = strchr(file, ' ');
    if (param) {
	*param = '\0';
	param++;
    }

	if (artecboot_load(file, param) == LOADER_NOT_SUPPORT)
    if (elf_load(file, param) == LOADER_NOT_SUPPORT)
	if (linux_load(file, param) == LOADER_NOT_SUPPORT)
	if (wince_load(file, param) == LOADER_NOT_SUPPORT)
	    printf("Unsupported image format\n");
    free(file);
}

#if CONFIG_USE_GRUB
/* The main routine */
int main(void)
{
    void grub_main(void);
    
    /* Initialize */
    init();
    grub_main();
    return 0;   
}

#else // ! CONFIG_USE_GRUB

#ifdef CONFIG_AUTOBOOT_FILE
#if CONFIG_AUTOBOOT_DELAY
static inline int autoboot_delay(void)
{
    u64 timeout;
    int sec, tmp;
    char key;
    
    key = 0;

    printf("Press <Enter> for default boot, or <Esc> for boot prompt... ");
    for (sec = CONFIG_AUTOBOOT_DELAY; sec>0 && key==0; sec--) {
	printf("%d", sec);
	timeout = currticks() + TICKS_PER_SEC;
	while (currticks() < timeout) {
	    if (havechar()) {
		key = getchar();
		if (key==ENTER || key==ESCAPE)
		    break;
	    }
	}
	for (tmp = sec; tmp; tmp /= 10)
	    printf("\b \b");
    }
    if (key == 0) {
	printf("timed out\n");
	return 0; /* success */
    } else {
	putchar('\n');
	if (key == ESCAPE)
	    return -1; /* canceled */
	else
	    return 0; /* default accepted */
    }
}
#endif /* CONFIG_AUTOBOOT_DELAY */

static void autoboot(void)
{
    /* If Escape key is pressed already, skip autoboot */
    if (havechar() && getchar()==ESCAPE)
	return;

    if (autoboot_delay()==0) {
	printf("boot: %s\n", CONFIG_AUTOBOOT_FILE);
	boot(CONFIG_AUTOBOOT_FILE);
    }
}
#endif /* AUTOBOOT_FILE */

/* The main routine */
int main(void)
{
    char line[256];

    /* Initialize */
    init();
    
    /* Try default image */
    autoboot();

    /* The above didn't work, ask user */
    while (havechar())
	getchar();
#ifdef CONFIG_AUTOBOOT_FILE
    strncpy(line, CONFIG_AUTOBOOT_FILE, sizeof(line)-1);
    line[sizeof(line)-1] = '\0';
#else
    line[0] = '\0';
#endif
    for (;;) {
	printf("boot: ");
	getline(line, sizeof line);

	if (strcmp(line,"quit")==0) break;

	if (line[0])
	    boot(line);
    }

    return 0;
}

#endif /* CONFIG_USE_GRUB */
