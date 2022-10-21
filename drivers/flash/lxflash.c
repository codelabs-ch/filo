/*******************************************************************************
 *
 *	Geode LX CS5536 flash driver
 *
 *	Copyright 2006 Andrei Birjukov <andrei.birjukov@artecdesign.ee> and
 *	Artec Design LLC http://www.artecdesign.ee
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ******************************************************************************/

#include <libpayload.h>
#include <config.h>
#include <fs.h>
#include <arch/io.h>
#include "lxflash.h"

#define DEBUG_THIS CONFIG_DEBUG_FLASH
#include <debug.h>

////////////////////////////////////////////////////////////////////////////////
// driver globals

static FLASH_INFO g_flashInfo;		// flash info structure
static u32	g_deviceID = 0;		// flash memory ID
static int32_t	g_chipID = -1;		// chip ID
static u16	g_baseAddr = 0;		// port mapped controller IO base address

static u8 g_eccTest[MAX_ECC_SIZE];	// used to retrieve/store ECC
static u8 g_eccCalc[MAX_ECC_SIZE];

static u32 g_currentPage = (u32)-1;
static u8 g_pageBuf[MAX_PAGE_SIZE];
static u8 *g_pBBT=NULL;

static msr_t g_orig_flsh;

////////////////////////////////////////////////////////////////////////////////
// ECC structs and routines

/*
 * Pre-calculated 256-way 1 byte column parity
 */
static const u8 nand_ecc_precalc_table[] = {
	0x00, 0x55, 0x56, 0x03, 0x59, 0x0c, 0x0f, 0x5a, 0x5a, 0x0f, 0x0c, 0x59, 0x03, 0x56, 0x55, 0x00,
	0x65, 0x30, 0x33, 0x66, 0x3c, 0x69, 0x6a, 0x3f, 0x3f, 0x6a, 0x69, 0x3c, 0x66, 0x33, 0x30, 0x65,
	0x66, 0x33, 0x30, 0x65, 0x3f, 0x6a, 0x69, 0x3c, 0x3c, 0x69, 0x6a, 0x3f, 0x65, 0x30, 0x33, 0x66,
	0x03, 0x56, 0x55, 0x00, 0x5a, 0x0f, 0x0c, 0x59, 0x59, 0x0c, 0x0f, 0x5a, 0x00, 0x55, 0x56, 0x03,
	0x69, 0x3c, 0x3f, 0x6a, 0x30, 0x65, 0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6a, 0x3f, 0x3c, 0x69,
	0x0c, 0x59, 0x5a, 0x0f, 0x55, 0x00, 0x03, 0x56, 0x56, 0x03, 0x00, 0x55, 0x0f, 0x5a, 0x59, 0x0c,
	0x0f, 0x5a, 0x59, 0x0c, 0x56, 0x03, 0x00, 0x55, 0x55, 0x00, 0x03, 0x56, 0x0c, 0x59, 0x5a, 0x0f,
	0x6a, 0x3f, 0x3c, 0x69, 0x33, 0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3c, 0x3f, 0x6a,
	0x6a, 0x3f, 0x3c, 0x69, 0x33, 0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3c, 0x3f, 0x6a,
	0x0f, 0x5a, 0x59, 0x0c, 0x56, 0x03, 0x00, 0x55, 0x55, 0x00, 0x03, 0x56, 0x0c, 0x59, 0x5a, 0x0f,
	0x0c, 0x59, 0x5a, 0x0f, 0x55, 0x00, 0x03, 0x56, 0x56, 0x03, 0x00, 0x55, 0x0f, 0x5a, 0x59, 0x0c,
	0x69, 0x3c, 0x3f, 0x6a, 0x30, 0x65, 0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6a, 0x3f, 0x3c, 0x69,
	0x03, 0x56, 0x55, 0x00, 0x5a, 0x0f, 0x0c, 0x59, 0x59, 0x0c, 0x0f, 0x5a, 0x00, 0x55, 0x56, 0x03,
	0x66, 0x33, 0x30, 0x65, 0x3f, 0x6a, 0x69, 0x3c, 0x3c, 0x69, 0x6a, 0x3f, 0x65, 0x30, 0x33, 0x66,
	0x65, 0x30, 0x33, 0x66, 0x3c, 0x69, 0x6a, 0x3f, 0x3f, 0x6a, 0x69, 0x3c, 0x66, 0x33, 0x30, 0x65,
	0x00, 0x55, 0x56, 0x03, 0x59, 0x0c, 0x0f, 0x5a, 0x5a, 0x0f, 0x0c, 0x59, 0x03, 0x56, 0x55, 0x00
};

