/*
 * This file is part of FILO.
 *
 * Copyright (C) 2008 coresystems GmbH
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

#include <fs.h>
#include <usb/usb.h>
#include <usb/usbmsc.h>


// FIXME: should be dynamic?
#define maxdevs 4
static usbdev_t* devs[maxdevs];
static int count = -1;

void usbdisk_create (usbdev_t* dev)
{
	if (count == maxdevs-1) return;
	devs[++count] = dev;
}

void usbdisk_remove (usbdev_t* dev)
{
	int i;
	if (count == -1) return;
	if (devs[count] == dev) {
		count--;
		return;
	}
	for (i=0; i<count; i++) {
		if (devs[i] == dev) {
			devs[i] = devs[count];
			count--;
			return;
		}
	}
}

int usb_new_probe(int drive)
{
	/* FIXME: need a place to periodically poll usb_poll().
	   or at least at sensible times.
	   this would be a nice place, but the usb disk handling
	   must be more clever for that.
	*/
	usb_poll();
	if (count >= drive) return 0;
	return -1;
}

int usb_new_read(const int drive, const sector_t sector, const int size, void *buffer)
{
	if (count < drive) return -1;
	int result = -readwrite_blocks(devs[drive], sector, size, cbw_direction_data_in, buffer);
	return result;
}
