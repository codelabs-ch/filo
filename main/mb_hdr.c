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

/* Support for MultiBoot */
#define MB_HEADER_MAGIC		0x1BADB002
#define MB_RQ_FLAGS_4K		1
#define MB_REQUESTED_FLAGS	(MB_RQ_FLAGS_4K)

/* this header is parsed by the boot loader that loads FILO, e.g. GRUB */
struct mb_header {
	long magic;		/* multiboot magic */
	long features;	/* requested features */
	long chksum;	/* chksum for whole structure */
} mb_header __attribute__((section(".hdr.mb"))) = {
	.magic = MB_HEADER_MAGIC,
	.features = MB_REQUESTED_FLAGS,
	.chksum = -(MB_HEADER_MAGIC + MB_REQUESTED_FLAGS),
};
