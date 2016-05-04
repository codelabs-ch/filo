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
#include <libpayload-config.h>
#include <config.h>
#include <version.h>
#include <lib.h>
#include <fs.h>
#include <sys_info.h>
#include <sound.h>
#include <timer.h>
#include <debug.h>

PAYLOAD_INFO(name, PROGRAM_NAME " " PROGRAM_VERSION);
PAYLOAD_INFO(listname, PROGRAM_NAME);
PAYLOAD_INFO(desc, "Bootloader");

const char const *program_name = PROGRAM_NAME;
const char const *program_version = PROGRAM_VERSION_FULL;

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

void filo_reset_handler(void)
{
	void __attribute__((weak)) platform_reboot(void);

	if (platform_reboot)
		platform_reboot();
	else
		printf("Rebooting not supported.\n");

}


static void init(void)
{
    /* Gather system information, and implicitly sets up timers */
    lib_get_sysinfo();

    /* Set up the consoles. */
    console_init();

    printf("coreboot: %s\n", get_cb_version());
    printf("%s version %s\n", program_name, program_version);
    collect_sys_info(&sys_info);
    relocate();

    /* lib_sysinfo may contain virtual pointers that are invalid
       after relocation. Therefore, run lib_get_sysinfo(), again. */
    lib_get_sysinfo();

#if IS_ENABLED(CONFIG_LIBPAYLOAD_STORAGE) && IS_ENABLED(CONFIG_LP_STORAGE)
    /* libpayload storage drivers */
    storage_initialize();
#endif
#if IS_ENABLED(CONFIG_USB_DISK)
#if IS_ENABLED(CONFIG_LP_USB)
    /* libpayload USB stack is there */
    usb_initialize();
#else
    printf("No USB stack in libpayload.\n");
#endif
#endif
#if IS_ENABLED(CONFIG_LP_PC_KEYBOARD) || IS_ENABLED(CONFIG_LP_USB_HID)
    add_reset_handler(filo_reset_handler);
#endif
#if IS_ENABLED(CONFIG_SUPPORT_SOUND)
    sound_init();
#endif
#if IS_ENABLED(CONFIG_SLOW_SATA)
    delay(5);
#endif
}

int boot(const char *line)
{
    char *file, *param;
    int ret;

    /* Split filename and parameter */
    file = strdup(line);
    param = strchr(file, ' ');
    if (param) {
	*param = '\0';
	param++;
    }

    /* If the boot command is successful, the loader
     * function will not return.
     *
     * If the loader is not supported, or it recognized
     * that it does not match for the given file type, it
     * will return LOADER_NOT_SUPPORT.
     *
     * All other cases are an unknown error for now.
     */

    ret = artecboot_load(file, param);
    if (ret != LOADER_NOT_SUPPORT)
	    goto out;

    ret = elf_load(file, param);
    if (ret != LOADER_NOT_SUPPORT)
	    goto out;

    ret = linux_load(file, param);
    if (ret != LOADER_NOT_SUPPORT)
	    goto out;

    ret = wince_load(file, param);
    if (ret != LOADER_NOT_SUPPORT)
	    goto out;

    printf("Unsupported image format\n");

out:
    free(file);

    return ret;
}

#if CONFIG_USE_GRUB
/* The main routine */
int main(void)
{
    void grub_menulst(void);
    void grub_main(void);

    /* Initialize */
    init();
    grub_menulst();
    grub_main();
    return 0;
}

#else // ! CONFIG_USE_GRUB

#ifdef CONFIG_AUTOBOOT_FILE
#ifdef CONFIG_AUTOBOOT_DELAY
#if IS_ENABLED(CONFIG_NON_INTERACTIVE)
#error "autoboot delay is not supported for non-interactive builds"
#define autoboot_delay() 0 /* success */
#else
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
#endif /* CONFIG_NON_INTERACTIVE */
#endif /* CONFIG_AUTOBOOT_DELAY */

static void autoboot(void)
{
#if !IS_ENABLED(CONFIG_NON_INTERACTIVE)
    /* If Escape key is pressed already, skip autoboot */
    if (havechar() && getchar()==ESCAPE)
	return;
#endif /* CONFIG_NON_INTERACTIVE */

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

#if !IS_ENABLED(CONFIG_NON_INTERACTIVE)
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
#else /* ! CONFIG_NON_INTERACTIVE */
    for (;;) {
	printf("\nAutoboot failed! Press any key to reboot.\n");
	getchar();
	if (reset_handler) {
	    reset_handler();
	}
    }
#endif /* CONFIG_NON_INTERACTIVE */

    return 0;
}

#endif /* CONFIG_USE_GRUB */
