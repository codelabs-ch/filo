/*
 * This file is part of FILO.
 *
 * Copyright (C) 2010 secunet Security Networks AG
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
 * Foundation, Inc.
 */

#include <libpayload.h>
#include <config.h>
#include <pci.h>
#include <flashlock.h>

#define DEBUG_THIS CONFIG_DEBUG_AMD
#include <debug.h>

#define ROM_BASE			0xfff00000	/* FIXME: should be read from chipset */
#define ROM_CHUNK_SIZE		512 /* kB */
#define SB600_LPC_BRIDGE	PCI_DEV(0, 0x14, 3)

extern int sb600_spi_lockdown(void);

int amd_lockdown_flash(void)
{
	u32 base = ROM_BASE;
	u32 reg32, val32;
	int spi, lpc;
	int reg;

	/* for now we only support the SB600 chipset */
	reg32 = pci_read_config32(SB600_LPC_BRIDGE, REG_VENDOR_ID);
	switch (reg32) {
		case 0x438d1002:
			break;

		default:
			debug("Not an SB600 chipset\n");
			return -1;
	}

	/* Lock the SPI interface first */
	debug("Locking SPI interface...\n");
	spi = sb600_spi_lockdown();
	if (spi == 0) {
		debug("Flash chip SPI write protect enabled!\n");
	} else {
		debug("Failed to enable flash chip SPI write protect%s!\n",
		      spi > 0 ? " (already locked)" : "");

		/* if the chip was already locked, this might be no failure */
		if (spi > 0) {
			spi = 0;
		}
	}

	/* Enable ROM write protect */
	debug("Locking BIOS ROM range to read-only...\n");

	/* LPC ROM read/write protect registers (50h, 54h, 58h, 5ch)
	 * 31:11 = base address (2k aligned)
	 *  10:2 = length-1 in kB (0=1kB, 511=512kB)
	 *     1 = read protect bit (1 = read protect)
	 *     0 = write protect bit (1 = write protect)
	 *
	 * => we need 2 of the 4 available registers to protect a chip sized 1MB
	 */

	lpc = -1;
	for (reg = 0x50; reg <= 0x5c; reg += 4) {
		val32  = base & ~((1 << 11) - 1);	// base address
		val32 |= (ROM_CHUNK_SIZE - 1) << 2;	// size = 512kB
		val32 |= 1;							// write protect

		pci_write_config32(SB600_LPC_BRIDGE, reg, val32);
		if (pci_read_config32(SB600_LPC_BRIDGE, reg) != val32) {
			debug("Setting LPC ROM protection register 0x%x failed, trying to "
			      "use another\n", reg);

			continue;
		}

		if (base + ROM_CHUNK_SIZE * 1024 - 1 == 0xffffffff) {
			lpc = 0;

			break;
		}

		/* increase to the next chunk to protect */
		base += ROM_CHUNK_SIZE * 1024;
	}

	if (lpc == 0) {
		debug("LPC BIOS ROM range write protectedd until next full reset.\n");
	} else {
		debug("Failed to protect LPC BIOS ROM range!\n");
	}

	return spi | lpc;
}
