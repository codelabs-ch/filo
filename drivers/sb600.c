/*
 * This file is part of FILO, based on code from the flashrom project.
 *
 * Copyright (C) 2000 Silicon Integrated System Corporation
 * Copyright (C) 2004 Tyan Corp <yhlu@tyan.com>
 * Copyright (C) 2005-2008 coresystems GmbH
 * Copyright (C) 2008,2009 Carl-Daniel Hailfinger
 * Copyright (C) 2010 secunet Security Networks AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <pci/pci.h>
#include <stdint.h>
#include <stddef.h>

/*
 * libpayload quirks
 */
#include <libpayload.h>

#define DEBUG_THIS CONFIG_DEBUG_AMD
#include <debug.h>

#define print(x,...)	debug(__VA_ARGS__)

/*
 * spi.h
 */

/* Read Electronic ID */
#define JEDEC_RDID		0x9f
#define JEDEC_RDID_OUTSIZE	0x01
/* INSIZE may be 0x04 for some chips*/
#define JEDEC_RDID_INSIZE	0x03

/* Read Status Register */
#define JEDEC_RDSR		0x05
#define JEDEC_RDSR_OUTSIZE	0x01
#define JEDEC_RDSR_INSIZE	0x01
#define JEDEC_RDSR_BIT_WIP	(0x01 << 0)

/* Write Status Enable */
#define JEDEC_EWSR		0x50
#define JEDEC_EWSR_OUTSIZE	0x01
#define JEDEC_EWSR_INSIZE	0x00

/* Write Status Register */
#define JEDEC_WRSR		0x01
#define JEDEC_WRSR_OUTSIZE	0x02
#define JEDEC_WRSR_INSIZE	0x00

/* Write Enable */
#define JEDEC_WREN		0x06
#define JEDEC_WREN_OUTSIZE	0x01
#define JEDEC_WREN_INSIZE	0x00

/* Error codes */
#define SPI_GENERIC_ERROR	-1
#define SPI_INVALID_OPCODE	-2
#define SPI_INVALID_ADDRESS	-3
#define SPI_INVALID_LENGTH	-4
#define SPI_FLASHROM_BUG	-5
#define SPI_PROGRAMMER_ERROR	-6

/*
 * flashchips.h
 */

#define GENERIC_MANUF_ID	0xffff	/* Check if there is a vendor ID */
#define GENERIC_DEVICE_ID	0xffff	/* Only match the vendor ID */

#define ATMEL_ID		0x1F	/* Atmel */
#define ATMEL_AT26DF081A		0x4501

/*
 * flash.h
 */

/* Error codes */
#define TIMEOUT_ERROR	-101

typedef unsigned long chipaddr;

static inline void programmer_delay(int usecs);

enum chipbustype {
	CHIP_BUSTYPE_NONE	= 0,
	CHIP_BUSTYPE_PARALLEL	= 1 << 0,
	CHIP_BUSTYPE_LPC	= 1 << 1,
	CHIP_BUSTYPE_FWH	= 1 << 2,
	CHIP_BUSTYPE_SPI	= 1 << 3,
	CHIP_BUSTYPE_NONSPI	= CHIP_BUSTYPE_PARALLEL | CHIP_BUSTYPE_LPC | CHIP_BUSTYPE_FWH,
	CHIP_BUSTYPE_UNKNOWN	= CHIP_BUSTYPE_PARALLEL | CHIP_BUSTYPE_LPC | CHIP_BUSTYPE_FWH | CHIP_BUSTYPE_SPI,
};

/*
 * How many different contiguous runs of erase blocks with one size each do
 * we have for a given erase function?
 */
#define NUM_ERASEREGIONS 5

/*
 * How many different erase functions do we have per chip?
 * Atmel AT25FS010 has 6 different functions.
 */
#define NUM_ERASEFUNCTIONS 6

#define FEATURE_REGISTERMAP	(1 << 0)
#define FEATURE_BYTEWRITES	(1 << 1)
#define FEATURE_LONG_RESET	(0 << 4)
#define FEATURE_SHORT_RESET	(1 << 4)
#define FEATURE_EITHER_RESET	FEATURE_LONG_RESET
#define FEATURE_RESET_MASK	(FEATURE_LONG_RESET | FEATURE_SHORT_RESET)
#define FEATURE_ADDR_FULL	(0 << 2)
#define FEATURE_ADDR_MASK	(3 << 2)
#define FEATURE_ADDR_2AA	(1 << 2)
#define FEATURE_ADDR_AAA	(2 << 2)
#define FEATURE_ADDR_SHIFTED	(1 << 5)
#define FEATURE_WRSR_EWSR	(1 << 6)
#define FEATURE_WRSR_WREN	(1 << 7)
#define FEATURE_WRSR_EITHER	(FEATURE_WRSR_EWSR | FEATURE_WRSR_WREN)

struct flashchip {
	const char *vendor;
	const char *name;

	enum chipbustype bustype;

	/*
	 * With 32bit manufacture_id and model_id we can cover IDs up to
	 * (including) the 4th bank of JEDEC JEP106W Standard Manufacturer's
	 * Identification code.
	 */
	uint32_t manufacture_id;
	uint32_t model_id;

	int total_size;
	int page_size;
	int feature_bits;