/**
 * nand_trans_result - [GENERIC] create non-inverted ECC
 * @reg2:	line parity reg 2
 * @reg3:	line parity reg 3
 * @ecc_code:	ecc
 *
 * Creates non-inverted ECC code from line parity
 */
static void NAND_transResult(u8 reg2, u8 reg3,
	u8 *ecc_code)
{
	u8 a, b, i, tmp1, tmp2;

	/* Initialize variables */
	a = b = 0x80;
	tmp1 = tmp2 = 0;

	/* Calculate first ECC byte */
	for (i = 0; i < 4; i++) {
		if (reg3 & a)		/* LP15,13,11,9 --> ecc_code[0] */
			tmp1 |= b;
		b >>= 1;
		if (reg2 & a)		/* LP14,12,10,8 --> ecc_code[0] */
			tmp1 |= b;
		b >>= 1;
		a >>= 1;
	}

	/* Calculate second ECC byte */
	b = 0x80;
	for (i = 0; i < 4; i++) {
		if (reg3 & a)		/* LP7,5,3,1 --> ecc_code[1] */
			tmp2 |= b;
		b >>= 1;
		if (reg2 & a)		/* LP6,4,2,0 --> ecc_code[1] */
			tmp2 |= b;
		b >>= 1;
		a >>= 1;
	}

	/* Store two of the ECC bytes */
	ecc_code[0] = tmp1;
	ecc_code[1] = tmp2;
}

/**
 * nand_calculate_ecc - [NAND Interface] Calculate 3 byte ECC code for 256 byte block
 * @dat:	raw data
 * @ecc_code:	buffer for ECC
 */
int NAND_calculateECC(const u8 *dat, u8 *ecc_code)
{
	u8 idx, reg1, reg2, reg3;
	int j;

	/* Initialize variables */
	reg1 = reg2 = reg3 = 0;
	ecc_code[0] = ecc_code[1] = ecc_code[2] = 0;

	/* Build up column parity */
	for(j = 0; j < 256; j++) {

		/* Get CP0 - CP5 from table */
		idx = nand_ecc_precalc_table[dat[j]];
		reg1 ^= (idx & 0x3f);

		/* All bit XOR = 1 ? */
		if (idx & 0x40) {
			reg3 ^= (u8) j;
			reg2 ^= ~((u8) j);
		}
	}

	/* Create non-inverted ECC code from line parity */
	NAND_transResult(reg2, reg3, ecc_code);

	/* Calculate final ECC code */
	ecc_code[0] = ~ecc_code[0];
	ecc_code[1] = ~ecc_code[1];
	ecc_code[2] = ((~reg1) << 2) | 0x03;
	return 0;
}

/**
 * nand_correct_data - [NAND Interface] Detect and correct bit error(s)
 * @dat:	raw data read from the chip
 * @read_ecc:	ECC from the chip
 * @calc_ecc:	the ECC calculated from raw data
 *
 * Detect and correct a 1 bit error for 256 byte block
 */
