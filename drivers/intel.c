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
 * Foundation, Inc.
 */

#include <libpayload.h>
#include <config.h>
#include <pci/pci.h>
#include <pci.h>
#include <flashlock.h>

#define DEBUG_THIS CONFIG_DEBUG_INTEL
#include <debug.h>

enum chipset {
	CHIPSET_UNKNOWN,
	INTEL_ICH7,
	INTEL_ICH9,
	INTEL_EARLY_PCH,
	INTEL_SUNRISEPOINT,
	INTEL_CANNONPOINT,
};

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
#define GPE0_EN			0x2c
#define GPE0_EN_31_0		0x90
#define GPE0_EN_63_32		0x94
#define GPE0_EN_95_64		0x98
#define GPE0_EN_127_96		0x9c
#define CNP_GPE0_EN_31_0	0x70
#define CNP_GPE0_EN_63_32	0x74
#define CNP_GPE0_EN_95_64	0x78
#define CNP_GPE0_EN_127_96	0x7c

#define RCBA8(x) *((volatile u8 *)(DEFAULT_RCBA + x))
#define RCBA16(x) *((volatile u16 *)(DEFAULT_RCBA + x))
#define RCBA32(x) *((volatile u32 *)(DEFAULT_RCBA + x))

#define ICH9_SPI_HSFS		0x3804
#define ICH9_SPI_HSFS_FLOCKDN	(1 << 15)
#define ICH9_SPI_FLASH_REGIONS	5
#define ICH9_SPI_FREG0		0x3854
#define ICH9_SPI_FPR0		0x3874
#define ICH9_SPI_PREOP		0x3894
#define ICH9_SPI_OPTYPE		0x3896
#define ICH9_SPI_OPMENU		0x3898

#define PCH100_SPI_HSFS		0x04
#define PCH100_SPI_HSFS_WRSDIS	(1 << 11)
#define PCH100_DLOCK		0x0c
#define PCH100_DLOCK_PR0_LOCKDN	(1 << 8)
#define PCH100_SPI_FPR0		0x84

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

static enum chipset detect_chipset(void)
{
	const uint32_t viddid = pci_read_config32(PCI_DEV(0, 0x1f, 0), 0);
	const unsigned int vid = viddid & 0xffff;
	const unsigned int did = viddid >> 16;

	if (vid != 0x8086) {
		printf("ERROR: Not a supported ICH or PCH southbridge: %04x:%04x\n", vid, did);
		return CHIPSET_UNKNOWN;
	}

	switch (did) {
	case 0x27b0:
	case 0x27b8:
	case 0x27b9:
	case 0x27bd:
		return INTEL_ICH7;
	case 0x2912:
	case 0x2914:
	case 0x2916:
	case 0x2917:
	case 0x2918:
	case 0x2919:
		return INTEL_ICH9;
	/* Cougar Point */
	case 0x1c44:
	case 0x1c46:
	case 0x1c47:
	case 0x1c49:
	case 0x1c4a:
	case 0x1c4b:
	case 0x1c4c:
	case 0x1c4d:
	case 0x1c4e:
	case 0x1c4f:
	case 0x1c50:
	case 0x1c52:
	case 0x1c54:
	case 0x1c56:
	case 0x1c5c:
	/* Panther Point */
	case 0x1e44:
	case 0x1e46:
	case 0x1e47:
	case 0x1e48:
	case 0x1e49:
	case 0x1e4a:
	case 0x1e53:
	case 0x1e55:
	case 0x1e56:
	case 0x1e57:
	case 0x1e58:
	case 0x1e59:
	case 0x1e5d:
	case 0x1e5e:
	case 0x1e5f:
		return INTEL_EARLY_PCH;
	/* Skylake U/Y */
	case 0x9d41:
	case 0x9d43:
	case 0x9d46:
	case 0x9d48:
	/* Kaby Lake U/Y */
	case 0x9d4b:
	case 0x9d4e:
	case 0x9d50:
	case 0x9d51:
	case 0x9d53:
	case 0x9d56:
	case 0x9d58:
	/* Sunrise Point PCH-H */
	case 0xa141:
	case 0xa142:
	case 0xa143:
	case 0xa144:
	case 0xa145:
	case 0xa146:
	case 0xa147:
	case 0xa148:
	case 0xa149:
	case 0xa14a:
	case 0xa14b:
	case 0xa14d:
	case 0xa14e:
	case 0xa150:
	case 0xa151:
	case 0xa152:
	case 0xa153:
	case 0xa154:
	case 0xa155:
	/* Union Point PCH-H */
	case 0xa2c4:
	case 0xa2c5:
	case 0xa2c6:
	case 0xa2c7:
	case 0xa2c8:
	case 0xa2c9:
	case 0xa2ca:
	case 0xa2cc:
		return INTEL_SUNRISEPOINT;
	/* Cannon/Coffee Lake U/Y */
	case 0x9d83:
	case 0x9d84:
	case 0x9d85:
	/* Cannon Lake PCH-H */
	case 0xa303:
	case 0xa304:
	case 0xa305:
	case 0xa306:
	case 0xa308:
	case 0xa309:
	case 0xa30a:
	case 0xa30c:
	case 0xa30d:
	case 0xa30e:
		return INTEL_CANNONPOINT;
	default:
		printf("ERROR: Not a supported ICH or PCH southbridge: %04x:%04x\n", vid, did);
		return CHIPSET_UNKNOWN;
	}
}

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

