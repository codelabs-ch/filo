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
 * Foundation, Inc.
 */

#include <drivers.h>
#include <sound.h>
#include <pci.h>
#include <debug.h>

static const struct sound_ops *ops;

int sound_init(void)
{
    struct driver *drv;
    pcidev_t dev = 0;

    for (drv = drivers_start; drv < drivers_end; drv++) {
	if (drv->type != DRIVER_SOUND)
		continue;
	if (pci_find_device(drv->vendor, drv->device, &dev)) {
	    if (drv->sound_ops->init(dev) == 0) {
		ops = drv->sound_ops;
		return 0;
	    }
	}
    }
    printf("No sound device found\n");
    return -1;
}

void sound_set_rate(int rate)
{
    if (ops && ops->set_rate)
	ops->set_rate(rate);
}

void sound_set_volume(int volume)
{
    if (ops && ops->set_volume)
	ops->set_volume(volume);
}

int sound_write(const void *buf, int size)
{
    if (ops && ops->write)
	return ops->write(buf, size);
    return -1;
}

int sound_is_active(void)
{
    if (ops && ops->is_active)
	return ops->is_active();
    return 0;
}

void sound_stop(void)
{
    if (ops && ops->stop)
	ops->stop();
}
