/*
 * This file is part of FILO.
 *
 * (C) 2008 coresystems GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */


#ifndef DRIVERS_H
#define DRIVERS_H

#include <libpayload.h>

/*
 * Driver interface
 */

typedef enum {
	DRIVER_SOUND = 1,
	DRIVER_STORAGE = 2
} drivertype_t;

struct sound_ops {
	int (*init)(pcidev_t dev);
	void (*set_rate)(int rate);
	void (*set_volume)(int volume);
	int (*write)(const void *buf, int size);
	int (*is_active)(void);
	void (*stop)(void);
};

struct storage_ops {
	int (*init)(pcidev_t dev);
	int (*open)(int drive);
	int (*close)(int drive);
	void (*read_sector)(u64 sector, const void *buf, int size);
	char *(*name)(void);
};

struct driver {
	drivertype_t type;
	u16 vendor;
	u16 device;
	union {
		const struct storage_ops *storage_ops;
		const struct sound_ops *sound_ops;
	};
};

#define __driver __attribute__((unused, section(".rodata.drivers")))

/* defined by the linker */
extern struct driver drivers_start[];
extern struct driver drivers_end[];

#endif /* DRIVERS_H */