int NAND_correctData(u8 *dat, u8 *read_ecc, u8 *calc_ecc)
{
	u8 a, b, c, d1, d2, d3, add, bit, i;

	/* Do error detection */
	d1 = calc_ecc[0] ^ read_ecc[0];
	d2 = calc_ecc[1] ^ read_ecc[1];
	d3 = calc_ecc[2] ^ read_ecc[2];

	if ((d1 | d2 | d3) == 0) {
		/* No errors */
		return 0;
	}
	else {
		a = (d1 ^ (d1 >> 1)) & 0x55;
		b = (d2 ^ (d2 >> 1)) & 0x55;
		c = (d3 ^ (d3 >> 1)) & 0x54;

		/* Found and will correct single bit error in the data */
		if ((a == 0x55) && (b == 0x55) && (c == 0x54)) {
			c = 0x80;
			add = 0;
			a = 0x80;
			for (i=0; i<4; i++) {
				if (d1 & c)
					add |= a;
				c >>= 2;
				a >>= 1;
			}
			c = 0x80;
			for (i=0; i<4; i++) {
				if (d2 & c)
					add |= a;
				c >>= 2;
				a >>= 1;
			}
			bit = 0;
			b = 0x04;
			c = 0x80;
			for (i=0; i<3; i++) {
				if (d3 & c)
					bit |= b;
				c >>= 2;
				b >>= 1;
			}
			b = 0x01;
			a = dat[add];
			a ^= (b << bit);
			dat[add] = a;
			return 1;
		}
		else {
			i = 0;
			while (d1) {
				if (d1 & 0x01)
					++i;
				d1 >>= 1;
			}
			while (d2) {
				if (d2 & 0x01)
					++i;
				d2 >>= 1;
			}
			while (d3) {
				if (d3 & 0x01)
					++i;
				d3 >>= 1;
			}
			if (i == 1) {
				/* ECC Code Error Correction */
				read_ecc[0] = calc_ecc[0];
				read_ecc[1] = calc_ecc[1];
				read_ecc[2] = calc_ecc[2];
				return 2;
			}
			else {
				/* Uncorrectable Error */
				return -1;
			}
		}
	}

	/* Should never happen */
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
// NAND flash helper functions go here, ported from Windows CE FMD

////////////////////////////////////////////////////////////////////////////////
//
// NAND_checkStatus()
//
// Retrieve the status of the Chip. This function accept a loop number, which
// is used to do the loop if chip is not ready.
//
// dwLoops:
//
// 0: no loop
// 0xffffffff: loop forever

u8 NAND_checkStatus(u32 dwLoops)
{
	int	bStop = (dwLoops != (u32) -1);
	u8	ucStatus;
	int i;

	// There is a 200ns delay (Twb) between the time that the !write-enable line
	// (!WE) is asserted and the time the ready (R/!B) line is de-asserted. Generate a
	// delay before querrying the status.
	for (i=0; i<10; i++)
		inb(g_baseAddr + IO_NAND_STS);

	while(TRUE)
	{
		ucStatus = inb(g_baseAddr + IO_NAND_STS);

		if(((ucStatus & CS_NAND_STS_FLASH_RDY) &&	// status ready
				!(ucStatus & CS_NAND_CTLR_BUSY)) ||		// controller not busy
				(bStop && !dwLoops))					// non-infinite loop and

			break;									// we already pay our due

		dwLoops --;
	}

	return ucStatus;
}

////////////////////////////////////////////////////////////////////////////////
// NAND command helpers

static __inline void NAND_enableHwECC(int enable)
{
	if(enable) outb(0x07, g_baseAddr + IO_NAND_ECC_CTL);
	else outb(0x02, g_baseAddr + IO_NAND_ECC_CTL);
}

static void NAND_readHwECC(u8 *pData)
{
	// read ECC data from status register
	pData[0] = inb(g_baseAddr + IO_NAND_ECC_MSB);
	pData[1] = inb(g_baseAddr + IO_NAND_ECC_LSB);
	pData[2] = inb(g_baseAddr + IO_NAND_ECC_COL);
}

static __inline void NAND_writeIO(u8 b)
{
	outb(b, g_baseAddr + IO_NAND_IO);
}

static __inline void NAND_writeCTL(u8 b)
{
	outb(b, g_baseAddr + IO_NAND_CTL);
}

static __inline u8 NAND_readDataByte()
{
	return inb(g_baseAddr + IO_NAND_DATA);
}

static void NAND_readData(u8 *pData, int nSize)
{
	int i, nDwords = nSize/4;	// number of double words
	int nBytes = nSize % 4;		// leftover stuff

	if(nSize > 528) return;		// oversized buffer?

	// read from port mapped registers,
	for(i=0; i<nDwords; i++)
		((u32*)pData)[i] = inl(g_baseAddr + IO_NAND_DATA);

	for(i=0; i<nBytes; i++)
		pData[i] = inb(g_baseAddr + IO_NAND_DATA);
}

static __inline void NAND_writeByte(u8 b)
{
	outb(b, g_baseAddr + IO_NAND_DATA);
}

////////////////////////////////////////////////////////////////////////////////
//
// NAND_getStatus()
//
// Retrieve the status of the Chip. This function accept a loop number, which
// is used to do the loop if chip is not ready.
//
// dwLoops:
//
// 0: no loop
// 0xffffffff: loop forever

u8 NAND_getStatus(u32 dwLoops)
{
	int	bStop = (dwLoops != (u32) -1);
	u8	ucStatus;

	NAND_checkStatus(dwLoops);		// wait until ready

	NAND_writeCTL(CS_NAND_CTL_CLE);	// latch command
	NAND_writeIO(CMD_STATUS);		// issue read status command
	NAND_writeCTL(0x00);			// enable chip

	while(1)
	{
		ucStatus = inb(g_baseAddr + IO_NAND_DATA);

		if((ucStatus & STATUS_READY) ||		// status ready
				(bStop && !dwLoops))			// non-infinite loop and

			break;							// we already pay our due

		dwLoops--;
	}

	return ucStatus;
}

////////////////////////////////////////////////////////////////////////////////
//
// NAND_readFlashID
//
// Retrieve the flash chip manufacturer and ID

u32 NAND_readFlashID()
{
	u32 dwID=0;

	NAND_writeCTL(0x00);			// enable chip

	NAND_checkStatus((u32) -1);	// check ready

	NAND_writeCTL(CS_NAND_CTL_CLE);	// latch command
	NAND_writeIO(CMD_READID);		// send command
	NAND_writeCTL(CS_NAND_CTL_ALE);	// latch address
	NAND_writeIO(0x00);				// send address
	NAND_writeCTL(0x00);			// enable chip

	NAND_checkStatus((u32) -1);

	dwID = inl(g_baseAddr + IO_NAND_DATA);

	NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip

	return dwID;
}

////////////////////////////////////////////////////////////////////////////////
//
// NAND_isBlockBad
//
// Check to see if the given block is bad. A block is bad if the 517th u8 on
// the first or second page is not 0xff.
//
// blockID: The block address. We need to convert this to page address
//

int NAND_isBlockBad(u32 blockID)
{
	u8 pa1, pa2, pa3, ca1, ca2, bData;

	// Get the first page of the block
	u32 dwPageID = blockID * g_flashInfo.pagesPerBlock;

	// for 512-byte page size, use the original addressing scheme
	if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_512)
	{
		// three page address bytes
		pa1 = (u8) (dwPageID & 0xff);
		pa2 = (u8) ((dwPageID >> 8) & 0xff);
		pa3 = (u8) ((dwPageID >> 16) & 0xff);
		// just one column address byte
		ca1 = VALIDADDR;
	}
	// for 2048-byte page size, we need to add some stuff
	else if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
	{
		// three page address bytes
		pa1 = (u8) (dwPageID & 0xff);
		pa2 = (u8) ((dwPageID >> 8) & 0xff);
		pa3 = (u8) ((dwPageID >> 16) & 0xff);
		// two column address bytes
		ca1 = 0x0000;
		ca2 = 0x0008;	// point to the 2048-th byte
	}
	// unsupported page size
	else return TRUE;

	// For our NAND flash, we don't have to issue two read command. We just need
	// to issue one read command and do contiquous read

	NAND_writeCTL(0x00);				// enable chip
	NAND_checkStatus((u32) -1);	// check ready

	// Check the first page.
	NAND_writeCTL(CS_NAND_CTL_CLE);		// latch command

	if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
		NAND_writeIO(CMD_READ);			// send read command
	else NAND_writeIO(CMD_READ2);		// send read command 2
	NAND_writeCTL(CS_NAND_CTL_ALE);		// latch address
	NAND_writeIO(ca1);					// send Column Address 1

	if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
		NAND_writeIO(ca2);				// send Column Address 2

	NAND_writeIO(pa1);					// send Page Address 1
	NAND_writeIO(pa2);					// send Page Address 2
	NAND_writeIO(pa3);					// send Page Address 3
	NAND_writeCTL(0x00);				// select chip

	if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
	{
		NAND_writeCTL(CS_NAND_CTL_CLE);	// latch command
		NAND_writeIO(CMD_READ_2K);		// send read command
		NAND_writeCTL(0x00);			// select chip
	}

	NAND_checkStatus((u32) -1);	// check ready

	bData = NAND_readDataByte();		// read out the bad block marker
	if(bData != 0xff)					// no bits may be zeroed
	{
		debug("bad block found at address 0x%x\n", blockID);
		NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip
		return TRUE;
	}

	NAND_writeCTL(CS_NAND_CTL_CE);		// disable chip
	return FALSE;
}

