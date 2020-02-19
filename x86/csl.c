/*
 * This file is part of FILO.
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
 */

#include <arch/cpuid.h>
#include <debug.h>
#include <libpayload.h>
#include <grub/shared.h>
#include <fs.h>
#include <csl.h>

#include "context.h"
#include "segment.h"

/* protocol version magic */
static const u64 my_vermagic = 0x8adc5fa2448cb65eULL;

typedef unsigned long data_length_t;

enum
{
	CMD_WRITE = 0,
	CMD_FILL = 1,
	CMD_SET_ENTRY_POINT = 2,
	CMD_CHECK_CPUID = 3,
};

enum
{
	CPUID_RESULT_EAX = 0,
	CPUID_RESULT_EBX = 1,
	CPUID_RESULT_ECX = 2,
	CPUID_RESULT_EDX = 3,
};

#define ULONG_MAX 4294967295UL

#define CMD_SET_ENTRY_POINT_DATA_LEN	8
#define CMD_FILL_PATTERN_DATA_LEN		24
#define CMD_CHECK_CPUID_DATA_LEN		88

#define VENDOR_CMD_ID_START	60000
#define VENDOR_CMD_ID_END	65535

#define MAX_CHECK_STRING 64

#define E820_RAM 1

#define CSL_INITIAL_STATE { \
	.eax = 0, \
	.ebx = 0, \
	.ecx = 0, \
	.edx = 0, \
	.esp = 0, \
	.ebp = 0, \
	.esi = 0, \
	.edi = 0, \
	.eip = 0, \
}

#define CSL_CONTEXT_LOC	0x92000
#define CSL_GDT_LOC		0x93000

static const char *cmd_names[] = {
	"CMD_WRITE",
	"CMD_FILL",
	"CMD_SET_ENTRY_POINT",
	"CMD_CHECK_CPUID",
};

static u32 entry_point = ULONG_MAX;

struct csl_file_operations csl_fs_ops = {
	.open  = file_open,
	.read  = file_read,
	.size  = file_size,
	.close = file_close,
};

static inline int cpuid_supported(void)
{
	u32 supported;

	asm(
		"pushfl\n\t"
		"popl %%eax\n\t"
		"movl %%eax, %%edx\n\t"
		"xorl $0x200000, %%eax\n\t"
		"pushl %%eax\n\t"
		"popfl\n\t"
		"pushfl\n\t"
		"popl %%eax\n\t"
		"xorl %%edx, %%eax\n\t"
		: "=a" (supported) : : "%edx");

	return supported != 0;
}

static int find_ram_region_limit(const u64 where, u64 *const limit)
{
	const struct memrange *const memranges = lib_sysinfo.memrange;

	int i;
	for (i = 0; i < lib_sysinfo.n_memranges; ++i) {
		if (memranges[i].type == E820_RAM &&
				memranges[i].base <= where &&
				where < memranges[i].base + memranges[i].size) {
			*limit = memranges[i].base + memranges[i].size;
			return 0;
		}
	}
	return -1;
}

static int ram_region_check(const u64 address, const u64 size,
		const char *const cmd_name)
{
	u64 ram_region_limit;

	if (find_ram_region_limit(address, &ram_region_limit)) {
		grub_printf("%s - can't find ram region for address 0x%08llx\n",
				cmd_name, address);
		return -1;
	}
	if (address + size < address || address + size > ram_region_limit) {
		grub_printf("%s - data does not fit into memory ["
				"last address: 0x%08llx, limit: 0x%08llx]\n",
				cmd_name, address + size - 1, ram_region_limit - 1);
		return -1;
	}
	return 0;
}

static int cls_read_address(const char * const cmd_name, u64 *address)
{
	if (csl_fs_ops.read(address, 8) != 8) {
		grub_printf("%s - unable to read address\n", cmd_name);
		return -1;
	}
	if (*address > ULONG_MAX) {
		grub_printf("%s - address out of range 0x%llx\n",
				cmd_name, address);
		return -1;
	}

	return 0;
}

static int csl_cmd_write(const data_length_t data_length)
{
	int err;
	u64 address = 0;
	data_length_t content_len;

	if (data_length < 9) {
		grub_printf("%s - unexpected data length 0x%lx\n",
				cmd_names[CMD_WRITE], data_length);
		return -1;
	}

	err = cls_read_address(cmd_names[CMD_WRITE], &address);
	if (err < 0)
		return err;

	content_len = data_length - 8;
	if (ram_region_check(address, content_len, cmd_names[CMD_WRITE]))
		return -1;

	if (csl_fs_ops.read(phys_to_virt(address), content_len) !=
			(int) content_len) {
		grub_printf("%s - unable to read 0x%lx content bytes\n",
				cmd_names[CMD_WRITE], content_len);
		return -1;
	}

	return 0;
}

