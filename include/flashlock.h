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

#ifndef FLASHLOCK_H
#define FLASHLOCK_H

extern int flashrom_lockdown;
int lockdown_flash(void);
int intel_lockdown_flash(void);
int amd_lockdown_flash(void);

#endif /* FLASHLOCK_H */
