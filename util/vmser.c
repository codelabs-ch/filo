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


/*
 * Simple terminal for VMware serial port
 */
#include <stdio.h>
#include <string.h>
#include <curses.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

int main()
{
    int s;
    struct sockaddr_un addr;
    char buf[1024];
    int n;

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
	perror("socket");
	return 1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/vmserial");

    if (connect(s, (void *) &addr, sizeof addr) < 0) {
	perror("connect");
	return 1;
    }

    initscr();
    cbreak();
    if (fork()) {
	while ((n = read(s, buf, sizeof buf)) > 0)
	    write(1, buf, n);
	if (n < 0)
	    perror("read from socket");
    } else {
	while ((n = read(0, buf, sizeof buf)) > 0)
	    write(s, buf, n);
    }
    endwin();

    return 0;
}
