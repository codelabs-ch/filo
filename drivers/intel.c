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
#include <pci/pci.h>
#include <pci.h>
#include <flashlock.h>

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
#define GPE0_EN_31_0	0x90
#define GPE0_EN_63_32	0x94
#define GPE0_EN_95_64	0x98
#define GPE0_EN_127_96	0x9c

#define RCBA8(x) *((volatile u8 *)(DEFAULT_RCBA + x))
#define RCBA16(x) *((volatile u16 *)(DEFAULT_RCBA + x))
#define RCBA32(x) *((volatile u32 *)(DEFAULT_RCBA + x))

#define ICH9_SPI_HSFS		0x3804
#define ICH9_SPI_HSFS_FLOCKDN	(1 << 15);
#define ICH9_SPI_FLASH_REGIONS	5
#define ICH9_SPI_FREG0		0x3854
#define ICH9_SPI_FPR0		0x3874
#define ICH9_SPI_PREOP		0x3894
#define ICH9_SPI_OPTYPE		0x3896
#define ICH9_SPI_OPMENU		0x3898

/*
 * SPI Opcode Menu setup for SPIBAR lockdown
 * should support most common flash chips.
 */
#define SPI_OPMENU_0 0x01 /* WRSR: Write Status Register */
#define SPI_OPTYPE_0 0x01 /* Write, no address */

#define SPI_OPMENU_1 0x02 /* BYPR: Byte Program */
#define SPI_OPTYPE_1 0x03 /* Write, address required */

#define SPI_OPMENU_2 0x03 /* READ: Read Data */
#define SPI_OPTYPE_2 0x02 /* Read, address required */

#define SPI_OPMENU_3 0x05 /* RDSR: Read Status Register */
#define SPI_OPTYPE_3 0x00 /* Read, no address */

#define SPI_OPMENU_4 0x20 /* SE20: Sector Erase 0x20 */
#define SPI_OPTYPE_4 0x03 /* Write, address required */

#define SPI_OPMENU_5 0x9f /* RDID: Read ID */
#define SPI_OPTYPE_5 0x00 /* Read, no address */

#define SPI_OPMENU_6 0xd8 /* BED8: Block Erase 0xd8 */
#define SPI_OPTYPE_6 0x03 /* Write, address required */

#define SPI_OPMENU_7 0x0b /* FAST: Fast Read */
#define SPI_OPTYPE_7 0x02 /* Read, address required */

#define SPI_OPMENU_UPPER ((SPI_OPMENU_7 << 24) | (SPI_OPMENU_6 << 16) | \
			  (SPI_OPMENU_5 << 8) | SPI_OPMENU_4)
#define SPI_OPMENU_LOWER ((SPI_OPMENU_3 << 24) | (SPI_OPMENU_2 << 16) | \
			  (SPI_OPMENU_1 << 8) | SPI_OPMENU_0)

#define SPI_OPTYPE ((SPI_OPTYPE_7 << 14) | (SPI_OPTYPE_6 << 12) | \
		    (SPI_OPTYPE_5 << 10) | (SPI_OPTYPE_4 << 8) |  \
		    (SPI_OPTYPE_3 << 6) | (SPI_OPTYPE_2 << 4) |	  \
		    (SPI_OPTYPE_1 << 2) | (SPI_OPTYPE_0))

#define SPI_OPPREFIX ((0x50 << 8) | 0x06) /* EWSR and WREN */

static void lockdown_flash_ich7(void)
{
	u8 reg8;
	u32 reg32;

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
}

static void lockdown_flash_ich9_empty_ops(void)
{
	/* Lock SPI interface with empty opcode registers */
	RCBA16(ICH9_SPI_PREOP)  = 0x0000;
	RCBA16(ICH9_SPI_OPTYPE) = 0x0000;
	RCBA32(ICH9_SPI_OPMENU + 0) = 0x00000000;
	RCBA32(ICH9_SPI_OPMENU + 4) = 0x00000000;
	RCBA16(ICH9_SPI_HSFS) |= ICH9_SPI_HSFS_FLOCKDN;
}

