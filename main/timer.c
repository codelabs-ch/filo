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

#include <libpayload.h>
#include <timer.h>

u64 currticks(void)
{
	return timer_raw_value();
}

int getrtsecs (void)
{
	u64 t;

	t=timer_raw_value();
	t=t / timer_hz();
	return (int)t;
}