	/*
	 * Indicate if flashrom has been tested with this flash chip and if
	 * everything worked correctly.
	 */
	uint32_t tested;

	int (*probe) (struct flashchip *flash);

	/* Delay after "enter/exit ID mode" commands in microseconds. */
	int probe_timing;

	/*
	 * Erase blocks and associated erase function. Any chip erase function
	 * is stored as chip-sized virtual block together with said function.
	 */
	struct block_eraser {
		struct eraseblock{
			unsigned int size; /* Eraseblock size */
			unsigned int count; /* Number of contiguous blocks with that size */
		} eraseblocks[NUM_ERASEREGIONS];
		int (*block_erase) (struct flashchip *flash, unsigned int blockaddr, unsigned int blocklen);
	} block_erasers[NUM_ERASEFUNCTIONS];

	int (*printlock) (struct flashchip *flash);
	int (*unlock) (struct flashchip *flash);
	int (*lock) (struct flashchip *flash);
	int (*write) (struct flashchip *flash, uint8_t *buf, int start, int len);
	int (*read) (struct flashchip *flash, uint8_t *buf, int start, int len);

	/* Some flash devices have an additional register space. */
	chipaddr virtual_memory;
	chipaddr virtual_registers;
};

#define TEST_UNTESTED	0

#define TEST_OK_PROBE	(1 << 0)
#define TEST_OK_READ	(1 << 1)
#define TEST_OK_ERASE	(1 << 2)
#define TEST_OK_WRITE	(1 << 3)
#define TEST_OK_PR	(TEST_OK_PROBE | TEST_OK_READ)
#define TEST_OK_PRE	(TEST_OK_PROBE | TEST_OK_READ | TEST_OK_ERASE)
#define TEST_OK_PRW	(TEST_OK_PROBE | TEST_OK_READ | TEST_OK_WRITE)
#define TEST_OK_PREW	(TEST_OK_PROBE | TEST_OK_READ | TEST_OK_ERASE | TEST_OK_WRITE)
#define TEST_OK_MASK	0x0f

#define TEST_BAD_PROBE	(1 << 4)
#define TEST_BAD_READ	(1 << 5)
#define TEST_BAD_ERASE	(1 << 6)
#define TEST_BAD_WRITE	(1 << 7)
#define TEST_BAD_PREW	(TEST_BAD_PROBE | TEST_BAD_READ | TEST_BAD_ERASE | TEST_BAD_WRITE)
#define TEST_BAD_MASK	0xf0

/* Timing used in probe routines. ZERO is -2 to differentiate between an unset
 * field and zero delay.
 *
 * SPI devices will always have zero delay and ignore this field.
 */
#define TIMING_FIXME	-1
/* this is intentionally same value as fixme */
#define TIMING_IGNORED	-1
#define TIMING_ZERO	-2

/* Something happened that shouldn't happen, but we can go on */
#define ERROR_NONFATAL 0x100

#define MSG_ERROR	0
#define MSG_INFO	1
#define MSG_DEBUG	2
#define MSG_BARF	3
#define msg_gerr(...)	print(MSG_ERROR, __VA_ARGS__)	/* general errors */
#define msg_perr(...)	print(MSG_ERROR, __VA_ARGS__)	/* programmer errors */
#define msg_cerr(...)	print(MSG_ERROR, __VA_ARGS__)	/* chip errors */
#define msg_ginfo(...)	print(MSG_INFO, __VA_ARGS__)	/* general info */
#define msg_pinfo(...)	print(MSG_INFO, __VA_ARGS__)	/* programmer info */
#define msg_cinfo(...)	print(MSG_INFO, __VA_ARGS__)	/* chip info */
#define msg_gdbg(...)	print(MSG_DEBUG, __VA_ARGS__)	/* general debug */
#define msg_pdbg(...)	print(MSG_DEBUG, __VA_ARGS__)	/* programmer debug */
#define msg_cdbg(...)	print(MSG_DEBUG, __VA_ARGS__)	/* chip debug */
#define msg_gspew(...)	print(MSG_BARF, __VA_ARGS__)	/* general debug barf  */
#define msg_pspew(...)	print(MSG_BARF, __VA_ARGS__)	/* programmer debug barf  */
#define msg_cspew(...)	print(MSG_BARF, __VA_ARGS__)	/* chip debug barf  */

struct spi_command {
	unsigned int writecnt;
	unsigned int readcnt;
	const unsigned char *writearr;
	unsigned char *readarr;
};

/*
 * programmer.h
 */

enum spi_controller {
	SPI_CONTROLLER_NONE,
	SPI_CONTROLLER_SB600,
};

struct spi_programmer {
	int (*command)(unsigned int writecnt, unsigned int readcnt,
		   const unsigned char *writearr, unsigned char *readarr);
	int (*multicommand)(struct spi_command *cmds);

	/* Optimized functions for this programmer */
	int (*read)(struct flashchip *flash, uint8_t *buf, int start, int len);
	int (*write_256)(struct flashchip *flash, uint8_t *buf, int start, int len);
};

static int default_spi_send_multicommand(struct spi_command *cmds);

#define rpci_write_byte	pci_write_byte

static int sb600_spi_send_command(unsigned int writecnt, unsigned int readcnt,
		      const unsigned char *writearr, unsigned char *readarr);
