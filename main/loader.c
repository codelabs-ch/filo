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
 */

#include <libpayload.h>
#include <coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>

#include <fs.h>
#include <flashlock.h>
#include <grub/shared.h>
#include <loader.h>

#define DEBUG_THIS CONFIG_DEBUG_LOADER
#include <debug.h>

/*
 * Boot-Module Framework
 * ^^^^^^^^^^^^^^^^^^^^^
 *
 * Boot modules are used to handle and store files and other data chunks
 * related to the next-stage program, e.g. an OS kernel. We want to load
 * files only a single time to assist TOCTOU safe checks.
 *
 * Ideally,  files will be placed directly where the next-stage program
 * needs them. Hence, we avoid using the heap and instead allocate big,
 * continuous chunks of memory below FILO from top down. FILO is expec-
 * ted to be already relocated to the top of memory. This way, we avoid
 * cluttering lower memory that may be needed by the next-stage program.
 *
 * We keep a linked-list of `struct boot_module` with meta data on the
 * heap. Beside the physical address and size of the module data, some
 * common meta data can be stored in `struct boot_module`, e.g. a file
 * path if it's backed by a file, a descriptive name,  or a string for
 * module parameters.
 *
 * On x86, the layout in 32-bit space might look roughly as follows:
 *
 *   +-MMIO-----+
 *   | ...      |         ...........heap...........
 *   +-tables---+        '
 *   | |||||||| |       '     ...
 *   +-FILO-----+      '
 *   | stack    |_____'      +-module--meta_data-+
 *   | heap     |_____     .---addr              |<------ modules_head
 *   | data     |     `   /  | size              |
 *   | code     |      ` /   |          next     |
 *   +----------+       /    +-----------|-------+
 *   |          |      / `               v
 *   +-module---+     /  '   +-module--meta_data-+
 *   | GDT      |<---'   ' .---addr              |<------ modules_tail
 *   +----------+        '/  | size              |
 *   |          |        /   | "/boot/initramfs" |
 *   |          |       /'   |          next     |
 *   +-module---+      / '   +-----------|-------+
 *   | initrd   |<----'  '               v
 *   +----------+        `..........................
 *   |          |
 *
 */

struct boot_module_node {
	struct boot_module module;
	struct boot_module_node *next;
};

static struct boot_module_node *modules_head, *modules_tail;

/* Place in the highest range in the memory map, but not above `below`. */
static uintptr_t place_boot_module(const size_t size, const uintptr_t below)
{
	const struct memrange *const memranges = lib_sysinfo.memrange;
	unsigned long long highest_base = 0;
	uintptr_t ret = 0;
	int i;

	if (size > below)
		return 0;

	for (i = 0; i < lib_sysinfo.n_memranges; ++i) {
		if (memranges[i].type != CB_MEM_RAM)
			continue;

		if (highest_base > memranges[i].base || memranges[i].base >= below)
			continue;

		const unsigned long long top =
			MIN(memranges[i].base + memranges[i].size, below);
		if (top < size)
			continue;

		const unsigned long long addr = ALIGN_DOWN(top - size, 0x1000);
		if (addr < memranges[i].base)
			continue;

		highest_base = memranges[i].base;
		ret = addr;
	}

	return ret;
}

static const struct boot_module *register_boot_module_(
		const size_t size, const char *filename,
		const char *const params, const char *const desc)
{
	struct boot_module_node *const node = calloc(1, sizeof(*node));
	struct boot_module *const mod = &node->module;
	extern char _start[];

	if (!node)
		goto oom_ret;

	if (desc) {
		mod->desc = strdup(desc);
		if (!mod->desc)
			goto oom_ret;
	}

	if (params) {
		mod->params = strdup(params);
		if (!mod->params)
			goto oom_ret;
	}

	if (filename) {
		mod->filename = strdup(filename);
		if (!mod->filename)
			goto oom_ret;
	}

	mod->size = size;

	mod->addr = place_boot_module(size,
			modules_tail ? modules_tail->module.addr : virt_to_phys(_start));
	if (!mod->addr)
		goto oom_ret;

	if (modules_tail)
		modules_tail->next = node;
	else
		modules_head = node;
	modules_tail = node;

	return mod;

oom_ret:
	if (node) {
		free(mod->filename);
		free(mod->params);
		free(mod->desc);
	}
	free(node);
	errnum = ERR_WONT_FIT;
	return NULL;
}

const struct boot_module *register_boot_module(
		uintptr_t *const addr, const size_t size,
		const char *const desc)
{
	const struct boot_module *const mod =
		register_boot_module_(size, NULL, NULL, desc);
	if (mod && addr)
		*addr = mod->addr;

	return mod;
}

const struct boot_module *register_file_module(
		const char *const filename,
		const char *const params, const char *const desc)
{
	if (!file_open(filename)) {
		errnum = ERR_FILE_NOT_FOUND;
		return NULL;
	}

	const size_t size = file_size();
	file_close();

	return register_boot_module_(size, filename, params, desc);
}

static int load_file_module(const struct boot_module *const mod)
{
	if (mod->desc)
		printf("Loading %s", mod->desc);
	else
		printf("Loading module '%s'", mod->filename);
	debug(" to 0x%08lx", mod->addr);
	printf("... ");

	if (!file_open(mod->filename)) {
		printf("failed!\n");
		return -1;
	}
	const int ret = file_read(phys_to_virt(mod->addr), mod->size);
	file_close();

	if (ret != (int)mod->size) {
		printf("failed!\n");
		return -1;
	}
	printf("ok\n");
	return 0;
}

int process_boot_modules(void)
{
	const struct boot_module *mod = NULL;
	while ((mod = next_boot_module(mod))) {
		if (!mod->filename)
			continue;

		if (load_file_module(mod))
			return -1;
	}

	return 0;
}

void clear_boot_modules(void)
{
	while (modules_head) {
		struct boot_module_node *const node = modules_head;
		struct boot_module *const mod = &node->module;

		modules_head = node->next;
		free(mod->filename);
		free(mod->params);
		free(mod->desc);
		free(node);
	}

	modules_tail = NULL;
}

const struct boot_module *next_boot_module(const struct boot_module *const it)
{
	if (!it)
		return &modules_head->module;

	return &((const struct boot_module_node *)it)->next->module;
}

int prepare_for_jump(void) {
	if (IS_ENABLED(CONFIG_FLASHROM_LOCKDOWN)) {
		if (lockdown_flash())
			return -1;
	}

	if (IS_ENABLED(CONFIG_LP_USB))
		usb_exit();

	if (IS_ENABLED(CONFIG_LP_PC_KEYBOARD))
		keyboard_disconnect();

	if (IS_ENABLED(CONFIG_PCMCIA_CF)) {
		unsigned char *cf_bar;
		int i;

		cf_bar = phys_to_virt(pci_read_config32(PCI_DEV(0, 0xa, 1), 0x10));
		for (i = 0x836; i < 0x840; i++)
			cf_bar[i] = 0;
	}

	return 0;
}

void restore_after_jump(void) {
	console_init();
}
