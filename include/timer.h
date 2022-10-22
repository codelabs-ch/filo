/*
 * This file is part of FILO.
 *
 * (C) 2004-2008 coresystems GmbH
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

#ifndef	TIMER_H
#define TIMER_H

#include <libpayload.h>

u64 currticks(void);
int getrtsecs (void);

#define TICKS_PER_SEC timer_hz()
#define TICKS_PER_USEC (cpu_khz / 1000)

#endif	/* TIMER_H */
