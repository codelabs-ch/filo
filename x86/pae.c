/*
 * This file is part of the coreboot project.
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
#include <debug.h>
#include <pae.h>

#define PDPTE_PRES	(1ULL << 0)

#define PDE_PRES	(1ULL << 0)
#define PDE_RW		(1ULL << 1)
#define PDE_PS		(1ULL << 7)

#define PDE_IDX_SHIFT 21

#define CR0_PG	(1 << 31)
#define CR4_PAE	(1 <<  5)

#define CRx_TYPE uint32_t

static const size_t s2MiB = 2 * MiB;

struct pde {
	uint32_t addr_lo;
	uint32_t addr_hi;
} __attribute__ ((packed));

struct pg_table {
	struct pde pd[2048];
	struct pde pdp[4];
} __attribute__ ((packed, aligned(4096)));


/* Page table instance. */
static struct pg_table pgtbl;
static unsigned int pgtbl_initialized = 0;

static inline CRx_TYPE read_cr0(void) __attribute__((always_inline));
static inline CRx_TYPE read_cr4(void) __attribute__((always_inline));
static inline void write_cr0(CRx_TYPE data) __attribute__((always_inline));
static inline void write_cr3(CRx_TYPE data) __attribute__((always_inline));
static inline void write_cr4(CRx_TYPE data) __attribute__((always_inline));

static inline CRx_TYPE read_cr0(void)
{
	CRx_TYPE value;
	asm volatile (
		"mov %%cr0, %0"
		: "=r"(value)
		:
		: "memory"
	);
	return value;
}

static inline CRx_TYPE read_cr4(void)
{
	CRx_TYPE value;
	asm volatile (
		"mov %%cr4, %0"
		: "=r"(value)
		:
		: "memory"
	);
	return value;
}

static inline void write_cr0(CRx_TYPE data)
{
	asm volatile (
		"mov %0, %%cr0"
		:
		: "r"(data)
		: "memory"
	);
}

static inline void write_cr3(CRx_TYPE data)
{
	asm volatile (
		"mov %0, %%cr3"
		:
		: "r"(data)
		: "memory"
	);
}

static inline void write_cr4(CRx_TYPE data)
{
	asm volatile (
		"mov %0, %%cr4"
		:
		: "r"(data)
		: "memory"
	);
}

static void paging_enable_pae(void)
{
	CRx_TYPE cr0;
	CRx_TYPE cr4;

	/* Enable PAE */
	cr4 = read_cr4();

	cr4 |= CR4_PAE;
	write_cr4(cr4);

	/* Enable Paging */
	cr0 = read_cr0();
	cr0 |= CR0_PG;
	write_cr0(cr0);
}

static void paging_enable_pae_cr3(uintptr_t cr3)
{
	/* Load the page table address */
	write_cr3(cr3);
	paging_enable_pae();
}

static void paging_disable_pae(void)
{
	CRx_TYPE cr0;
	CRx_TYPE cr4;

	/* Disable Paging */
	cr0 = read_cr0();
	cr0 &= ~(CRx_TYPE)CR0_PG;
	write_cr0(cr0);

	/* Disable PAE */
	cr4 = read_cr4();
	cr4 &= ~(CRx_TYPE)CR4_PAE;
	write_cr4(cr4);
}

/*
 * Initialize page table instance pgtbl with an identity map of the 32-bit
 * address space.
 */
static void identity_paging_enable(void)
{
	struct pde *pd = (struct pde *)&pgtbl.pd, *pdp = (struct pde *)&pgtbl.pdp;
	/* Point the page directory pointers at the page directories. */
	memset(&pgtbl.pdp, 0, sizeof(pgtbl.pdp));

	pdp[0].addr_lo = (virt_to_phys((uintptr_t)&pd[512*0])) | PDPTE_PRES;
	pdp[1].addr_lo = (virt_to_phys((uintptr_t)&pd[512*1])) | PDPTE_PRES;
	pdp[2].addr_lo = (virt_to_phys((uintptr_t)&pd[512*2])) | PDPTE_PRES;
	pdp[3].addr_lo = (virt_to_phys((uintptr_t)&pd[512*3])) | PDPTE_PRES;

	for (size_t i = 0; i < 2048; i++) {
		pd[i].addr_lo = (i << PDE_IDX_SHIFT) | PDE_PS | PDE_PRES | PDE_RW;
		pd[i].addr_hi = 0;
	}
}