static int sb600_spi_read(struct flashchip *flash, uint8_t *buf, int start, int len);
static int sb600_spi_write_256(struct flashchip *flash, uint8_t *buf, int start, int len);

/*
 * chipdrivers.h
 */

static uint8_t spi_read_status_register(void);

/*
 * hwaccess.h
 */

#define OUTB outb
#define OUTW outw
#define OUTL outl
#define INB  inb
#define INW  inw
#define INL  inl

/*
 * jedec.c
 */

/* Check one byte for odd parity */
static uint8_t oddparity(uint8_t val)
{
	val = (val ^ (val >> 4)) & 0xf;
	val = (val ^ (val >> 2)) & 0x3;
	return (val ^ (val >> 1)) & 0x1;
}

/*
 * spi.c
 */

static enum spi_controller spi_controller = SPI_CONTROLLER_NONE;

static const struct spi_programmer spi_programmer[] = {
	{ /* SPI_CONTROLLER_NONE */
		.command = NULL,
		.multicommand = NULL,
		.read = NULL,
		.write_256 = NULL,
	},

	{ /* SPI_CONTROLLER_SB600 */
		.command = sb600_spi_send_command,
		.multicommand = default_spi_send_multicommand,
		.read = sb600_spi_read,
		.write_256 = sb600_spi_write_256,
	},
};

static int spi_send_command(unsigned int writecnt, unsigned int readcnt,
		const unsigned char *writearr, unsigned char *readarr)
{
	if (!spi_programmer[spi_controller].command) {
		msg_perr("%s called, but SPI is unsupported on this "
			 "hardware. Please report a bug at "
			 "flashrom@flashrom.org\n", __func__);
		return 1;
	}

	return spi_programmer[spi_controller].command(writecnt, readcnt,
						      writearr, readarr);
}

static int spi_send_multicommand(struct spi_command *cmds)
{
	if (!spi_programmer[spi_controller].multicommand) {
		msg_perr("%s called, but SPI is unsupported on this "
			 "hardware. Please report a bug at "
			 "flashrom@flashrom.org\n", __func__);
		return 1;
	}

	return spi_programmer[spi_controller].multicommand(cmds);
}

static int default_spi_send_multicommand(struct spi_command *cmds)
{
	int result = 0;

	for (; (cmds->writecnt || cmds->readcnt) && !result; cmds++) {
		result = spi_send_command(cmds->writecnt, cmds->readcnt,
					  cmds->writearr, cmds->readarr);
	}
	return result;
}

/*
 * spi25.c
 */

static int spi_rdid(unsigned char *readarr, int bytes)
{
	static const unsigned char cmd[JEDEC_RDID_OUTSIZE] = { JEDEC_RDID };
	int ret;
	int i;

	ret = spi_send_command(sizeof(cmd), bytes, cmd, readarr);
	if (ret)
		return ret;
	msg_cspew("RDID returned");
	for (i = 0; i < bytes; i++)
		msg_cspew(" 0x%02x", readarr[i]);
	msg_cspew(". ");
	return 0;
}

static void spi_prettyprint_status_register(struct flashchip *flash)
{
	msg_cdbg("Chip status register is %02x\n", spi_read_status_register());
}

static int probe_spi_rdid_generic(struct flashchip *flash, int bytes)
{
	unsigned char readarr[4];
	uint32_t id1;
	uint32_t id2;

	if (spi_rdid(readarr, bytes))
		return 0;

	if (!oddparity(readarr[0]))
		msg_cdbg("RDID byte 0 parity violation. ");

	/* Check if this is a continuation vendor ID.
	 * FIXME: Handle continuation device IDs.
	 */
	if (readarr[0] == 0x7f) {
		if (!oddparity(readarr[1]))
			msg_cdbg("RDID byte 1 parity violation. ");
		id1 = (readarr[0] << 8) | readarr[1];
		id2 = readarr[2];
		if (bytes > 3) {
			id2 <<= 8;
			id2 |= readarr[3];
		}
	} else {
		id1 = readarr[0];
		id2 = (readarr[1] << 8) | readarr[2];
	}

	msg_cdbg("%s: id1 0x%02x, id2 0x%02x\n", __func__, id1, id2);

	if (id1 == flash->manufacture_id && id2 == flash->model_id) {
		/* Print the status register to tell the
		 * user about possible write protection.
		 */
		spi_prettyprint_status_register(flash);

		return 1;
	}

	/* Test if this is a pure vendor match. */
	if (id1 == flash->manufacture_id &&
	    GENERIC_DEVICE_ID == flash->model_id)
		return 1;

	/* Test if there is any vendor ID. */
	if (GENERIC_MANUF_ID == flash->manufacture_id &&
	    id1 != 0xff)
		return 1;

	return 0;
}

static int probe_spi_rdid(struct flashchip *flash)
{
	return probe_spi_rdid_generic(flash, 3);
}

static uint8_t spi_read_status_register(void)
{
	static const unsigned char cmd[JEDEC_RDSR_OUTSIZE] = { JEDEC_RDSR };
	/* FIXME: No workarounds for driver/hardware bugs in generic code. */
	unsigned char readarr[2]; /* JEDEC_RDSR_INSIZE=1 but wbsio needs 2 */
	int ret;

	/* Read Status Register */
	ret = spi_send_command(sizeof(cmd), sizeof(readarr), cmd, readarr);
	if (ret)
		msg_cerr("RDSR failed!\n");

	return readarr[0];
}

