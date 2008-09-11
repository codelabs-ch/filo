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
#include <lib.h>
#include <fs.h>
#include <arch/timer.h>

extern char config_file[];

#define ENTER '\r'
#define ESCAPE '\x1b'

#ifndef CONFIG_MENULST_TIMEOUT
#define CONFIG_MENULST_TIMEOUT 0
#endif
#if !CONFIG_MENULST_TIMEOUT
#define menulst_delay() 0 /* success */
#endif

#if CONFIG_MENULST_TIMEOUT
static inline int menulst_delay(void)
{
    u64 timeout;
    int sec, tmp;
    char key;
    
    key = 0;

#ifdef CONFIG_MENULST_FILE
    printf("Press <Enter> for default menu.lst (%s), or <Esc> for prompt... ",
		    CONFIG_MENULST_FILE);
#else
    printf("Press <Enter> for the FILO shell or <ESC> to enter a menu.lst path...");
#endif    
    for (sec = CONFIG_MENULST_TIMEOUT; sec>0 && key==0; sec--) {
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
#endif /* CONFIG_MENULST_TIMEOUT */

void grub_menulst(void)
{
    char line[256];

    /* If Escape key is pressed already, skip autoboot */
    if (havechar() && getchar()==ESCAPE)
	return;

    if (menulst_delay()==0) {
#ifdef CONFIG_MENULST_FILE
	printf("menu: %s\n", CONFIG_MENULST_FILE);
	strcpy(config_file, CONFIG_MENULST_FILE);
#endif
    } else {
	    /* The above didn't work, ask user */
        while (havechar())
   	    getchar();

#ifdef CONFIG_MENULST_FILE
        strncpy(line, CONFIG_MENULST_FILE, sizeof(line)-1);
        line[sizeof(line)-1] = '\0';
#else
        line[0] = '\0';
#endif
        for (;;) {
	    printf("menu: ");
	    getline(line, sizeof line);

	    if (strcmp(line,"quit")==0) break;

	    if (line[0]) {
	        strcpy(config_file, line);
		break;
	    }
        }
    }

    
}

