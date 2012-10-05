/*
 * This file is part of FILO.
 *
 * Copyright (C) 2008-2009 coresystems GmbH
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

#define DEFAULT_RCBA            phys_to_virt(0xfed1c000)

#define PM1_STS		0x00
#define   PWRBTN_STS	(1 << 8)
#define PM1_EN		0x02
#define PM1_CNT		0x04
#define   SLP_EN	(1 << 13)
#define   SLP_TYP	(7 << 10)
#define   SLP_TYP_S0	(0 << 10)
#define   SLP_TYP_S1	(1 << 10)
#define   SLP_TYP_S3	(5 << 10)
#define   SLP_TYP_S4	(6 << 10)
#define   SLP_TYP_S5	(7 << 10)
#define GPE0_EN		0x2c

#define RCBA8(x) *((volatile u8 *)(DEFAULT_RCBA + x))
#define RCBA16(x) *((volatile u16 *)(DEFAULT_RCBA + x))
#define RCBA32(x) *((volatile u32 *)(DEFAULT_RCBA + x))


int intel_lockdown_flash(void)
{
	u8 reg8;
	u32 reg32;

	reg32 = pci_read_config32(PCI_DEV(0,0x1f, 0), 0);
	switch (reg32) {
		/* ICH7 */
	case 0x27b08086:
	case 0x27b88086:
	case 0x27b98086:
	case 0x27bd8086:
		/* ICH9 */
	case 0x29128086:
	case 0x29148086:
	case 0x29168086:
	case 0x29178086:
	case 0x29188086:
	case 0x29198086:
		break;
	default:
		debug("Not an ICH7 or ICH9 southbridge\n");
		return -1;
	}

	/* Now try this: */
	debug("Locking BIOS to read-only... "); 
	reg8 = pci_read_config8(PCI_DEV(0,0x1f,0), 0xdc);	/* BIOS_CNTL */
	debug(" BIOS Lockdown Enable: %s; BIOS Write-Enable: %s\n",
			(reg8&2)?"on":"off", (reg8&1)?"rw":"ro");

	reg8 &= ~(1 << 0);			/* clear BIOSWE */
	pci_write_config8(PCI_DEV(0,0x1f,0), 0xdc, reg8);

	reg8 |= (1 << 1);			/* set BLE */
	pci_write_config8(PCI_DEV(0,0x1f,0), 0xdc, reg8);

	reg32 = RCBA32(0x3410); /* GCS - General Control and Status */
	reg32 |= 1;		/* BILD - BIOS Interface Lockdown */
	RCBA32(0x3410) = reg32; /* Set BUC.TS and GCS.BBS to RO */

	debug("BIOS hard locked until next full reset.\n");

	return 0;
}


/* We should add some "do this for each pci device" function to libpayload */

static void busmaster_disable_on_bus(int bus)
{
	int slot, func;
	unsigned int val;
	unsigned char hdr;

	for (slot = 0; slot < 0x20; slot++) {
		for (func = 0; func < 8; func++) {
			u32 reg32;
			pcidev_t dev = PCI_DEV(bus, slot, func);

			val = pci_read_config32(dev, REG_VENDOR_ID);

			if (val == 0xffffffff || val == 0x00000000 ||
			    val == 0x0000ffff || val == 0xffff0000)
				continue;

			/* Disable Bus Mastering for this one device */
			reg32 = pci_read_config32(dev, REG_COMMAND);
			reg32 &= ~REG_COMMAND_BM;
			pci_write_config32(dev, REG_COMMAND, reg32);

			/* If this is a bridge, then follow it. */
			hdr = pci_read_config8(dev, REG_HEADER_TYPE);
			hdr &= 0x7f;
			if (hdr == HEADER_TYPE_BRIDGE ||
			    hdr == HEADER_TYPE_CARDBUS) {
				unsigned int buses;
				buses = pci_read_config32(dev, REG_PRIMARY_BUS);
				busmaster_disable_on_bus((buses >> 8) & 0xff);
			}
		}
	}
}

static void busmaster_disable(void)
{
	busmaster_disable_on_bus(0);
}

/**
 * Mostly for testing purposes without booting an OS
 */

void platform_poweroff(void)
{
	u16 pmbase;
	u32 reg32;
	
	pmbase = pci_read_config16(PCI_DEV(0,0x1f, 0), 0x40) & 0xfffe;

	/* Mask interrupts */
	asm("cli");

	/* Turn off all bus master enable bits */
	busmaster_disable();

	/* avoid any GPI waking the system from S5 */
	outl(0x00000000, pmbase + GPE0_EN);

	/* Clear Power Button Status */
	outw(PWRBTN_STS, pmbase + PM1_STS);

        /* PMBASE + 4, Bit 10-12, Sleeping Type,
	 * set to 111 -> S5, soft_off */

	reg32 = inl(pmbase + PM1_CNT);

	reg32 &= ~(SLP_EN | SLP_TYP);
	reg32 |= SLP_TYP_S5;
	outl(reg32, pmbase + PM1_CNT);

	reg32 |= SLP_EN;
	outl(reg32, pmbase + PM1_CNT);

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