/*
 * This is according the SST25VF016 datasheet, who knows it is more
 * generic that this...
 */
static int spi_write_status_register_ewsr(struct flashchip *flash, int status)
{
	int result;
	int i = 0;
	struct spi_command cmds[] = {
	{
	/* WRSR requires either EWSR or WREN depending on chip type. */
		.writecnt	= JEDEC_EWSR_OUTSIZE,
		.writearr	= (const unsigned char[]){ JEDEC_EWSR },
		.readcnt	= 0,
		.readarr	= NULL,
	}, {
		.writecnt	= JEDEC_WRSR_OUTSIZE,
		.writearr	= (const unsigned char[]){ JEDEC_WRSR, (unsigned char) status },
		.readcnt	= 0,
		.readarr	= NULL,
	}, {
		.writecnt	= 0,
		.writearr	= NULL,
		.readcnt	= 0,
		.readarr	= NULL,
	}};

	result = spi_send_multicommand(cmds);
	if (result) {
		msg_cerr("%s failed during command execution\n",
			__func__);
		/* No point in waiting for the command to complete if execution
		 * failed.
		 */
		return result;
	}
	/* WRSR performs a self-timed erase before the changes take effect.
	 * This may take 50-85 ms in most cases, and some chips apparently
	 * allow running RDSR only once. Therefore pick an initial delay of
	 * 100 ms, then wait in 10 ms steps until a total of 5 s have elapsed.
	 */
	programmer_delay(100 * 1000);
	while (spi_read_status_register() & JEDEC_RDSR_BIT_WIP) {
		if (++i > 490) {
			msg_cerr("Error: WIP bit after WRSR never cleared\n");
			return TIMEOUT_ERROR;
		}
		programmer_delay(10 * 1000);
	}
	return 0;
}

static int spi_write_status_register_wren(struct flashchip *flash, int status)
{
	int result;
	int i = 0;
	struct spi_command cmds[] = {
	{
	/* WRSR requires either EWSR or WREN depending on chip type. */
		.writecnt	= JEDEC_WREN_OUTSIZE,
		.writearr	= (const unsigned char[]){ JEDEC_WREN },
		.readcnt	= 0,
		.readarr	= NULL,
	}, {
		.writecnt	= JEDEC_WRSR_OUTSIZE,
		.writearr	= (const unsigned char[]){ JEDEC_WRSR, (unsigned char) status },
		.readcnt	= 0,
		.readarr	= NULL,
	}, {
		.writecnt	= 0,
		.writearr	= NULL,
		.readcnt	= 0,
		.readarr	= NULL,
	}};

	result = spi_send_multicommand(cmds);
	if (result) {
		msg_cerr("%s failed during command execution\n",
			__func__);
		/* No point in waiting for the command to complete if execution
		 * failed.
		 */
		return result;
	}
	/* WRSR performs a self-timed erase before the changes take effect.
	 * This may take 50-85 ms in most cases, and some chips apparently
	 * allow running RDSR only once. Therefore pick an initial delay of
	 * 100 ms, then wait in 10 ms steps until a total of 5 s have elapsed.
	 */
	programmer_delay(100 * 1000);
	while (spi_read_status_register() & JEDEC_RDSR_BIT_WIP) {
		if (++i > 490) {
			msg_cerr("Error: WIP bit after WRSR never cleared\n");
			return TIMEOUT_ERROR;
		}
		programmer_delay(10 * 1000);
	}
	return 0;
}

static int spi_write_status_register(struct flashchip *flash, int status)
{
	int ret = 1;

	if (!(flash->feature_bits & (FEATURE_WRSR_WREN | FEATURE_WRSR_EWSR))) {
		msg_cdbg("Missing status register write definition, assuming "
			 "EWSR is needed\n");
		flash->feature_bits |= FEATURE_WRSR_EWSR;
	}
	if (flash->feature_bits & FEATURE_WRSR_WREN)
		ret = spi_write_status_register_wren(flash, status);
	if (ret && (flash->feature_bits & FEATURE_WRSR_EWSR))
		ret = spi_write_status_register_ewsr(flash, status);
	return ret;
}

static int spi_enable_blockprotect_at25df(struct flashchip *flash)
{
	uint8_t status;
	int result;

	/* Reset SPRL when needed to be able to modify status register */
	status = spi_read_status_register();
	if (status & (1 << 7)) {
		msg_cdbg("Need to disable Sector Protection Register Lock\n");
		if ((status & (1 << 4)) == 0) {
			msg_cerr("WP# pin is active, disabling "
				 "write protection is impossible.\n");
			return 1;
		}
		/* All bits except bit 7 (SPRL) are readonly. */
		result = spi_write_status_register(flash, status & ~(1 << 7));
		if (result) {
			msg_cerr("spi_write_status_register failed\n");
			return result;
		}

	}
	/* Global protect. Make sure to set SPRL as well. */
	result = spi_write_status_register(flash, status | 0xbc);
	if (result) {
		msg_cerr("spi_write_status_register failed\n");
		return result;
	}
	status = spi_read_status_register();
	if ((status & (3 << 2)) != (3 << 2)) {
		msg_cerr("Block protection could not be enabled!\n");
		return 1;
	}
	msg_cinfo("Block protection enabled, %s locked!\n",
		  (status & (1 << 4)) ? "software" : "hardware");
	return 0;
}

