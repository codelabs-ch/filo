#include <stdlib.h>
#include <getopt.h>
#include <libpayload.h>
#include <coreboot_tables.h>
#include <cbfs.h>
#include <grub/shared.h>
#include <fs.h>
#include <pci/pci.h>

/* flashrom defines */
#define CONFIG_INTERNAL 1
#include "flashchips.h"
#include "flash.h"
#include "programmer.h"

extern int new_rom_size;
extern void *new_rom_data;

struct flashctx flashchip;

static void beep_success()
{
	int i;

	for (i = 0; i < 5; i++) {
		speaker_tone(440, 200); mdelay(300);
		speaker_tone(660, 200); mdelay(300);
		speaker_tone(880, 200); mdelay(300);
		mdelay(3*1000);
	}
}

static void beep_fail()
{
	int i;

	for (i = 0; i < 5; i++) {
		speaker_tone(1200, 200); mdelay(300);
		speaker_tone( 660, 200); mdelay(300);
		mdelay(3*1000);
	}
}

static int init_flash(const char* flashtype)
{
	int j;

	verbose++;
	if (programmer_init(PROGRAMMER_INTERNAL, NULL)) {
		grub_printf("Could not initialize programmer\n");
		return -1;
	}
	for (j = 0; j < registered_programmer_count; j++) {
		int current_chip = -1;

		do {
			current_chip = probe_flash(&registered_programmers[j], ++current_chip, &flashchip, 0);
			if ((current_chip != -1) && ((flashtype == NULL) || (strcasecmp(flashchip.name, flashtype) == 0))) {
				return 0;
			}
		} while (current_chip != -1);
	}

	grub_printf("Could not find flash chip\n");
	return -1;
}

static int write_flash(void *imgdata, int size)
{
	return doit(&flashchip, 1, imgdata, 0, 1, 0, 1);
}

static int test_id_section(void *romarea, unsigned int romsize, int offset, char **vendor, char **model, char **version)
{
	/* data[-1]: romsize
	   data[-2]: offset top-of-rom to board model
	   data[-3]: offset top-of-rom to board vendor
	   data[-4]: offset top-of-rom to build version */
	unsigned int *data = romarea+romsize-offset;
	*version = romarea+romsize-data[-4];
	*vendor = romarea+romsize-data[-3];
	*model = romarea+romsize-data[-2];
	/* Assume that data[-1] matches filesize, and that vendor and model are laid out without extra space.
	   DO NOT assume that model aligns to the offsets, there may be future additions. */
	return ((data[-1] == romsize) && (*vendor+strnlen(*vendor, data[-2])+1 == *model));
}

