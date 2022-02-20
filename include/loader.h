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

#ifndef LOADER_H
#define LOADER_H

#include <stddef.h>
#include <stdint.h>

#include <config.h>

#define LOADER_NOT_SUPPORT 0xbadf11e

struct boot_module {
	char *desc;
	char *params;
	char *filename;
	uintptr_t addr;
	size_t size;
};

#ifdef CONFIG_ELF_BOOT
int elf_boot(const char *filename, const char *cmdline);
#else
#define elf_boot(x,y) LOADER_NOT_SUPPORT /* nop */
#endif
int elf_load(uintptr_t *entry, bool elfboot);

#ifdef CONFIG_LINUX_LOADER
int linux_load(const char *filename, const char *cmdline);
#else
#define linux_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#ifdef CONFIG_WINCE_LOADER
int wince_load(const char *filename, const char *cmdline);
#else
#define wince_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

#ifdef CONFIG_ARTEC_BOOT
int artecboot_load(const char *filename, const char *cmdline);
#else
#define artecboot_load(x,y) LOADER_NOT_SUPPORT /* nop */
#endif

/*
 * Allocate `size` bytes for a boot module. All other parameters
 * are optional, i.e. can be NULL.
 *
 * If `addr` is given, it will receive the physical address of the
 * allocated space.
 *
 * `desc` will be used as description in console output.
 *
 * On success a pointer to the new module is returned, NULL otherwise.
 */
const struct boot_module *register_boot_module(uintptr_t *addr, size_t size, const char *desc);

/*
 * Allocate a boot module backed by a file given by `filename`.
 * All other parameters are optional, i.e. can be NULL.
 *
 * `desc` will be used as description in console output. `params`
 * can be passed to the module in a loader specific way, e.g. as
 * command line for Multiboot modules.
 *
 * The file won't be read right away. It's queued to be read into
 * the allocated space by process_boot_modules(). However, the
 * file will be opened temporarily to query its size.
 *
 * On success a pointer to the new module is returned, NULL otherwise.
 */
const struct boot_module *register_file_module(const char *filename, const char *params, const char *desc);

/*
 * Load all files associated with a boot module.
 *
 * This should be called by loaders that support additional file
 * modules, right before jumping to the loaded program.
 */
int process_boot_modules(void);

/*
 * Clear the list of modules and reclaim allocated space.
 *
 * This will be called in case a loaded program returns and leaves
 * a clean state (i.e. we can start from scratch, loading another
 * program and modules).
 */
void clear_boot_modules(void);

/* Provide information about the currently registered modules. */
const struct boot_module *next_boot_module(const struct boot_module *);

/*
 * Perform generic preparations like exiting device drivers before
 * jumping to a loaded program.
 */
int prepare_for_jump(void);

/*
 * Perform generic tasks to continue execution after a loaded
 * program returned.
 */
void restore_after_jump(void);

#endif /* LOADER_H */