/*
 * hwaccess.c
 */

static inline void mmio_writeb(uint8_t val, void *addr)
{
	*(volatile uint8_t *) addr = val;
}

static inline void mmio_writew(uint16_t val, void *addr)
{
	*(volatile uint16_t *) addr = val;
}

static inline void mmio_writel(uint32_t val, void *addr)
{
	*(volatile uint32_t *) addr = val;
}

static inline uint8_t mmio_readb(void *addr)
{
	return *(volatile uint8_t *) addr;
}

static inline uint16_t mmio_readw(void *addr)
{
	return *(volatile uint16_t *) addr;
}

static inline uint32_t mmio_readl(void *addr)
{
	return *(volatile uint32_t *) addr;
}

/*
 * physmap.c
 */

static inline void *physmap(const char *descr, unsigned long phys_addr,
			    size_t len)
{
	return phys_to_virt(phys_addr);
}

static inline void physunmap(void *virt_addr, size_t len)
{
}

/*
 * internal.c
 */

static struct pci_dev *pci_dev_find(uint16_t vendor, uint16_t device)
{
	static struct pci_access *pacc;
	struct pci_dev *temp;
	struct pci_filter filter;

	if (pacc == NULL) {
		pacc = pci_alloc();     /* Get the pci_access structure */
		pci_init(pacc);         /* Initialize the PCI library */
		pci_scan_bus(pacc);     /* We want to get the list of devices */
	}

	pci_filter_init(NULL, &filter);
	filter.vendor = vendor;
	filter.device = device;

	for (temp = pacc->devices; temp; temp = temp->next)
		if (pci_filter_match(&filter, temp))
			return temp;

	return NULL;
}

/*
 * flashchips.c
 */

/**
 * List of supported flash chips.
 *
 * Please keep the list sorted by vendor name and chip name, so that
 * the output of 'flashrom -L' is alphabetically sorted.
 */
static struct flashchip flashchips[] = {
	{
		.vendor		= "Atmel",
		.name		= "AT26DF081A",
		.bustype	= CHIP_BUSTYPE_SPI,
		.manufacture_id	= ATMEL_ID,
		.model_id	= ATMEL_AT26DF081A,
		.total_size	= 1024,
		.page_size	= 256,
		.feature_bits	= FEATURE_WRSR_WREN,
		.tested		= TEST_OK_PREW,
		.probe		= probe_spi_rdid,
		.probe_timing	= TIMING_ZERO,
#if 0			/* not needed */
		.block_erasers	=
		{
			{
				.eraseblocks = { {4 * 1024, 256} },
				.block_erase = spi_block_erase_20,
			}, {
				.eraseblocks = { {32 * 1024, 32} },
				.block_erase = spi_block_erase_52,
			}, {
				.eraseblocks = { {64 * 1024, 16} },
				.block_erase = spi_block_erase_d8,
			}, {
				.eraseblocks = { {1024 * 1024, 1} },
				.block_erase = spi_block_erase_60,
			}, {
				.eraseblocks = { {1024 * 1024, 1} },
				.block_erase = spi_block_erase_c7,
			}
		},
		.unlock		= spi_disable_blockprotect_at25df,
#endif
		.lock		= spi_enable_blockprotect_at25df,
#if 0			/* not needed */
		.write		= spi_chip_write_256,
		.read		= spi_chip_read,
#endif
	},

	{ NULL	}
};

/*
 * flashrom.c
 */

/* Supported buses for the current programmer. */
static enum chipbustype buses_supported;

/* Is writing allowed with this programmer? */
static int programmer_may_write;

/* If nonzero, used as the start address of bottom-aligned flash. */
static unsigned long flashbase;

static inline void programmer_delay(int usecs)
{
	udelay(usecs);
}

static struct flashchip *probe_flash(struct flashchip *first_flash, int force)
{
	struct flashchip *flash;
	unsigned long base = 0;
	char location[64];
	uint32_t size;
	enum chipbustype buses_common;

	for (flash = first_flash; flash && flash->name; flash++) {
		msg_gdbg("Probing for %s %s, %d KB: ",
			     flash->vendor, flash->name, flash->total_size);
		if (!flash->probe && !force) {
			msg_gdbg("failed! flashrom has no probe function for "
				 "this flash chip.\n");
			continue;
		}
		buses_common = buses_supported & flash->bustype;
		if (!buses_common) {
			msg_gdbg("skipped.");
			msg_gspew(" Host bus type and chip bus type are incompatible.");
			msg_gdbg("\n");
			continue;
		}

		size = flash->total_size * 1024;

		base = flashbase ? flashbase : (0xffffffff - size + 1);
		flash->virtual_memory = (chipaddr)physmap("flash chip", base, size);

		if (force)
			break;

		if (flash->probe(flash) != 1)
			goto notfound;

		if (first_flash == flashchips
		    || flash->model_id != GENERIC_DEVICE_ID)
			break;

notfound:
		physunmap((void *)flash->virtual_memory, size);
	}

	if (!flash || !flash->name)
		return NULL;