static void lockdown_flash_pch(void)
{
	u8 reg8;

	reg8 = pci_read_config8(PCI_DEV(0,0x1f,0), 0xdc);	/* BIOS_CNTL */
	reg8 |= (1 << 5);
	pci_write_config8(PCI_DEV(0,0x1f,0), 0xdc, reg8);

	lockdown_flash_ich7();

	/* Since Cougar Point, coreboot uses flash regions,
	   lock them and set up static SPI opcodes. */
	lockdown_flash_ich9_lock_regions();
}

static int lockdown_flash_spt(void)
{
	const unsigned int fpr_count = 5;
	uint8_t *const spibar = phys_to_virt(
		pci_read_config32(PCI_DEV(0, 0x1f, 5), PCI_BASE_ADDRESS_0)
		& 0xfffff000);

	if (!spibar || spibar == phys_to_virt(0xfffff000)) {
		printf("ERROR: SPIBAR not accessible!\n");
		return -1;
	}

	/* Find a writeable FPR to write-protect everything. */
	unsigned int i;
	for (i = 0; i < fpr_count; ++i) {
		write32(spibar + PCH100_SPI_FPR0 + 4 * i, 0xffff0000);
		if (read32(spibar + PCH100_SPI_FPR0 + 4 * i) == 0xffff0000)
			break;
	}
	if (i >= fpr_count) {
		printf("ERROR: Can't find writeable FPR register!\n");
		return -1;
	}
	write32(spibar + PCH100_DLOCK,
		read32(spibar + PCH100_DLOCK) | PCH100_DLOCK_PR0_LOCKDN << i);
	write32(spibar + PCH100_SPI_HSFS,
		read32(spibar + PCH100_SPI_HSFS)
		| ICH9_SPI_HSFS_FLOCKDN | PCH100_SPI_HSFS_WRSDIS);
	return 0;
}

int intel_lockdown_flash(void)
{
	int ret = 0;

	switch (detect_chipset()) {
	case INTEL_ICH7:
		lockdown_flash_ich7();
		break;
	case INTEL_ICH9:
		lockdown_flash_ich7();
		/* Coreboot doesn't use flash regions for ICH9 based
		   boards, so we just lock empty SPI opcodes instead
		   of setting up any write protection. */
		lockdown_flash_ich9_empty_ops();
		break;
	case INTEL_EARLY_PCH:
		lockdown_flash_pch();

		/* Also trigger coreboot's SMM finalize() handlers. */
		outb(0xcb, 0xb2);
		break;
	case INTEL_SUNRISEPOINT:
		ret = lockdown_flash_spt();
		break;
	default:
		ret = -1;
		break;
	}

	if (ret == 0)
		debug("BIOS hard locked until next full reset.\n");

	return ret;
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

	const enum chipset cs = detect_chipset();

	switch (cs) {
	case INTEL_CANNONPOINT:
		/* FIXME: I guess we are supposed to read FADT. */
		pmbase = 0x1800;
		break;
	case INTEL_SUNRISEPOINT:
		pmbase = pci_read_config16(pmc_dev, 0x40) & 0xff00;
		break;
	default:
		pmbase = pci_read_config16(lpc_dev, 0x40) & 0xfffe;
		break;
	}

	/* Mask interrupts */
	asm("cli");

	/* Turn off all bus master enable bits */
	busmaster_disable();

	/* avoid any GPI waking the system from S5 */
	switch (cs) {
	case INTEL_CANNONPOINT:
		outl(0x00000000, pmbase + CNP_GPE0_EN_31_0);
		outl(0x00000000, pmbase + CNP_GPE0_EN_63_32);
		outl(0x00000000, pmbase + CNP_GPE0_EN_95_64);
		outl(0x00000000, pmbase + CNP_GPE0_EN_127_96);
		break;
	case INTEL_SUNRISEPOINT:
		outl(0x00000000, pmbase + GPE0_EN_31_0);
		outl(0x00000000, pmbase + GPE0_EN_63_32);
		outl(0x00000000, pmbase + GPE0_EN_95_64);
		outl(0x00000000, pmbase + GPE0_EN_127_96);
		break;
	default:
		outl(0x00000000, pmbase + GPE0_EN);
		break;
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

