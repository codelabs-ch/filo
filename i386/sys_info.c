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

#include <libpayload.h>
#include <config.h>
#include <sys_info.h>
#include "context.h"
#define DEBUG_THIS CONFIG_DEBUG_SYS_INFO
#include <debug.h>

void collect_sys_info(struct sys_info *info)
{
    /* Pick up paramters given by bootloader to us */
    info->boot_type = boot_ctx->eax;
    info->boot_data = boot_ctx->ebx;
    info->boot_arg = boot_ctx->param[0];
    debug("boot eax = %#lx\n", info->boot_type);
    debug("boot ebx = %#lx\n", info->boot_data);
    debug("boot arg = %#lx\n", info->boot_arg);

    collect_elfboot_info(info);
}