	snprintf(location, sizeof(location), "at physical address 0x%lx", base);

	msg_cinfo("%s chip \"%s %s\" (%d KB) %s.\n",
	       force ? "Assuming" : "Found",
	       flash->vendor, flash->name, flash->total_size, location);

	/* Flash registers will not be mapped if the chip was forced. Lock info
	 * may be stored in registers, so avoid lock info printing.
	 */
	if (!force)
		if (flash->printlock)
			flash->printlock(flash);

	return flash;
}

/*
 * sb600spi.c
 */

static uint8_t *sb600_spibar = NULL;

static int sb600_spi_read(struct flashchip *flash, uint8_t *buf, int start, int len)
{
	/* not needed */
	msg_perr("%s: unsupported!\n", __func__);
	return SPI_PROGRAMMER_ERROR;
}

static int sb600_spi_write_256(struct flashchip *flash, uint8_t *buf, int start, int len)
{
	/* not needed */
	msg_perr("%s: unsupported!\n", __func__);
	return SPI_PROGRAMMER_ERROR;
}

static void reset_internal_fifo_pointer(void)
{
	mmio_writeb(mmio_readb(sb600_spibar + 2) | 0x10, sb600_spibar + 2);

	/* FIXME: This loop makes no sense at all. */
	while (mmio_readb(sb600_spibar + 0xD) & 0x7)
		msg_pspew("reset\n");
}

static int compare_internal_fifo_pointer(uint8_t want)
{
	uint8_t tmp;

	tmp = mmio_readb(sb600_spibar + 0xd) & 0x07;
	want &= 0x7;
	if (want != tmp) {
		msg_perr("SB600 FIFO pointer corruption! Pointer is %d, wanted "
			 "%d\n", tmp, want);
		msg_perr("Something else is accessing the flash chip and "
			 "causes random corruption.\nPlease stop all "
			 "applications and drivers and IPMI which access the "
			 "flash chip.\n");
		return 1;
	} else {
		msg_pspew("SB600 FIFO pointer is %d, wanted %d\n", tmp, want);
		return 0;
	}
}

static int reset_compare_internal_fifo_pointer(uint8_t want)
{
	int ret;

	ret = compare_internal_fifo_pointer(want);
	reset_internal_fifo_pointer();
	return ret;
}

static void execute_command(void)
{
	mmio_writeb(mmio_readb(sb600_spibar + 2) | 1, sb600_spibar + 2);

	while (mmio_readb(sb600_spibar + 2) & 1)
		;
}

static int sb600_spi_send_command(unsigned int writecnt, unsigned int readcnt,
		      const unsigned char *writearr, unsigned char *readarr)
{
	int count;
	/* First byte is cmd which can not being sent through FIFO. */
	unsigned char cmd = *writearr++;
	unsigned int readoffby1;
	unsigned char readwrite;

	writecnt--;

	msg_pspew("%s, cmd=%x, writecnt=%x, readcnt=%x\n",
		  __func__, cmd, writecnt, readcnt);

	if (readcnt > 8) {
		msg_pinfo("%s, SB600 SPI controller can not receive %d bytes, "
		       "it is limited to 8 bytes\n", __func__, readcnt);
		return SPI_INVALID_LENGTH;
	}

	if (writecnt > 8) {
		msg_pinfo("%s, SB600 SPI controller can not send %d bytes, "
		       "it is limited to 8 bytes\n", __func__, writecnt);
		return SPI_INVALID_LENGTH;
	}

	/* This is a workaround for a bug in SB600 and SB700. If we only send
	 * an opcode and no additional data/address, the SPI controller will
	 * read one byte too few from the chip. Basically, the last byte of
	 * the chip response is discarded and will not end up in the FIFO.
	 * It is unclear if the CS# line is set high too early as well.
	 */
	readoffby1 = (writecnt) ? 0 : 1;
	readwrite = (readcnt + readoffby1) << 4 | (writecnt);
	mmio_writeb(readwrite, sb600_spibar + 1);
	mmio_writeb(cmd, sb600_spibar + 0);

	/* Before we use the FIFO, reset it first. */
	reset_internal_fifo_pointer();

	/* Send the write byte to FIFO. */
	msg_pspew("Writing: ");
	for (count = 0; count < writecnt; count++, writearr++) {
		msg_pspew("[%02x]", *writearr);
		mmio_writeb(*writearr, sb600_spibar + 0xC);
	}
	msg_pspew("\n");

	/*
	 * We should send the data by sequence, which means we need to reset
	 * the FIFO pointer to the first byte we want to send.
	 */
	if (reset_compare_internal_fifo_pointer(writecnt))
		return SPI_PROGRAMMER_ERROR;

	msg_pspew("Executing: \n");
	execute_command();

	/*
	 * After the command executed, we should find out the index of the
	 * received byte. Here we just reset the FIFO pointer and skip the
	 * writecnt.
	 * It would be possible to increase the FIFO pointer by one instead
	 * of reading and discarding one byte from the FIFO.
	 * The FIFO is implemented on top of an 8 byte ring buffer and the
	 * buffer is never cleared. For every byte that is shifted out after
	 * the opcode, the FIFO already stores the response from the chip.
	 * Usually, the chip will respond with 0x00 or 0xff.
	 */
	if (reset_compare_internal_fifo_pointer(writecnt + readcnt))
		return SPI_PROGRAMMER_ERROR;

	/* Skip the bytes we sent. */
	msg_pspew("Skipping: ");
	for (count = 0; count < writecnt; count++) {
		cmd = mmio_readb(sb600_spibar + 0xC);
		msg_pspew("[%02x]", cmd);
	}
	msg_pspew("\n");
	if (compare_internal_fifo_pointer(writecnt))
		return SPI_PROGRAMMER_ERROR;

	msg_pspew("Reading: ");
	for (count = 0; count < readcnt; count++, readarr++) {
		*readarr = mmio_readb(sb600_spibar + 0xC);
		msg_pspew("[%02x]", *readarr);
	}
	msg_pspew("\n");
	if (reset_compare_internal_fifo_pointer(readcnt + writecnt))
		return SPI_PROGRAMMER_ERROR;

	if (mmio_readb(sb600_spibar + 1) != readwrite) {
		msg_perr("Unexpected change in SB600 read/write count!\n");
		msg_perr("Something else is accessing the flash chip and "
			 "causes random corruption.\nPlease stop all "
			 "applications and drivers and IPMI which access the "
			 "flash chip.\n");
		return SPI_PROGRAMMER_ERROR;
	}

	return 0;
}

