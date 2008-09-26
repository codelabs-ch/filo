/*
 * This file is part of FILO.
 *
 * Copyright (C) 2008 coresystems GmbH
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
#include <pci.h>

#define DEBUG_THIS CONFIG_DEBUG_INTEL
#include <debug.h>

/**
 * Mostly for testing purposes without booting an OS
 */

void platform_poweroff(void)
{
	int pmbase = 0x800;

	/* XXX The sequence is correct; It works fine under Linux. 
	 * Yet, it does not power off the system in FILO. 
	 * Some initialization is missing
	 */

        /* PMBASE + 4, Bit 10-12, Sleeping Type,
	 * set to 110 -> S5, soft_off */

	outl((6 << 10), pmbase + 0x04);
	outl((1 << 13) | (6 << 10), pmbase + 0x04);

	for (;;) ;
}

static inline void kbc_wait(void)
{
	int i;
	for (i = 0; i < 0x10000; i++) {
		if ((inb(0x64) & 0x02) == 0)
			break;
		udelay(2);
	}
}

void platform_reboot(void)
{
	int i;

	for (i = 0; i < 10; i++) {
		kbc_wait();

		outb(0x60, 0x64);       /* write Controller Command Byte */
		udelay(50);
		kbc_wait();

		outb(0x14, 0x60);       /* set "System flag" */
		udelay(50);
		kbc_wait();

		outb(0xfe, 0x64);       /* pulse reset low */
		udelay(50);
	}
	
	for (;;) ;
}

