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

#ifndef X86_PAE_H
#define X86_PAE_H

/* Fill memory specified by physical address and length with a constant byte */
void memset_pae(uint64_t dest, unsigned char pat, uint64_t length);

/*
 * Use given function read_func to read length bytes from buf to physical
 * address dest
 */
int read_pae(uint64_t dest, uint64_t length,
		int (*read_func)(void *buf, unsigned long len));

#endif /* X86_PAE_H  */