static int sb600_probe_spi(struct pci_dev *dev)
{
	struct pci_dev *smbus_dev;
	uint32_t tmp;
	uint8_t reg;
#ifdef DEBUG
	static const char *const speed_names[4] = {
		"Reserved", "33", "22", "16.5"
	};
#endif

	/* Read SPI_BaseAddr */
	tmp = pci_read_long(dev, 0xa0);
	tmp &= 0xffffffe0;	/* remove bits 4-0 (reserved) */
	msg_pdbg("SPI base address is at 0x%x\n", tmp);

	/* If the BAR has address 0, it is unlikely SPI is used. */
	if (!tmp)
		return 0;

	/* Physical memory has to be mapped at page (4k) boundaries. */
	sb600_spibar = physmap("SB600 SPI registers", tmp & 0xfffff000,
			       0x1000);
	/* The low bits of the SPI base address are used as offset into
	 * the mapped page.
	 */
	sb600_spibar += tmp & 0xfff;

	tmp = pci_read_long(dev, 0xa0);
	msg_pdbg("AltSpiCSEnable=%i, SpiRomEnable=%i, "
		     "AbortEnable=%i\n", tmp & 0x1, (tmp & 0x2) >> 1,
		     (tmp & 0x4) >> 2);
	tmp = (pci_read_byte(dev, 0xba) & 0x4) >> 2;
	msg_pdbg("PrefetchEnSPIFromIMC=%i, ", tmp);

	tmp = pci_read_byte(dev, 0xbb);
	/* FIXME: Set bit 3,6,7 if not already set.
	 * Set bit 5, otherwise SPI accesses are pointless in LPC mode.
	 * See doc 42413 AMD SB700/710/750 RPR.
	 */
	msg_pdbg("PrefetchEnSPIFromHost=%i, SpiOpEnInLpcMode=%i\n",
		     tmp & 0x1, (tmp & 0x20) >> 5);
	tmp = mmio_readl(sb600_spibar);
	/* FIXME: If SpiAccessMacRomEn or SpiHostAccessRomEn are zero on
	 * SB700 or later, reads and writes will be corrupted. Abort in this
	 * case. Make sure to avoid this check on SB600.
	 */
	msg_pdbg("SpiArbEnable=%i, SpiAccessMacRomEn=%i, "
		     "SpiHostAccessRomEn=%i, ArbWaitCount=%i, "
		     "SpiBridgeDisable=%i, DropOneClkOnRd=%i\n",
		     (tmp >> 19) & 0x1, (tmp >> 22) & 0x1,
		     (tmp >> 23) & 0x1, (tmp >> 24) & 0x7,
		     (tmp >> 27) & 0x1, (tmp >> 28) & 0x1);
	tmp = (mmio_readb(sb600_spibar + 0xd) >> 4) & 0x3;
	msg_pdbg("NormSpeed is %s MHz\n", speed_names[tmp]);

	/* Look for the SMBus device. */
	smbus_dev = pci_dev_find(0x1002, 0x4385);

	if (!smbus_dev) {
		msg_perr("ERROR: SMBus device not found. Not enabling SPI.\n");
		return ERROR_NONFATAL;
	}

	/* Note about the bit tests below: If a bit is zero, the GPIO is SPI. */
	/* GPIO11/SPI_DO and GPIO12/SPI_DI status */
	reg = pci_read_byte(smbus_dev, 0xAB);
	reg &= 0xC0;
	msg_pdbg("GPIO11 used for %s\n", (reg & (1 << 6)) ? "GPIO" : "SPI_DO");
	msg_pdbg("GPIO12 used for %s\n", (reg & (1 << 7)) ? "GPIO" : "SPI_DI");
	if (reg != 0x00) {
		msg_pdbg("Not enabling SPI");
		return 0;
	}
	/* GPIO31/SPI_HOLD and GPIO32/SPI_CS status */
	reg = pci_read_byte(smbus_dev, 0x83);
	reg &= 0xC0;
	msg_pdbg("GPIO31 used for %s\n", (reg & (1 << 6)) ? "GPIO" : "SPI_HOLD");
	msg_pdbg("GPIO32 used for %s\n", (reg & (1 << 7)) ? "GPIO" : "SPI_CS");
	/* SPI_HOLD is not used on all boards, filter it out. */
	if ((reg & 0x80) != 0x00) {
		msg_pdbg("Not enabling SPI");
		return 0;
	}
	/* GPIO47/SPI_CLK status */
	reg = pci_read_byte(smbus_dev, 0xA7);
	reg &= 0x40;
	msg_pdbg("GPIO47 used for %s\n", (reg & (1 << 6)) ? "GPIO" : "SPI_CLK");
	if (reg != 0x00) {
		msg_pdbg("Not enabling SPI");
		return 0;
	}

	reg = pci_read_byte(dev, 0x40);
	msg_pdbg("SB700 IMC is %sactive.\n", (reg & (1 << 7)) ? "" : "not ");
	if (reg & (1 << 7)) {
		/* If we touch any region used by the IMC, the IMC and the SPI
		 * interface will lock up, and the only way to recover is a
		 * hard reset, but that is a bad choice for a half-erased or
		 * half-written flash chip.
		 * There appears to be an undocumented register which can freeze
		 * or disable the IMC, but for now we want to play it safe.
		 */
		msg_perr("The SB700 IMC is active and may interfere with SPI "
			 "commands. Disabling write.\n");
		/* FIXME: Should we only disable SPI writes, or will the lockup
		 * affect LPC/FWH chips as well?
		 */
		programmer_may_write = 0;
	}

	/* Bring the FIFO to a clean state. */
	reset_internal_fifo_pointer();

	buses_supported |= CHIP_BUSTYPE_SPI;
	spi_controller = SPI_CONTROLLER_SB600;
	return 0;
}

