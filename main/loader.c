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

#include <flashlock.h>
#include <loader.h>

int prepare_for_jump(void) {
	if (IS_ENABLED(CONFIG_FLASHROM_LOCKDOWN)) {
		if (lockdown_flash())
			return -1;
	}

	if (IS_ENABLED(CONFIG_LP_USB))
		usb_exit();

	if (IS_ENABLED(CONFIG_PCMCIA_CF)) {
		unsigned char *cf_bar;
		int i;

		cf_bar = phys_to_virt(pci_read_config32(PCI_DEV(0, 0xa, 1), 0x10));
		for (i = 0x836; i < 0x840; i++)
			cf_bar[i] = 0;
	}

	return 0;
}

void restore_after_jump(void) {
	console_init();
}
