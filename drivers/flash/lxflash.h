#ifndef LXFLASH_H
#define LXFLASH_H

#define TRUE	1	// hmm that's quite obvious :)
#define FALSE	0

typedef struct msr_struct
{
	unsigned lo;
	unsigned hi;
} msr_t;

static inline msr_t rdmsr(unsigned index)
{
	msr_t result;
	__asm__ __volatile__ (
		"rdmsr"
		: "=a" (result.lo), "=d" (result.hi)
		: "c" (index)
		);
	return result;
}

static inline void wrmsr(unsigned index, msr_t msr)
{
	__asm__ __volatile__ (
		"wrmsr"
		: /* No outputs */
		: "c" (index), "a" (msr.lo), "d" (msr.hi)
		);
}

typedef struct _FLASH_INFO
{
	unsigned long	numBlocks;
	unsigned long	bytesPerBlock;
	unsigned short	pagesPerBlock;
	unsigned short	dataBytesPerPage;
	unsigned short	flashType;
} FLASH_INFO;

// NAND flash controller MSR registers

#define MSR_DIVIL_LBAR_FLSH0	0x51400010	// Flash Chip Select 0
#define MSR_DIVIL_LBAR_FLSH1	0x51400011	// Flash Chip Select 1
#define MSR_DIVIL_LBAR_FLSH2	0x51400012	// Flash Chip Select 2
#define MSR_DIVIL_LBAR_FLSH3	0x51400013	// Flash Chip Select 3
#define NOR_NAND				(1UL<<1)	// 1 for NAND, 0 for NOR

#define MSR_DIVIL_BALL_OPTS		0x51400015
#define PIN_OPT_IDE				(1UL<<0)	// 0 for flash, 1 for IDE

#define MSR_NANDF_DATA			0x5140001B
#define MSR_NANDF_CTL			0x5140001C

// Intended value for LBAR_FLSHx: enabled, PIO, NAND, @0xC000

#define SET_FLSH_HIGH			0x0000FFF3
#define SET_FLSH_LOW			0x0000C000

// ThinCan defaults

#define PAGE_SIZE_512			512
#define PAGE_SIZE_2048			2048
#define MAX_PAGE_SIZE			2048
#define MAX_ECC_SIZE			24
#define READ_BLOCK_SIZE			256

//  VALIDADDR is 5 << 8
//
//  Explain:    5 means the 6th byte in spare area (517 byte in the page)
//              Shift 8 bit to the left to form the correct address for 16bit port
//
#define VALIDADDR				0x05
#define OEMADDR					0x04		// 5th byte in spare area

//  NAND Flash Command. This appears to be generic across all NAND flash chips

#define CMD_READ                0x00        //  Read
#define CMD_READ1               0x01        //  Read1
#define CMD_READ2               0x50        //  Read2
#define CMD_READID              0x90        //  ReadID
#define CMD_READID2             0x91        //  Read extended ID
#define CMD_WRITE               0x80        //  Write phase 1
#define CMD_WRITE2              0x10        //  Write phase 2
#define CMD_ERASE               0x60        //  Erase phase 1
#define CMD_ERASE2              0xd0        //  Erase phase 2
#define CMD_STATUS              0x70        //  Status read
#define CMD_RESET               0xff        //  Reset
#define CMD_READ_2K             0x30        //  Second cycle read cmd for 2KB flash

// Registers within the NAND flash controller BAR -- memory mapped

#define MM_NAND_DATA			0x00		// 0 to 0x7ff, in fact
#define MM_NAND_CTL				0x800		// Any even address 0x800-0x80e
#define MM_NAND_IO				0x801		// Any odd address 0x801-0x80f
#define MM_NAND_STS				0x810
#define MM_NAND_ECC_LSB			0x811
#define MM_NAND_ECC_MSB			0x812
#define MM_NAND_ECC_COL			0x813
#define MM_NAND_LAC				0x814
#define MM_NAND_ECC_CTL			0x815

// Registers within the NAND flash controller BAR -- I/O mapped

#define IO_NAND_DATA			0x00		// 0 to 3, in fact
#define IO_NAND_CTL				0x04
#define IO_NAND_IO				0x05
#define IO_NAND_STS				0x06
#define IO_NAND_ECC_CTL			0x08
#define IO_NAND_ECC_LSB			0x09
#define IO_NAND_ECC_MSB			0x0a
#define IO_NAND_ECC_COL			0x0b
#define IO_NAND_LAC				0x0c

#define CS_NAND_CTL_DIST_EN			(1<<4)	// Enable NAND Distract interrupt
#define CS_NAND_CTL_RDY_INT_MASK	(1<<3)	// Enable RDY/BUSY# interrupt
#define CS_NAND_CTL_ALE				(1<<2)
#define CS_NAND_CTL_CLE				(1<<1)
#define CS_NAND_CTL_CE				(1<<0)	// Keep low; 1 to reset

#define CS_NAND_STS_FLASH_RDY		(1<<3)
#define CS_NAND_CTLR_BUSY			(1<<2)
#define CS_NAND_CMD_COMP			(1<<1)
#define CS_NAND_DIST_ST				(1<<0)

#define CS_NAND_ECC_PARITY			(1<<2)
#define CS_NAND_ECC_CLRECC			(1<<1)
#define CS_NAND_ECC_ENECC			(1<<0)

//  Status bit pattern, read from chip register

#define STATUS_READY				0x40	//  Ready
#define STATUS_ERROR				0x01	//  Error

#define FLASH_NOR					0
#define FLASH_NAND					1

#define SAMSUNG_NAND_64MB			0xc0a576ec
#define ST_NAND_64MB				0x76207620
#define ST_NAND_512MB				0x9580dc20
#define ST_NAND_128MB				0x1d80f120
#define HY_NAND_128MB				0x1d80f1ad

#define ERROR_SUCCESS				0
#define ERROR_BAD_PARAMS			-1
#define ERROR_ECC_ERROR1			1
#define ERROR_ECC_ERROR2			2
#define ERROR_BAD_ADDRESS			3
#define ERROR_NO_ECC				4

#define BLOCK_UNKNOWN				0
#define BLOCK_BAD					2
#define BLOCK_GOOD					1

#endif
