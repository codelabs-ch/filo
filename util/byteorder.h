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


#ifndef _BYTESWAP_H
#define _BYTESWAP_H
#include <stdint.h>
/* These are unportable GNU functions */

static inline uint16_t bswap_16(uint16_t x)
{
	return (x>>8) | (x<<8);
}

static inline uint32_t bswap_32(uint32_t x)
{
	return (bswap_16(x&0xffff)<<16) | (bswap_16(x>>16));
}

static inline uint64_t bswap_64(uint64_t x)
{
	return (((uint64_t)bswap_32(x&0xffffffffull))<<32) | (bswap_32(x>>32));
}

#endif
