/*******************************************************************************
 *
 *	WindowsCE/i386 loader 
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
#include <lib.h>
#include <fs.h>
#include <arch/io.h>
#include "context.h"
#include "segment.h"

#define DEBUG_THIS DEBUG_WINCELOAD
#include <debug.h>

#define BOOTARG_PTR_LOCATION	0x001FFFFC
#define BOOTARG_LOCATION		0x001FFF00
#define BOOTARG_SIGNATURE		0x544F4F42
#define BOOTARG_VERSION_SIG		0x12345678
#define BOOTARG_MAJOR_VER		1
#define BOOTARG_MINOR_VER		0

#define MAX_DEV_NAMELEN			16	// Should match EDBG_MAX_DEV_NAMELEN.

#define LDRFL_USE_EDBG			0x0001	// Set to attempt to use debug Ethernet
// The following two flags are only looked at if LDRFL_USE_EDBG is set
#define LDRFL_ADDR_VALID		0x0002	// Set if EdbgAddr field is valid
#define LDRFL_JUMPIMG			0x0004	// If set, don't communicate with eshell to get
// The following flag is only used for backup FLASH operation
#define LDRFL_FLASH_BACKUP		0x80
// configuration, use ucEshellFlags field.
// Use this constant in EdbgIRQ to specify that EDBG should run without an interrupt.
#define EDBG_IRQ_NONE			0xFF

#define EDBG_ADAPTER_DEFAULT	2
#define EDBG_ADAPTER_RTL8139	4

#define PSIZE					(1500)	// Max Packet Size
#define DSIZE					(PSIZE+12)
#define BIN_HDRSIG_SIZE			7

#define ROM_SIGNATURE_OFFSET	0x40	// Offset from the image's physfirst address to the ROM signature.
#define ROM_SIGNATURE			0x43454345
#define ROM_TOC_POINTER_OFFSET	0x44	// Offset from the image's physfirst address to the TOC pointer.
#define ROM_TOC_OFFSET_OFFSET	0x48	// Offset from the image's physfirst address to the TOC offset (from physfirst).

#define GDT_LOC					0x92000
#define STACK_LOC				0x93000

typedef struct _EDBG_ADDR {
	u32 dwIP;
	u16 wMAC[3];
	u16 wPort;
} EDBG_ADDR;

typedef struct _BOOT_ARGS {
	u8 ucVideoMode;
	u8 ucComPort;
	u8 ucBaudDivisor;
	u8 ucPCIConfigType;
	u32 dwSig;
	u32 dwLen;
	u8 ucLoaderFlags;
	u8 ucEshellFlags;
	u8 ucEdbgAdapterType;
	u8 ucEdbgIRQ;
	u32 dwEdbgBaseAddr;
	u32 dwEdbgDebugZone;
	EDBG_ADDR EdbgAddr;
	EDBG_ADDR EshellHostAddr;
	EDBG_ADDR DbgHostAddr;
	EDBG_ADDR CeshHostAddr;
	EDBG_ADDR KdbgHostAddr;
	u32 DHCPLeaseTime;
	u16 EdbgFlags;
	u16 KitlTransport;
	u32 dwEBootFlag;
	u32 dwEBootAddr;
	u32 dwLaunchAddr;
	u32 pvFlatFrameBuffer;
	u16 vesaMode;
	u16 cxDisplayScreen;
	u16 cyDisplayScreen;
	u16 cxPhysicalScreen;
	u16 cyPhysicalScreen;
	u16 cbScanLineLength;
	u16 bppScreen;
	u8 RedMaskSize;
	u8 RedMaskPosition;
	u8 GreenMaskSize;
	u8 GreenMaskPosition;
	u8 BlueMaskSize;
	u8 BlueMaskPosition;
	u32 dwVersionSig;
	u16 MajorVersion;
	u16 MinorVersion;
	u8 szDeviceNameRoot[MAX_DEV_NAMELEN];
	u32 dwImgStoreAddr;
	u32 dwImgLoadAddr;
	u32 dwImgLength;
	u8 NANDBootFlags;
	u8 NANDBusNumber;
	u32 NANDSlotNumber;
} BOOT_ARGS;

typedef struct _ROMHDR {
	u32 dllfirst;
	u32 dlllast;
	u32 physfirst;
	u32 physlast;
	u32 nummods;
	u32 ulRAMStart;
	u32 ulRAMFree;
	u32 ulRAMEnd;
	u32 ulCopyEntries;
	u32 ulCopyOffset;
	u32 ulProfileLen;
	u32 ulProfileOffset;
	u32 numfiles;
	u32 ulKernelFlags;
	u32 ulFSRamPercent;
	u32 ulDrivglobStart;
	u32 ulDrivglobLen;
	u16 usCPUType;
	u16 usMiscFlags;
	void *pExtensions;
	u32 ulTrackingStart;
	u32 ulTrackingLen;
} ROMHDR;

typedef struct _SEGMENT_INFO {
	u32 segAddr;
	u32 segSize;
	u32 checkSum;
} SEGMENT_INFO;

typedef void (*PFN_LAUNCH) ();	// WinCE launch function proto

static u8 g_ceSignature[] = { 'B', '0', '0', '0', 'F', 'F', '\n' };
static void **g_ppBootArgs = NULL;
BOOT_ARGS *g_pBootArgs = NULL;
static ROMHDR *pROMHeader = NULL;

static u32 g_imageStart = 0;
static u32 g_imageSize = 0;
static u32 g_romOffset = 0;

static int verifyCheckSum(u8 * pData, int nSize, u32 checkSum)
{
	// check the CRC
	u32 crc = 0;
	int i;

	for (i = 0; i < nSize; i++)
		crc += *pData++;

	return (crc == checkSum);
}

int wince_launch(u32 imageStart, u32 imageSize,
		 u32 entryPoint, ROMHDR * pRomHdr)
{
	struct segment_desc *wince_gdt;
	struct context *ctx;

	debug("start Windows CE from address 0x%x, image loaded 0x%x,%d\n",
	      entryPoint, imageStart, imageSize);

	// initialize new stack
	ctx = init_context(phys_to_virt(STACK_LOC), 4096, 0);

	// initialize GDT in low memory
	wince_gdt = phys_to_virt(GDT_LOC);
	memset(wince_gdt, 0, 13 * sizeof(struct segment_desc));
	// flat kernel code/data segments
	wince_gdt[2] = gdt[FLAT_CODE];
	wince_gdt[3] = gdt[FLAT_DATA];

	wince_gdt[12] = gdt[FLAT_CODE];
	wince_gdt[13] = gdt[FLAT_DATA];
	ctx->gdt_base = GDT_LOC;
	ctx->gdt_limit = 14 * 8 - 1;
	ctx->cs = 0x10;
	ctx->ds = 0x18;
	ctx->es = 0x18;
	ctx->fs = 0x18;
	ctx->gs = 0x18;
	ctx->ss = 0x18;

	// kernel entry point
	ctx->eip = entryPoint;

	printf("Launching Windows CE...\n");

	// go...!
	ctx = switch_to(ctx);

	// may never return here
	printf("returned with eax=%#x\n", ctx->eax);
	return ctx->eax;
}

void wince_init_bootarg(u32 entryPoint)
{
	// init the BOOT_ARGS pointer at the known address
	g_ppBootArgs = phys_to_virt(BOOTARG_PTR_LOCATION);
	*g_ppBootArgs = (void *) BOOTARG_LOCATION;

	// keep our BOOT_ARGS somewhere in a dry dark place 
	g_pBootArgs = phys_to_virt(BOOTARG_LOCATION);

	debug("BOOT_ARGS at addr 0x%x, pointer at 0x%x [%x]\n",
	      (unsigned int) *g_ppBootArgs, BOOTARG_PTR_LOCATION,
	      (unsigned int) g_ppBootArgs);

	memset(g_pBootArgs, 0, sizeof(BOOT_ARGS));

	// this data was copied from WinCE EDBG boot args       
	g_pBootArgs->ucEdbgAdapterType = EDBG_ADAPTER_DEFAULT;
	// use the first PCI NIC available
	g_pBootArgs->ucEdbgIRQ = 0;
	g_pBootArgs->dwEdbgBaseAddr = 0;

	// set the KITL device name to something adequate
	strcpy((char *) g_pBootArgs->szDeviceNameRoot, "FILO");

	g_pBootArgs->dwSig = BOOTARG_SIGNATURE;
	g_pBootArgs->dwLen = sizeof(BOOT_ARGS);
	g_pBootArgs->dwVersionSig = BOOTARG_VERSION_SIG;
	g_pBootArgs->MajorVersion = BOOTARG_MAJOR_VER;
	g_pBootArgs->MinorVersion = BOOTARG_MINOR_VER;

/*
	g_pBootArgs->ucVideoMode = 255;
	g_pBootArgs->ucComPort = 1;
	g_pBootArgs->ucBaudDivisor = 3;
	g_pBootArgs->ucPCIConfigType = 1;
	g_pBootArgs->ucLoaderFlags = 0x7;
*/

	debug("Boot arguments initialized at 0x%x\n",
	      (unsigned int) *g_ppBootArgs);
}