static int csl_cmd_fill(const data_length_t data_length)
{
	int err;
	u64 address = 0, fill_length = 0, pattern = 0;

	if (data_length != CMD_FILL_PATTERN_DATA_LEN) {
		grub_printf("%s - unexpected data length 0x%lx\n",
				cmd_names[CMD_FILL], data_length);
		return -1;
	}

	err = cls_read_address(cmd_names[CMD_FILL], &address);
	if (err < 0)
		return err;

	if (csl_fs_ops.read(&fill_length, 8) != 8) {
		grub_printf("%s - unable to read fill length\n",
				cmd_names[CMD_FILL]);
		return -1;
	}
	if (fill_length > ULONG_MAX) {
		grub_printf("%s - fill length is out of range - 0x%llx\n",
				cmd_names[CMD_FILL], fill_length);
		return -1;
	}

	if (ram_region_check(address, fill_length, cmd_names[CMD_FILL]))
		return -1;

	if (csl_fs_ops.read(&pattern, 8) != 8) {
		grub_printf("%s - unable to read pattern\n",
				cmd_names[CMD_FILL]);
		return -1;
	}

	memset(phys_to_virt(address), (int) pattern & 0xff, (size_t) fill_length);
	return 0;
}

static int csl_cmd_set_entry_point(const data_length_t data_length)
{
	u64 ep = 0;

	if (data_length != CMD_SET_ENTRY_POINT_DATA_LEN) {
		grub_printf("%s - unexpected data length 0x%lx\n",
				cmd_names[CMD_SET_ENTRY_POINT], data_length);
		return -1;
	}
	if (csl_fs_ops.read(&ep, 8) != CMD_SET_ENTRY_POINT_DATA_LEN) {
		grub_printf("%s - unable to read entry point\n",
				cmd_names[CMD_SET_ENTRY_POINT]);
		return -1;
	}
	if (ep > ULONG_MAX) {
		grub_printf("%s - entry point 0x%llx not reachable\n",
				cmd_names[CMD_SET_ENTRY_POINT], ep);
		return -1;
	}

	entry_point = (u32) ep;
	grub_printf("%s - setting entry point to 0x%x\n",
			cmd_names[CMD_SET_ENTRY_POINT], entry_point);
	return 0;
}

