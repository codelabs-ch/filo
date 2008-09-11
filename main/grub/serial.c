/* serial.c - serial device interface */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libpayload.h>
#include <config.h>
#include <grub/shared.h>
#include <grub/term.h>
#include <grub/terminfo.h>

/* An input buffer.  */
static char input_buf[8];
static int npending = 0;

static int serial_x;
static int serial_y;

static int keep_track = 1;

/* Fetch a key.  */
int
serial_hw_fetch (void)
{
#if CONFIG_SERIAL_CONSOLE
  if(serial_havechar())
	  return serial_getchar();
#endif
  return -1;
}

/* Put a chararacter.  */
void
serial_hw_put (int c)
{
#if CONFIG_SERIAL_CONSOLE
	serial_putchar(c);
#endif
}

/* Return the port number for the UNITth serial device.  */
unsigned short
serial_hw_get_port (int unit)
{
#if CONFIG_SERIAL_CONSOLE
  return CONFIG_SERIAL_IOBASE;
#else
  return 0;
#endif
}

/* Initialize a serial device. PORT is the port number for a serial device.
   SPEED is a DTE-DTE speed which must be one of these: 2400, 4800, 9600,
   19200, 38400, 57600 and 115200. WORD_LEN is the word length to be used
   for the device. Likewise, PARITY is the type of the parity and
   STOP_BIT_LEN is the length of the stop bit. The possible values for
   WORD_LEN, PARITY and STOP_BIT_LEN are defined in the header file as
   macros.  */
int
serial_hw_init (unsigned short port, unsigned int speed,
		int word_len, int parity, int stop_bit_len)
{
#if CONFIG_CONSOLE_SERIAL
  int i;
  /* Drain the input buffer.  */
  while (serial_checkkey () != -1) {
    (void) serial_getkey ();
  }

  /* Get rid of TERM_NEED_INIT from the serial terminal.  */
  for (i = 0; term_table[i].name; i++)
    if (grub_strcmp (term_table[i].name, "serial") == 0)
      {
	term_table[i].flags &= ~TERM_NEED_INIT;
	break;
      }
#endif
  return 1;
}

/* Generic definitions.  */

#if CONFIG_SERIAL_CONSOLE
static void
serial_translate_key_sequence (void)
{
  const struct
  {
    char key;
    char ascii;
  }
  three_code_table[] =
    {
      {'A', 16},
      {'B', 14},
      {'C', 6},
      {'D', 2},
      {'F', 5},
      {'H', 1},
      {'4', 4}
    };

  const struct
  {
    short key;
    char ascii;
  }
  four_code_table[] =
    {
      {('1' | ('~' << 8)), 1},
      {('3' | ('~' << 8)), 4},
      {('5' | ('~' << 8)), 7},
      {('6' | ('~' << 8)), 3},
    };
  
  /* The buffer must start with ``ESC [''.  */
  if (*((unsigned short *) input_buf) != ('\e' | ('[' << 8)))
    return;
  
  if (npending >= 3)
    {
      int i;

      for (i = 0;
	   i < sizeof (three_code_table) / sizeof (three_code_table[0]);
	   i++)
	if (three_code_table[i].key == input_buf[2])
	  {
	    input_buf[0] = three_code_table[i].ascii;
	    npending -= 2;
	    memmove (input_buf + 1, input_buf + 3, npending - 1);
	    return;
	  }
    }

  if (npending >= 4)
    {
      int i;
      short key = *((short *) (input_buf + 2));

      for (i = 0;
	   i < sizeof (four_code_table) / sizeof (four_code_table[0]);
	   i++)
	if (four_code_table[i].key == key)
	  {
	    input_buf[0] = four_code_table[i].ascii;
	    npending -= 3;
	    memmove (input_buf + 1, input_buf + 4, npending - 1);
	    return;
	  }
    }
}
   
static
int fill_input_buf (int nowait)
{
  int i;

  for (i = 0; i < 10000 && npending < sizeof (input_buf); i++)
    {
      int c;

      c = serial_hw_fetch ();
      if (c >= 0)
	{
	  input_buf[npending++] = c;

	  /* Reset the counter to zero, to wait for the same interval.  */
	  i = 0;
	}
      
      if (nowait)
	break;
    }

  /* Translate some key sequences.  */
  serial_translate_key_sequence ();
	  
  return npending;
}
#endif

/* The serial version of getkey.  */
int
serial_getkey (void)
{
  int c;
#if CONFIG_SERIAL_CONSOLE
  while (! fill_input_buf (0))
    ;
#endif

  c = input_buf[0];
  npending--;
  memmove (input_buf, input_buf + 1, npending);
  
  return c;
}

/* The serial version of checkkey.  */
int
serial_checkkey (void)
{
#if CONFIG_SERIAL_CONSOLE
  if (fill_input_buf (1))
    return input_buf[0];
#endif

  return -1;
}

/* The serial version of grub_putchar.  */
void
grub_serial_putchar (int c)
{
  /* Keep track of the cursor.  */
  if (keep_track)
    {
      /* The serial terminal doesn't have VGA fonts.  */
      switch (c)
	{
	case DISP_UL:
	  c = ACS_ULCORNER;
	  break;
	case DISP_UR:
	  c = ACS_URCORNER;
	  break;
	case DISP_LL:
	  c = ACS_LLCORNER;
	  break;
	case DISP_LR:
	  c = ACS_LRCORNER;
	  break;
	case DISP_HORIZ:
	  c = ACS_HLINE;
	  break;
	case DISP_VERT:
	  c = ACS_VLINE;
	  break;
	case DISP_LEFT:
	  c = ACS_LARROW;
	  break;
	case DISP_RIGHT:
	  c = ACS_RARROW;
	  break;
	case DISP_UP:
	  c = ACS_UARROW;
	  break;
	case DISP_DOWN:
	  c = ACS_DARROW;
	  break;
	default:
	  break;
	}
      
      switch (c)
	{
	case '\r':
	  serial_x = 0;
	  break;
	  
	case '\n':
	  serial_y++;
	  break;
	  
	case '\b':
	case 127:
	  if (serial_x > 0)
	    serial_x--;
	  break;
	  
	case '\a':
	  break;
	  
	default:
	  if (serial_x >= 79)
	    {
	      grub_serial_putchar ('\r');
	      grub_serial_putchar ('\n');
	    }
	  serial_x++;
	  break;
	}
    }
  
  serial_hw_put (c);
}

int
serial_getxy (void)
{
  return (serial_x << 8) | serial_y;
}

void
serial_gotoxy (int x, int y)
{
  keep_track = 0;
  ti_cursor_address (x, y);
  keep_track = 1;
  
  serial_x = x;
  serial_y = y;
}

void
serial_cls (void)
{
  keep_track = 0;
  ti_clear_screen ();
  keep_track = 1;
  
  serial_x = serial_y = 0;
}

void
serial_setcolorstate (color_state state)
{
  keep_track = 0;
  if (state == COLOR_STATE_HIGHLIGHT)
    ti_enter_standout_mode ();
  else
    ti_exit_standout_mode ();
  keep_track = 1;
}