static void lockdown_flash_ich9_lock_regions(void)
{
	/* Lock SPI interface with the opcodes coreboot uses */
	RCBA16(ICH9_SPI_PREOP)  = SPI_OPPREFIX;
	RCBA16(ICH9_SPI_OPTYPE) = SPI_OPTYPE;
	RCBA32(ICH9_SPI_OPMENU + 0) = SPI_OPMENU_LOWER;
	RCBA32(ICH9_SPI_OPMENU + 4) = SPI_OPMENU_UPPER;

	/* Copy flash regions from FREG0-4 to FPR0-4
	   and enable write protection bit31 */
	int i;
	for (i = 0; i < (4 * ICH9_SPI_FLASH_REGIONS); i += 4)
		RCBA32(ICH9_SPI_FPR0 + i) =
			RCBA32(ICH9_SPI_FREG0 + i) | (1 << 31);

	RCBA16(ICH9_SPI_HSFS) |= ICH9_SPI_HSFS_FLOCKDN;
}

int intel_lockdown_flash(void)
{
	const u32 reg32 = pci_read_config32(PCI_DEV(0,0x1f, 0), 0);
	switch (reg32) {

		/* ICH7 */
	case 0x27b08086:
	case 0x27b88086:
	case 0x27b98086:
	case 0x27bd8086:
		lockdown_flash_ich7();
		break;

		/* ICH9 */
	case 0x29128086:
	case 0x29148086:
	case 0x29168086:
	case 0x29178086:
	case 0x29188086:
	case 0x29198086:
		lockdown_flash_ich7();
		/* Coreboot doesn't use flash regions for ICH9 based
		   boards, so we just lock empty SPI opcodes instead
		   of setting up any write protection. */
		lockdown_flash_ich9_empty_ops();
		break;

		/* Cougar Point */
	case 0x1c448086:
	case 0x1c468086:
	case 0x1c478086:
	case 0x1c498086:
	case 0x1c4a8086:
	case 0x1c4b8086:
	case 0x1c4c8086:
	case 0x1c4d8086:
	case 0x1c4e8086:
	case 0x1c4f8086:
	case 0x1c508086:
	case 0x1c528086:
	case 0x1c548086:
	case 0x1c568086:
	case 0x1c5c8086:
		/* Panther Point */
	case 0x1e448086:
	case 0x1e468086:
	case 0x1e478086:
	case 0x1e488086:
	case 0x1e498086:
	case 0x1e4a8086:
	case 0x1e538086:
	case 0x1e558086:
	case 0x1e568086:
	case 0x1e578086:
	case 0x1e588086:
	case 0x1e598086:
	case 0x1e5d8086:
	case 0x1e5e8086:
	case 0x1e5f8086:
		lockdown_flash_ich7();
		/* Since Cougar Point, coreboot uses flash regions,
		   lock them and set up static SPI opcodes. */
		lockdown_flash_ich9_lock_regions();
		/* Also trigger coreboot's SMM finalize() handlers. */
		outb(0xcb, 0xb2);
		break;
	default:
		debug("Not a supported ICH or PCH southbridge\n");
		return -1;
	}

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
	const u32 pmc_dev = PCI_DEV(0, 0x1f, 2); /* from Sunrise Point PCH on */
	const u32 lpc_dev = PCI_DEV(0, 0x1f, 0);

	u16 pmbase;
	u32 reg32;

	const u16 class = pci_read_config16(pmc_dev, PCI_CLASS_DEVICE);
	const int is_pmc = class == PCI_CLASS_MEMORY_OTHER;
	if (is_pmc)
		pmbase = pci_read_config16(pmc_dev, 0x40) & 0xff00;
	else
		pmbase = pci_read_config16(lpc_dev, 0x40) & 0xfffe;

	/* Mask interrupts */
	asm("cli");

	/* Turn off all bus master enable bits */
	busmaster_disable();

	/* avoid any GPI waking the system from S5 */
	if (is_pmc) {
		outl(0x00000000, pmbase + GPE0_EN_31_0);
		outl(0x00000000, pmbase + GPE0_EN_63_32);
		outl(0x00000000, pmbase + GPE0_EN_95_64);
		outl(0x00000000, pmbase + GPE0_EN_127_96);
	} else {
		outl(0x00000000, pmbase + GPE0_EN);
	}

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

	printf("\nPOWEROFF FAILURE! Power off the system manually, please.\n");
	halt();
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

	/* Try a system reset first. */
	outb(0x2, 0xcf9);
	outb(0x6, 0xcf9);

	/* Fall back to keyboard controller reset. */
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

	printf("\nREBOOT FAILURE! Reboot the system manually, please.\n");
	halt();
}

