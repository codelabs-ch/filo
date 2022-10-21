#include <stdarg.h>
#include <string.h>

#include "flashchips.h"
#include "flash.h"
#include "programmer.h"

int new_rom_size;
void *new_rom_data;

int old_rom_size;
void *old_rom_data;

// for flashrom
int print(int type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int print(int type, const char *fmt, ...)
{
#ifdef DEBUG
	if (type > MSG_INFO) return 0;
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = vprintf(fmt, args);
	va_end(args);
	return ret;
#else
	return 0;
#endif
}

/* returned memory must be freed by caller */
char *flashbuses_to_text(enum chipbustype bustype)
{
	return strdup("Unknown");
}

int read_buf_from_file(unsigned char *buf, unsigned long size, const char *filename)
{
	// we need at least enough space for our image.
	if (size < new_rom_size)
		return 1;

	// top-align our image into the chip
	memcpy(buf + size - new_rom_size, new_rom_data, new_rom_size);

	return 0;
}

int read_flash_to_file(struct flashctx *flash, const char *filename)
{
	int ret = 0;
	unsigned long size = flash->total_size * 1024;

	if (size > old_rom_size) {
		msg_cerr("old rom buffer not large enough for flash\n");
		ret = 1;
		goto out_free;
	}

	if (!flash->read) {
		msg_cerr("No read function available for this flash chip.\n");
		ret = 1;
		goto out_free;
	}
	if (flash->read(flash, old_rom_data, 0, size)) {
		msg_cerr("Read operation failed!\n");
		ret = 1;
		goto out_free;
	}

out_free:
	return ret;
}

