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
#include <sound.h>
#define DEBUG_THIS CONFIG_DEBUG_VIA_SOUND
#include <debug.h>

static u16 viasnd_iobase;

static u16 via_ac97_read(u8 reg)
{
	u32 data;

	data = ((u32) reg << 16) | (1 << 23) | (1 << 25);
	outl(data, viasnd_iobase + 0x80);
	for (;;) {
		if ((inl(viasnd_iobase + 0x80) & ((1 << 24) | (1 << 25))) == (1 << 25))
			break;
	}
	udelay(25);
	data = inl(viasnd_iobase + 0x80);
	outb(2, viasnd_iobase + 0x83);
	if (((data & 0x7f0000) >> 16) != reg) {
		printf("not our reg\n");
		return 0;
	}
	return data & 0xffff;
}

static void via_ac97_write(u8 reg, u16 value)
{
	u32 data;

	data = ((u32) reg << 16) | value;
	outl(data, viasnd_iobase + 0x80);
	udelay(10);

	for (;;) {
		if ((inl(viasnd_iobase + 0x80) & (1 << 24)) == 0)
			break;
	}
}

static void viasnd_stop(void)
{
	via_ac97_write(0x18, via_ac97_read(0x18) | 0x8000);	/* PCM mute */
	outb(0x40, viasnd_iobase + 1);	/* SGD stop */
}

static int viasnd_init(pcidev_t dev)
{
	viasnd_iobase = pci_read_config16(dev, 0x10) & ~1;
	debug("Found VIA sound device at %#x\n", viasnd_iobase);
	pci_write_config8(dev, 0x41, 0xcc);	/* Enable AC link, VSR on, SGD on */
	pci_write_config8(dev, 0x48, 0x05);	/* Disable FM interrupt */
	outl(0, viasnd_iobase + 0x8c);	/* Disable codec GPI interrupt */

	viasnd_stop();

	via_ac97_write(0x2a, via_ac97_read(0x2a) | 1);	/* variable rate on */
	via_ac97_write(0x26, 0x0100);	/* Input power down, all other power up */

	via_ac97_write(0x18, 0x8808);	/* PCM out 0dB mute */
	via_ac97_write(0x02, 0x0000);	/* Master 0dB unmute */
	via_ac97_write(0x2c, 44100);	/* DAC rate */
	debug("DAC rate set to %dHz\n", via_ac97_read(0x2c));

#if 0
	viasnd_sgtable.base = DMA_ADDR;
	viasnd_sgtable.count = DMA_SIZE | (1 << 31);	/* EOL flag */
	outl(virt_to_phys(&viasnd_sgtable), viasnd_iobase + 4);	/* SGD table pointer */
	outb(0xb0, viasnd_iobase + 2);	/* Loop, 16-bit stereo, no interrupt */
#endif

	return 0;
}

static struct sound_ops viasnd_ops = {
	.init = viasnd_init,
	.stop = viasnd_stop,
};

const struct sound_driver viasnd_driver[] __sound_driver = {
	{0x1106, 0x3058, &viasnd_ops},	/* VT82C686 AC97 Audio Controller */
};
