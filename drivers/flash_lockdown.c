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
 */

#include <libpayload.h>
#include <config.h>
#include <debug.h>	/* for grub printf() override */
#include <flashlock.h>

int flashrom_lockdown = 1;

int lockdown_flash(void)
{
	if (flashrom_lockdown) {
		printf("Locking system flash memory...\n");
		if (IS_ENABLED(CONFIG_TARGET_I386) &&
				intel_lockdown_flash() == 0) {
			printf("done (Intel)\n");
		} else if (IS_ENABLED(CONFIG_TARGET_I386) &&
				amd_lockdown_flash() == 0) {
			printf("done (AMD)\n");
		} else {
			printf("FAILED!\n");
			delay(5);
			return -1;
		}
	} else {
		printf("Leaving system flash memory unlocked...\n");
	}
	return 0;
}