static int csl_cmd_check_cpuid(const data_length_t data_length)
{
	u64 word = 0;
	u32 leaf = 0, mask = 0, value = 0;
	u32 eax = 0, ebx = 0, ecx = 0, edx = 0, result;
	u8 result_register;
	char msg[MAX_CHECK_STRING];

	if (!cpuid_supported()) {
		grub_printf("%s - CPUID instruction not supported\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}

	if (data_length != CMD_CHECK_CPUID_DATA_LEN) {
		grub_printf("%s - unexpected data length 0x%lx\n",
				cmd_names[CMD_CHECK_CPUID], data_length);
		return -1;
	}
	/* ecx is not currently used as input to CPUID */
	if (csl_fs_ops.read(&ecx, 4) != 4) {
		grub_printf("%s - unable to read ecx field\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	if (csl_fs_ops.read(&leaf, 4) != 4) {
		grub_printf("%s - unable to read leaf field\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	if (csl_fs_ops.read(&value, 4) != 4) {
		grub_printf("%s - unable to read value field\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	if (csl_fs_ops.read(&mask, 4) != 4) {
		grub_printf("%s - unable to mask field\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	if (csl_fs_ops.read(&word, 8) != 8) {
		grub_printf("%s - unable to read result register\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	result_register = (u8) word & 0xff;

	if (csl_fs_ops.read(&msg, MAX_CHECK_STRING) != MAX_CHECK_STRING) {
		grub_printf("%s - unable to read message\n",
				cmd_names[CMD_CHECK_CPUID]);
		return -1;
	}
	/* enforce null-termination */
	msg[MAX_CHECK_STRING - 1] = '\0';

	grub_printf("%s - %s\n", cmd_names[CMD_CHECK_CPUID], msg);
	cpuid(leaf, eax, ebx, ecx, edx);

	switch (result_register)
	{
		case CPUID_RESULT_EAX:
			result = eax;
			break;
		case CPUID_RESULT_EBX:
			result = ebx;
			break;
		case CPUID_RESULT_ECX:
			result = ecx;
			break;
		case CPUID_RESULT_EDX:
			result = edx;
			break;
		default:
			grub_printf("%s - unknown result register ID %u\n",
					cmd_names[CMD_CHECK_CPUID], result_register);
			return -1;
	}

	if ((result & mask) != value) {
		grub_printf("%s - '%s' failed (expected 0x%x, got 0x%x)\n",
				cmd_names[CMD_CHECK_CPUID], msg, value, result);
		return -1;
	}

	return 0;
}

static int csl_dispatch(const u16 cmd, const u64 length)
{
	unsigned int i;
	u8 dummy;

	debug("Dispatching cmd %u with data length 0x%llx\n", cmd, length);

	/*
	 * FILO's file_read function can only read (int) bytes because of return
	 * type, be conservative and assume the whole data is read via
	 * csl_fs_ops.read function.
	 */
	if ((int) length < 0) {
		grub_printf("Data length out of range - 0x%llx\n", length);
		return -1;
	}

	switch (cmd)
	{
		case CMD_WRITE:
			return csl_cmd_write(length);
		case CMD_FILL:
			return csl_cmd_fill(length);
		case CMD_SET_ENTRY_POINT:
			return csl_cmd_set_entry_point(length);
		case CMD_CHECK_CPUID:
			return csl_cmd_check_cpuid(length);
		case VENDOR_CMD_ID_START ... VENDOR_CMD_ID_END:
			for (i = 0; i < length; i++)
				if (csl_fs_ops.read(&dummy, 1) != 1) {
					grub_printf("Unable to discard vendor command payload\n");
					return -1;
				}

			return 0;
		default:
			grub_printf("Unknown command ID %u\n", cmd);
			return -1;
	}
}

static int csl_entry(size_t entry_addr)
{
	struct context *ctx;
	struct segment_desc *mb_gdt;

	if (entry_point == ULONG_MAX) {
		grub_printf("No entry point set, stopping boot\n");
		return -1;
	}

	ctx = init_context(phys_to_virt(CSL_CONTEXT_LOC), 0x1000, 0);

	mb_gdt = phys_to_virt(CSL_GDT_LOC);
	memset(mb_gdt, 0, 13 * sizeof(struct segment_desc));
	/* Normal kernel code/data segments */
	mb_gdt[2] = gdt[FLAT_CODE];
	mb_gdt[3] = gdt[FLAT_DATA];
	mb_gdt[12] = gdt[FLAT_CODE];
	mb_gdt[13] = gdt[FLAT_DATA];
	ctx->gdt_base = CSL_GDT_LOC;
	ctx->gdt_limit = 14 * 8 - 1;
	ctx->cs = 0x10;
	ctx->ds = 0x18;
	ctx->es = 0x18;
	ctx->fs = 0x18;
	ctx->gs = 0x18;
	ctx->ss = 0x18;

	ctx->eip = (u32) entry_addr;

	grub_printf("Jumping to entry point @ 0x%x\n", ctx->eip);
	ctx = switch_to(ctx);

	/* Not reached */
	return 0;
}

int csl_load(const char *const file, const char *const cmdline)
{
	u64 cmd, length, vermagic;
	int err = -1;

	grub_printf("CSL - processing stream '%s'\n", file);

	if (!csl_fs_ops.open(file)) {
		grub_printf("CSL - failed to open '%s'\n", file);
		return -1;
	}

	if (csl_fs_ops.read(&vermagic, 8) != 8) {
		grub_printf("CSL - failed to read version magic from '%s'\n", file);
		goto err_file_close;
	}
	if (vermagic != my_vermagic) {
		grub_printf("CSL - version magic mismatch [got: 0x%llx, expected: 0x%llx]\n",
				vermagic, my_vermagic);
		goto err_file_close;
	}

	while (csl_fs_ops.read(&cmd, 8) == 8) {
		if (csl_fs_ops.read(&length, 8) != 8) {
			grub_printf("'%s' - unable to read data length\n", file);
			goto err_file_close;
		}
		err = csl_dispatch(cmd & 0xffff, length);
		if (err < 0)
			goto err_file_close;
	}

	csl_fs_ops.close();

	return csl_entry(entry_point);

err_file_close:
	csl_fs_ops.close();
	return err;
}