////////////////////////////////////////////////////////////////////////////////
//

__inline int IsECCWritten(u8 *pECC)
{
	// FIXME: check only 6 first bytes
	static u8 abNoECC[] = {0xff,0xff,0xff,0xff,0xff,0xff};

	return (memcmp(pECC, abNoECC, sizeof(abNoECC)) != 0);
}

////////////////////////////////////////////////////////////////////////////////
//

void NAND_close(void)
{
	if (g_chipID >= 0)
		wrmsr(MSR_DIVIL_LBAR_FLSH0 + g_chipID, g_orig_flsh);
}

////////////////////////////////////////////////////////////////////////////////
//

int NAND_initChip(int chipNum)
{
	msr_t msr;

	if (g_chipID == chipNum) return 0;
	if (g_chipID != -1) NAND_close();

	memset(&g_flashInfo, 0, sizeof(g_flashInfo));

	g_chipID = -1;

	///////////////////////////////////////////////////////////////////////////////////
	// init the MSR_DIVIL_BALL_OPTS register, enable flash mode

	msr = rdmsr(MSR_DIVIL_BALL_OPTS);

	if (msr.lo & PIN_OPT_IDE) {
		printf("NAND controller not enabled!\n");
		return -1;
	}

	///////////////////////////////////////////////////////////////////////////////////
	// init the MSR_DIVIL_LBAR_FLSHx register, I/O mapped mode, set a hardcoded base address
	// Later we restore initial state

	g_orig_flsh = rdmsr(MSR_DIVIL_LBAR_FLSH0 + chipNum);
	if (!(g_orig_flsh.hi & NOR_NAND)) {
		printf("CS%d set up for NOR, aborting!\n", chipNum);
		return -1;
	}

	msr.hi = SET_FLSH_HIGH;
	msr.lo = SET_FLSH_LOW;
	wrmsr(MSR_DIVIL_LBAR_FLSH0 + chipNum, msr);
	g_baseAddr = SET_FLSH_LOW;	// set the IO base address

	// read the register back
	msr = rdmsr(MSR_DIVIL_LBAR_FLSH0 + chipNum);
	debug("MSR_DIVIL_LBAR_FLSH%d = 0x%08x 0x%08x\n", (int)chipNum, msr.hi, msr.lo);

	// read out flash chip ID
	g_deviceID = NAND_readFlashID();

	switch(g_deviceID)	// allow only known flash chips
	{
	case SAMSUNG_NAND_64MB:
	case ST_NAND_64MB:

		g_flashInfo.numBlocks = 4096;
		g_flashInfo.pagesPerBlock = 32;
		g_flashInfo.dataBytesPerPage = 512;
		g_flashInfo.flashType = FLASH_NAND;

		break;

	case ST_NAND_128MB:
	case HY_NAND_128MB:

		g_flashInfo.numBlocks = 1024;
		g_flashInfo.pagesPerBlock = 64;
		g_flashInfo.dataBytesPerPage = 2048;
		g_flashInfo.flashType = FLASH_NAND;

	case ST_NAND_512MB:

		g_flashInfo.numBlocks = 4096;
		g_flashInfo.pagesPerBlock = 64;
		g_flashInfo.dataBytesPerPage = 2048;
		g_flashInfo.flashType = FLASH_NAND;

		break;

	default:
		printf("Unsupported flash chip ID: %x\n", g_deviceID);
		return -1;
	}

	g_flashInfo.bytesPerBlock = g_flashInfo.dataBytesPerPage * g_flashInfo.pagesPerBlock;

	if(!g_pBBT) g_pBBT = malloc(g_flashInfo.numBlocks);
	if(!g_pBBT)
	{
		printf("Could not allocate bad block table\n");
		return -1;
	}

	debug("bad block table allocated, size %d\n", g_flashInfo.numBlocks);
	memset(g_pBBT, BLOCK_UNKNOWN, g_flashInfo.numBlocks);

	g_chipID = chipNum;

	printf("Geode LX flash driver initialized, device ID 0x%x\n", g_deviceID);
	debug("FlashChip = 0x%x\n", g_chipID);
	debug("NumBlocks = 0x%x\n", g_flashInfo.numBlocks);
	debug("PagesPerBlock = 0x%x\n", g_flashInfo.pagesPerBlock);
	debug("BytesPerPage = 0x%x\n", g_flashInfo.dataBytesPerPage);
	debug("FlashType = %s\n", g_flashInfo.flashType == FLASH_NAND ? "NAND" : "NOR");
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// NAND_readPage
//
// Read the content of the sector.
//
// startSectorAddr: Starting page address
// pSectorBuff : Buffer for the data portion

int NAND_readPage(u32 pageAddr, u8 *pPageBuff)
{
	if (!pPageBuff)
	{
		debug("Invalid parameters!\n");
		return ERROR_BAD_PARAMS;
	}

	// sanity check
	if (pageAddr < (g_flashInfo.numBlocks * g_flashInfo.pagesPerBlock))
	{
		u8 bBadBlock = 0, bReserved = 0;

		u8 addr1 = (u8)(pageAddr & 0xff);
		u8 addr2 = (u8)((pageAddr >> 8) & 0xff);
		u8 addr3 = (u8)((pageAddr >> 16) & 0xff);

		u16 eccSize = 0;				// total ECC size
		u32 pageSize = 0;

		NAND_writeCTL(0x00);				// enable chip
		NAND_checkStatus((u32)-1);		// check ready

		NAND_writeCTL(CS_NAND_CTL_CLE);		// latch command
		NAND_writeIO(CMD_READ);				// send read command
		NAND_writeCTL(CS_NAND_CTL_ALE);		// latch address
		NAND_writeIO(0x00);					// send Column Address 1

		if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
			NAND_writeIO(0x00);				// send Column Address 2

		NAND_writeIO(addr1);				// send Page Address 1
		NAND_writeIO(addr2);				// send Page Address 2
		NAND_writeIO(addr3);				// send Page Address 3
		NAND_writeCTL(0x00);				// select chip

		if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
		{
			NAND_writeCTL(CS_NAND_CTL_CLE);	// latch command
			NAND_writeIO(CMD_READ_2K);		// send read command
			NAND_writeCTL(0x00);			// select chip
		}

		NAND_checkStatus((u32) -1);	// check ready

		while(pageSize < g_flashInfo.dataBytesPerPage)
		{
			// read out the first half of page data
			NAND_enableHwECC(1);			// enable HW ECC calculation
			NAND_readData(&pPageBuff[pageSize], READ_BLOCK_SIZE);
			NAND_readHwECC(&g_eccCalc[pageSize / READ_BLOCK_SIZE * 3]);
			// update counters too
			pageSize += READ_BLOCK_SIZE;
			eccSize += 3;
		}

		debug("read %d bytes from page address %x\n", pageSize, pageAddr);
		NAND_enableHwECC(0);				// disable HW ECC

		// Now read the spare area data

		if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_512)
		{
			// Read the ECC info according to Linux MTD format, first part
			NAND_readData(g_eccTest, 4);

			bBadBlock = NAND_readDataByte();	// bad block byte
			bReserved = NAND_readDataByte();	// reserved byte
			// Read the ECC info according to Linux MTD format, second part
			NAND_readData(&g_eccTest[4], 2);

			// calculate the first part of ECC, use software method
			//NAND_calculateECC(&pPageBuff[0], &g_eccCalc[0]);
			// calculate the second part of ECC, use software method
			//NAND_calculateECC(&pPageBuff[256], &g_eccCalc[3]);
		}
		else if(g_flashInfo.dataBytesPerPage == PAGE_SIZE_2048)
		{
			int i;

			for(i=0; i<40; i++) NAND_readDataByte();	// skip stuff
			// Read the ECC info according to Linux MTD format (2048 byte page)
			NAND_readData(g_eccTest, eccSize);
		}

		// test the data integrity; if the data is invalid, attempt to fix it using ECC
		if(memcmp(g_eccCalc, g_eccTest, eccSize))
		{
			int nRet = 0;

			// If the ECC is all 0xff, then it probably hasn't been written out yet
			// because the data hasn't been written, so ignore the invalid ECC.
			if(!IsECCWritten(g_eccTest))
			{
				debug("No ECC detected at page 0x%x\n", pageAddr);
				NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip
				return ERROR_NO_ECC;
			}

			debug("Page data (page 0x%x) is invalid. Attempting ECC to fix it.\n", pageAddr);
			nRet = NAND_correctData(&pPageBuff[0], &g_eccTest[0], &g_eccCalc[0]);
			if(nRet == -1)
			{
				debug("ERROR - page data (page 0x%x, first part) Unable to correct invalid data!\n", pageAddr);
				NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip
				return ERROR_ECC_ERROR1;
			}
			else if(nRet == 0) debug("No errors detected (page 0x%x, first part)\n", pageAddr);
			else debug("Invalid data (page 0x%x, first part) was corrected using ECC!\n", pageAddr);

			// now do the second part
			nRet = NAND_correctData(&pPageBuff[256], &g_eccTest[3], &g_eccCalc[3]);
			if(nRet == -1)
			{
				debug("ERROR - page data (page 0x%x, second part) Unable to correct invalid data!\n", pageAddr);
				NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip
				return ERROR_ECC_ERROR2;
			}
			else if(nRet == 0) debug("No errors detected (page 0x%x, second part)\n", pageAddr);
			else debug("Invalid data (page 0x%x, second part) was corrected using ECC!\n", pageAddr);
		}
	}
	else
	{
		debug("Page address [%d] is too large\n", pageAddr);
		return ERROR_BAD_ADDRESS;
	}

	NAND_writeCTL(CS_NAND_CTL_CE);	// disable chip
	return ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// FILO interface functions

int flash_probe(int drive)
{
	debug("drive %d\n", drive);
	return NAND_initChip(drive);
}

////////////////////////////////////////////////////////////////////////////////

int flash_read(int drive, sector_t sector, void *buffer)
{
	int block, nRet;
	u32 pageAddress = sector * DEV_SECTOR_SIZE / g_flashInfo.dataBytesPerPage;
	u32 pageOffset = sector * DEV_SECTOR_SIZE % g_flashInfo.dataBytesPerPage;

	// sanity check
	if(!g_pBBT || !g_flashInfo.pagesPerBlock)
	{
		debug("error: NAND not initialized\n");
		return -1;
	}

	// check that the page ID is valid
	if(pageAddress >= (g_flashInfo.numBlocks * g_flashInfo.pagesPerBlock))
	{
		debug("error: sector offset %x out of range\n", sector);
		return -2;
	}

	// get block address
	block = pageAddress / g_flashInfo.pagesPerBlock;

	debug("drive %d, sector %d -> page %d + %d, buffer 0x%08x\n",
		drive, (unsigned int)sector, pageAddress, pageOffset, (unsigned int)buffer);

	// get the block status first
	if(g_pBBT[block] == BLOCK_UNKNOWN)
	{
		debug("checking block 0x%x status for BBT\n", block);
		g_pBBT[block] = NAND_isBlockBad(block) ? BLOCK_BAD : BLOCK_GOOD;
	}

	// return failure immediately if the block is bad
	if(g_pBBT[block] == BLOCK_BAD)
	{
		debug("error: block %x is bad\n", block);
		return -3;
	}

	// check if we have just read that page
	if(g_currentPage == pageAddress)
	{
		// should use cache instead
		memcpy(buffer, g_pageBuf + pageOffset, DEV_SECTOR_SIZE);
		return ERROR_SUCCESS;
	}

	// otherwise proceed with normal reading
	nRet = NAND_readPage(pageAddress, g_pageBuf);
	memcpy(buffer, g_pageBuf + pageOffset, DEV_SECTOR_SIZE);

	return nRet;
}