/*
 * Return address outside of filo _start .. _end relocated range, i.e. a linear
 * address which is not used by FILO CS/DS. Basically any address is fine for
 * the vmem window use case, as long it does not overlap with relocated FILO
 * and is 2 MiB aligned. Use FILO's _start address before relocation.
 * */
static uintptr_t get_vmem_addr(void)
{
	extern char _start[];
	const uintptr_t addr = (uintptr_t)&_start;

	return ALIGN_UP(addr, s2MiB);
}

/*
 * Add mapping for given phys address to specified page directory entry pd.
 * Then invalidate given virtual address virt.
 */
static void map_page(struct pde *const pd, uint64_t phys, void *const virt)
{
	pd->addr_lo = phys | PDE_PS | PDE_RW | PDE_PRES;
	pd->addr_hi = phys >> 32;
	asm volatile ("invlpg (%0)" :: "b"(virt) : "memory");
}

void memset_pae(uint64_t dest, unsigned char pat, uint64_t length)
{
	const uintptr_t vmem_addr = get_vmem_addr();
	struct pde *const pd = &pgtbl.pd[vmem_addr >> PDE_IDX_SHIFT];

	ssize_t offset;

	if (!pgtbl_initialized) {
		identity_paging_enable();
		pgtbl_initialized = 1;
	}

	paging_enable_pae_cr3(virt_to_phys((uintptr_t)&pgtbl.pdp));

	offset = dest - ALIGN_DOWN(dest, s2MiB);
	dest = ALIGN_DOWN(dest, s2MiB);

	do {
		const size_t len = MIN(length, s2MiB - offset);

		map_page(pd, dest, phys_to_virt(vmem_addr));
		debug("%s: Mapped 0x%llx[0x%lx] - 0x%zx\n", __func__,
				dest + offset, vmem_addr + offset, len);

		/* phys_to_virt: Add FILO relocation offset (segmentation) */
		memset(phys_to_virt(vmem_addr + offset), pat, len);

		dest += s2MiB;
		length -= len;
		offset = 0;
	} while (length > 0);

	paging_disable_pae();
}

int64_t read_pae(uint64_t dest, uint64_t length,
		int (*read_func)(void *buf, unsigned long len))
{
	const uintptr_t vmem_addr = get_vmem_addr();
	struct pde *const pd = &pgtbl.pd[vmem_addr >> PDE_IDX_SHIFT];

	int64_t ret = length;
	ssize_t offset;

	if (length > INT64_MAX)
		return -1;

	if (read_func == NULL)
		return -1;

	if (!pgtbl_initialized) {
		identity_paging_enable();
		pgtbl_initialized = 1;
	}

	paging_enable_pae_cr3(virt_to_phys((uintptr_t)&pgtbl.pdp));

	offset = dest - ALIGN_DOWN(dest, s2MiB);
	dest = ALIGN_DOWN(dest, s2MiB);

	do {
		const size_t len = MIN(length, s2MiB - offset);

		map_page(pd, dest, phys_to_virt(vmem_addr));
		debug("%s: Mapped 0x%llx[0x%lx] - 0x%zx\n", __func__,
				dest + offset, vmem_addr + offset, len);

		/* phys_to_virt: Add FILO relocation offset (segmentation) */
		const int err = read_func(phys_to_virt(vmem_addr + offset), len);
		if (err < 0) {
			ret = err;
			goto _disable_ret;
		} else if (err != (int)len) {
			ret -= length - (size_t)err;
			goto _disable_ret;
		}

		dest += s2MiB;
		length -= len;
		offset = 0;
	} while (length > 0);

_disable_ret:
	paging_disable_pae();

	return ret;
}