int flashupdate_func(char *arg, int flags)
{
	int result = -1;
	int force = 0;
	char *chiptype = NULL;
	char *vendor = NULL;
	char *board = NULL;
	char *region = NULL;

	char **argv;
	int argc = to_argc_argv(arg, &argv);

	new_rom_data = NULL;

	char opt;

	optreset = optind = 1;
	while ((opt = getopt(argc, argv, "fc:v:b:i:")) != -1) {
		switch (opt) {
			case 'f':
				force = 1;
				break;
			case 'c':
				chiptype = optarg;
				break;
			case 'v':
				vendor = optarg;
				break;
			case 'b':
				board = optarg;
				break;
			case 'i':
				region = optarg;
				break;
			default:
				grub_printf("unsupported option -%c.\n", opt);
				goto out;
				break;
		}
	}

	if (optind >= argc) {
		grub_printf("No filename specified.\n");
		goto out;
	}
	const char *filename = argv[optind];

	if (vendor && board &&
		((strcmp(vendor, cb_mb_vendor_string(phys_to_virt(lib_sysinfo.cb_mainboard))) != 0) ||
		(strcmp(board, cb_mb_part_string(phys_to_virt(lib_sysinfo.cb_mainboard))) != 0))) {
		/* since flashupdate semantics is: iff board matches, write flash;
		   it's a success not to flash on another board. */
		result = 0;
		goto out;
	}

	if (region) {
		register_include_arg(region);
		layout_use_ifd();
	}

	/* Step 0: Init programmer/flash to find out if we _can_ flash (we should) */
	if (init_flash(chiptype) == -1) {
		grub_printf("Could not initialize flash programmer.\n");
		goto out;
	}

	/* Step 1: read requested image file */
	if (!file_open(filename)) {
		grub_printf("Could not open file '%s'\n", filename);
		goto out;
	}

	/* 16K of extra space, since we don't have feof() */
	new_rom_size = flashchip.total_size * 1024 + 16384;
	new_rom_data = malloc(new_rom_size);
	if (new_rom_data == NULL) {
		grub_printf("Could not allocate memory.\n");
		goto out;
	}
	void *readbuf = new_rom_data;
	int len;

	grub_printf("Loading image file... ");
	while ((len = file_read(readbuf, 16384)) != 0) {
		readbuf += len;
		/* We can abort if we cut into the last 16K:
		 * it's more than the flash is able to carry
		 */
		if (readbuf - new_rom_data > new_rom_size - 16384) {
			file_close();
			grub_printf("File too large for flash\n");
			goto out;
		}
	}
	new_rom_size = readbuf - new_rom_data;
	file_close();
	grub_printf("done\n");

	/* retarget all CBFS accesses to new image */
	setup_cbfs_from_ram(new_rom_data, new_rom_size);

	/* Step 2: Check Board IDs (that we work on the right image) */
	const char *cur_version = phys_to_virt(lib_sysinfo.cb_version);
	const char *cur_vendor = cb_mb_vendor_string(phys_to_virt(lib_sysinfo.cb_mainboard));
	const char *cur_board = cb_mb_part_string(phys_to_virt(lib_sysinfo.cb_mainboard));

	/* Step 2a: check if this is a coreboot image by looking for a CBFS master header */
	if (get_cbfs_header() != 0xffffffff) {
		/* Step 2b: look for .id section in new ROM */
		/* There are currently two locations where .id can reside: top-0x10 and top-0x80. There's
		   no indicator which one is used in any given image, though it should be stable on any given
		   board. As a heuristic, we can check if the data is sane at each location. */
		char *new_vendor;
		char *new_board;
		char *new_version;

		if (!test_id_section(new_rom_data, new_rom_size, 0x10, &new_vendor, &new_board, &new_version))
			if (!test_id_section(new_rom_data, new_rom_size, 0x80, &new_vendor, &new_board, &new_version)) {
				grub_printf("Could not detect if file supports this system.\n");
				goto out;
		}
		if ((strcmp(cur_vendor, new_vendor) != 0) || (strcmp(cur_board, new_board) != 0)) {
			grub_printf("File (%s %s) seems to be incompatible with current system (%s %s).\n",
				new_vendor, new_board, cur_vendor, cur_board);
			goto out;
		}

		/* Step 3: Check image version (so that we don't reflash the same image again and again) */
		if (strcmp(cur_version, new_version) == 0) {
			grub_printf("No need to update to the same version '%s'.\n", cur_version);
			/* TODO: exit successfully. right? */
			result = 0;
			goto out;
		}

		/* Step 4: Merge current CMOS data with cmos.defaults */
		/* Step 4a: Load new CMOS data */
		void *cmos_defaults = cbfs_find_file("cmos.default", CBFS_COMPONENT_CMOS_DEFAULT);
		void *cmos_layout = cbfs_find_file("cmos_layout.bin", CBFS_COMPONENT_CMOS_LAYOUT);
		/* Step 4a1: Point use_mem to cmos.default. */
		mem_accessor_base = cmos_defaults;
		if (cmos_defaults && cmos_layout) {
			/* Step 4b: Check if current CMOS data is valid. */
			if (options_checksum_valid(use_nvram)) {
				/* Step 4c: Iterate over current CMOS data */
				struct cb_cmos_entries *cmos_entry = first_cmos_entry(get_system_option_table());

				do {
					/* Step 4c1: Attempt to update new CMOS data with current values. Ignore failures */
					char *val = NULL;

					get_option_as_string(use_nvram, get_system_option_table(), &val, cmos_entry->name);
					if (val) {
						set_option_from_string(use_mem, cmos_layout, val, cmos_entry->name);
					}
				} while (cmos_entry = next_cmos_entry(cmos_entry));
			}

			/* Step 5: Write merged CMOS image to CMOS */
			/* TODO: do we need to skip 0x32 (RTC century)? */
			int i;

			for (i = 14; i < 256; i++)
				nvram_write(((u8*)cmos_defaults)[i], i);

			/* Step 6: Set cmos_defaults_loaded=no in CMOS data using new layout */
			set_option_from_string(use_nvram, cmos_layout, "No", "cmos_defaults_loaded");

			/* Step 6a: Set cmos_defaults_loaded=Yes in cmos.default in ROM image. */
			set_option_from_string(use_mem, cmos_layout, "Yes", "cmos_defaults_loaded");
		}
	} else {
		if (!force) {
			grub_printf("This is a not a coreboot image. Aborting.\n");
			goto out;
		}
	}

	grub_printf("Writing image to flash, please wait.\n");
	/* Step 7: Flash image */
	if (write_flash(new_rom_data, new_rom_size) == 0) {
		grub_printf("Flash was updated successfully.\n");
		result = 0;
		beep_success();
	} else {
		grub_printf("WARNING: Error while updating flash!!\n");
		beep_fail();
	}

	/* success */
	programmer_shutdown();

out:
	if (new_rom_data) free(new_rom_data);
	new_rom_data = NULL;
	if (argv) free(argv);
	return result;
}