int wince_load(const char *file, const char *cmdline)
{
	u8 signBuf[BIN_HDRSIG_SIZE], *pDest = NULL;
	SEGMENT_INFO segInfo;
	u32 totalBytes = 0;

	if (!file_open(file)) {
		printf("Failed opening image file: %s\n", file);
		return LOADER_NOT_SUPPORT;
	}
	// read the image signature
	file_read((void *) signBuf, BIN_HDRSIG_SIZE);

	if (memcmp(signBuf, g_ceSignature, BIN_HDRSIG_SIZE)) {
		printf("Bad or unknown Windows CE image signature\n");
		file_close();
		return LOADER_NOT_SUPPORT;
	}
	// now read image start address and size        
	file_read((void *) &g_imageStart, sizeof(u32));
	file_read((void *) &g_imageSize, sizeof(u32));

	if (!g_imageStart || !g_imageSize)	// sanity check
	{
		printf("Invalid image descriptors\n");
		file_close();
		return LOADER_NOT_SUPPORT;
	}

	printf("Windows CE BIN image, start 0x%x, length %d\n",
	       g_imageStart, g_imageSize);

	// main image reading loop      
	while (1) {
		// first grab the segment descriptor
		if (file_read(&segInfo, sizeof(SEGMENT_INFO)) <
		    sizeof(SEGMENT_INFO)) {
			printf ("\nFailed reading image segment descriptor\n");
			file_close();
			return LOADER_NOT_SUPPORT;
		}

		totalBytes += sizeof(SEGMENT_INFO);	// update data counter
		printf("#");	// that's a progress bar :)

		// now check if that's the last one
		if (segInfo.segAddr == 0 && segInfo.checkSum == 0)
			break;

		// map segment address to current address space
		pDest = (u8 *) phys_to_virt(segInfo.segAddr);
		debug("fetched segment address 0x%x [%x] size %d\n",
		      segInfo.segAddr, (unsigned int) pDest,
		      segInfo.segSize);

		// read the image segment data from VFS
		if (file_read((void *) pDest, segInfo.segSize) <
		    segInfo.segSize) {
			printf ("\nFailed reading image segment data (address 0x%x, size %d)\n",
			     segInfo.segAddr, segInfo.segSize);
			file_close();
			return LOADER_NOT_SUPPORT;
		}
		// check the data integrity
		if (!verifyCheckSum
		    (pDest, segInfo.segSize, segInfo.checkSum)) {
			printf ("\nFailed verifying segment checksum at address 0x%x, size %d\n",
			     (unsigned int) pDest, segInfo.segSize);
			file_close();
			return LOADER_NOT_SUPPORT;
		}
		// Look for ROMHDR to compute ROM offset.  NOTE: romimage guarantees that the record containing
		// the TOC signature and pointer will always come before the record that contains the ROMHDR contents.

		if (segInfo.segSize == sizeof(ROMHDR) && 
				(*(u32 *) phys_to_virt(g_imageStart + ROM_SIGNATURE_OFFSET) == ROM_SIGNATURE)) {
			u32 tempOffset =
			    (segInfo.segAddr -
			     *(u32 *) phys_to_virt(g_imageStart +
							ROM_SIGNATURE_OFFSET
							+ sizeof(long)));
			ROMHDR *pROMhdr = (ROMHDR *) pDest;

			// check to make sure this record really contains the ROMHDR.
			if ((pROMhdr->physfirst == (g_imageStart - tempOffset)) && 
					(pROMhdr->physlast == (g_imageStart - tempOffset + g_imageSize)) &&
			    		(u32) (((pROMhdr-> dllfirst << 16) & 0xffff0000) <= pROMhdr->dlllast) &&
			    		(u32) (((pROMhdr-> dllfirst << 16) & 0x0000ffff) <= pROMhdr->dlllast)) {
				g_romOffset = tempOffset;
				debug("\nROM offset = 0x%x\n", g_romOffset);
			}
		}

		totalBytes += segInfo.segSize;	// update data counter
	}

	// we should have moved all image segments to RAM by now
	printf("\nOS image loaded.\n");

	// check for pTOC signature ("CECE") here, after image in place
	if (*(u32 *) phys_to_virt(g_imageStart + ROM_SIGNATURE_OFFSET) == ROM_SIGNATURE) {
		// a pointer to the ROMHDR structure lives just past the ROM_SIGNATURE (which is a longword value).  Note that
		// this pointer is remapped since it might be a flash address (image destined for flash), but is actually cached
		// in RAM.

		u32 cacheAddress = *(u32 *) phys_to_virt(g_imageStart + ROM_SIGNATURE_OFFSET + sizeof(u32));

		pROMHeader =
		    (ROMHDR *) phys_to_virt(cacheAddress + g_romOffset);
		debug("ROMHDR at address 0x%xh\n",
		      cacheAddress + g_romOffset);
	}

	file_close();

	// prepare the boot arguments
	// note that the last segment size carries the launch address 
	wince_init_bootarg(segInfo.segSize);

	// finally, call the generic launch() function
	return wince_launch(g_imageStart, g_imageSize, segInfo.segSize,
			    pROMHeader);
}
