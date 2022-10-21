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

#ifndef SOUND_H
#define SOUND_H

#include <libpayload.h>

/*
 * Application interface
 */
int sound_init(void);
void sound_set_rate(int rate);
void sound_set_volume(int volume);
int sound_write(const void *buf, int size);
void sound_start(void);
void sound_stop(void);
int sound_is_active(void);

#endif /* SOUND_H */