/*
 * chipset_enable.c
 */

static int enable_flash_sb600(struct pci_dev *dev)
{
	uint32_t prot;
	uint8_t reg;
	int ret;

	/* Clear ROM protect 0-3. */
	for (reg = 0x50; reg < 0x60; reg += 4) {
		prot = pci_read_long(dev, reg);
		/* No protection flags for this region?*/
		if ((prot & 0x3) == 0)
			continue;
		msg_pinfo("SB600 %s%sprotected from 0x%08x to 0x%08x\n",
			(prot & 0x1) ? "write " : "",
			(prot & 0x2) ? "read " : "",
			(prot & 0xfffff800),
			(prot & 0xfffff800) + (((prot & 0x7fc) << 8) | 0x3ff));
		prot &= 0xfffffffc;
		rpci_write_byte(dev, reg, prot);
		prot = pci_read_long(dev, reg);
		if (prot & 0x3)
			msg_perr("SB600 %s%sunprotect failed from 0x%08x to 0x%08x\n",
				(prot & 0x1) ? "write " : "",
				(prot & 0x2) ? "read " : "",
				(prot & 0xfffff800),
				(prot & 0xfffff800) + (((prot & 0x7fc) << 8) | 0x3ff));
	}

	buses_supported = CHIP_BUSTYPE_LPC | CHIP_BUSTYPE_FWH;

	ret = sb600_probe_spi(dev);

	/* Read ROM strap override register. */
	OUTB(0x8f, 0xcd6);
	reg = INB(0xcd7);
	reg &= 0x0e;
	msg_pdbg("ROM strap override is %sactive", (reg & 0x02) ? "" : "not ");
	if (reg & 0x02) {
		switch ((reg & 0x0c) >> 2) {
		case 0x00:
			msg_pdbg(": LPC");
			break;
		case 0x01:
			msg_pdbg(": PCI");
			break;
		case 0x02:
			msg_pdbg(": FWH");
			break;
		case 0x03:
			msg_pdbg(": SPI");
			break;
		}
	}
	msg_pdbg("\n");

	/* Force enable SPI ROM in SB600 PM register.
	 * If we enable SPI ROM here, we have to disable it after we leave.
	 * But how can we know which ROM we are going to handle? So we have
	 * to trade off. We only access LPC ROM if we boot via LPC ROM. And
	 * only SPI ROM if we boot via SPI ROM. If you want to access SPI on
	 * boards with LPC straps, you have to use the code below.
	 */
	/*
	OUTB(0x8f, 0xcd6);
	OUTB(0x0e, 0xcd7);
	*/

	return ret;
}

int sb600_spi_lockdown(void) {
	struct flashchip *flash;
	struct pci_dev *dev;

	dev = pci_dev_find(0x1002, 0x438d);
	if (!dev) {
		debug("Failed to find SB600 LPC bridge!\n");

		return -1;
	}

	enable_flash_sb600(dev);

	if (spi_controller == SPI_CONTROLLER_NONE) {
		debug("Failed to enable SPI controller!\n");

		return -1;
	}

	flash = probe_flash(flashchips, 0);
	if (!flash) {
		debug("Failed to find flash chip!\n");

		return -1;
	}

	if (!flash->lock) {
		debug("Flash chip cannot be locked!\n");

		return -1;
	}

	if (flash->lock(flash)) {
		debug("Failed to lock flash chip (already locked?)!\n");

		/* signal this case differently, the chip may already be locked */
		return 1;
	}

	return 0;
}
