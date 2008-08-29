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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <fs.h>
#include "usb/usb.h"
#include "usb/usbmsc.h"


static usbdev_t* devs[4]; // FIXME: only 4 devices
static int count = -1;

void usbdisk_create (usbdev_t* dev)
{
	if (count == 3) return;
	devs[++count] = dev;
}

void usbdisk_remove (usbdev_t* dev)
{
	/* FIXME: actually remove the right device */
	if (count == -1) return;
	count--;
}

int usb_new_probe(int drive)
{
	/* FIXME: need a place to periodically poll usb_poll().
	   or at least at sensible times.
	   this would be a nice place, but the usb disk handling
	   must be more clever for that.
	*/
	if (count >= drive) return drive;
	return -1;
}

int usb_new_read(int drive, sector_t sector, void *buffer)
{
	if (count < drive) return -1;
	/* FIXME: only one sector at a time :-(
	   This must happen some layers further up
	*/
	const int size = 1;
	int result = -readwrite_blocks(devs[drive], sector, size, cbw_direction_data_in, buffer);
	return result;
}
