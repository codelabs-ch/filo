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

#include <stdlib.h>
#include <fs.h>
#include <grub/shared.h>
#include <csl.h>
#include <sbs.h>

static struct sbs_header_t header;

static uint64_t encoded_file_size = 0;

struct reader_state_type
{
	uint64_t bytes_remaining;
	unsigned int sbs_valid;
	uint8_t next_hash[SHA512_HASHSUM_LEN];
	uint8_t *read_pos;
	uint8_t *data;
};

static struct reader_state_type state =
{
	.bytes_remaining = 0,
	.sbs_valid       = 0,
	.read_pos        = NULL,
	.data            = NULL
};

/* Return length of block data in bytes */
static inline uint32_t bdl (void)
{
	return header.block_size - header.hashsum_len;
}

/*
 * Return current read buffer fill state in bytes. Requires initialized reader
 * state
 */
static inline uint32_t
buffer_bytes (void)
{
	if (! state.sbs_valid)
		return 0;
	return bdl () - (state.read_pos - state.data);
}

/* Returns 1 if read buffer is empty, 0 otherwise */
static inline unsigned
buffer_empty (void)
{
	return buffer_bytes () == 0;
}

/* Invalidate global state and cleanup */
static void
invalidate (void)
{
	state.sbs_valid = 0;
	state.read_pos  = NULL;

	if (state.data)
	{
		free (state.data);
		state.data = NULL;
	}
}

/* Print len bytes of given buffer to console */
static void
print_buffer (const uint8_t * const b, const unsigned len)
{
	unsigned i;
	grub_printf ("0x");
	for (i = 0; i < len; i++)
		grub_printf ("%02x", b[i]);
}

/*
 * Verify header signature, returns 1 and sets sbs_valid to 1 if verification
 * succeeds, 0 otherwise.
 */
static unsigned verify (void)
{
	uint8_t signature[PGP_RSA4096_SIG_LEN];

	/* DUMMY IMPLEMENTATION */
	grub_printf ("SBS - dummy verify() WARNING\n");
	if (file_read (signature, header.sig_len) != header.sig_len)
	{
		grub_printf ("SBS - unable to read signature\n");
		return 0;
	}
	state.sbs_valid = 1;
	return 1;
}

/* Read next block hash and data */
static unsigned
read_block (void)
{
	if (file_read (state.next_hash, header.hashsum_len)
			!= header.hashsum_len)
	{
		grub_printf ("SBS - unable to read block hash value\n");
		return 0;
	}
	if (file_read (state.data, bdl ()) != bdl ())
	{
		grub_printf ("SBS - unable to read block data\n");
		return 0;
	}
	return 1;
}

/*
 * Verify that given hash matches the hash over state hashsum and data values.
 * Returns 1 if both hashsums are equal, 0 otherwise
 */
static unsigned
hash_valid (const uint8_t * const h)
{
	/* DUMMY IMPLEMENTATION */
	grub_printf ("SBS - dummy hash_valid(), returning true\n");
	return 1;
}

/* Read next block and verify hashsum */
static unsigned
load_next_block (const uint32_t offset)
{
	uint8_t current_hash[SHA512_HASHSUM_LEN];

	memcpy (current_hash, state.next_hash, SHA512_HASHSUM_LEN);

	if (! read_block ())
		return 0;

	if (! hash_valid (current_hash))
		return 0;

	state.read_pos = state.data + offset;
	return 1;
}

/*
 * Read a header field of given length. Closes fd and returns 0 if an error
 * occurrs, 1 otherwise.
 */
static unsigned
read_field (void *field,
		const unsigned int width,
		const char * const err_msg)
{
	if (file_read (field, width) != width)
	{
		grub_printf ("SBS - %s\n", err_msg);
		return 0;
	}

	return 1;
}

/* Read SBS header */
static unsigned
read_header (void)
{
	if (! read_field (&header.version_magic, 4, "unable to read version magic"))
		return 0;
	if (header.version_magic != SBS_VERMAGIC)
	{
		grub_printf ("SBS - version magic mismatch [got: 0x%x, expected: 0x%x]\n",
				header.version_magic, SBS_VERMAGIC);
		return 0;
	}

	if (! read_field (&header.block_count, 4, "unable to read block_count"))
		return 0;
	if (! read_field (&header.block_size, 4, "unable to read block size"))
		return 0;
	if (! read_field (&header.sig_len, 4, "unable to read signature length"))
		return 0;

	if (! read_field (&header.header_size, 2, "unable to read header size"))
		return 0;
	if (header.header_size != sizeof (struct sbs_header_t))
	{
		grub_printf ("SBS - incorrect header size %u [expected: %zu]\n",
				header.header_size, sizeof (struct sbs_header_t));
		return 0;
	}

	if (! read_field (&header.hashsum_len, 2, "unable to read hashsum length"))
		return 0;
	if (! read_field (&header.hash_algo_id_1, 2, "unable to read hash ID 1"))
		return 0;
	if (! read_field (&header.hash_algo_id_2, 2, "unable to read hash ID 2"))
		return 0;
	if (! read_field (&header.hash_algo_id_3, 2, "unable to read hash ID 3"))
		return 0;
	if (! read_field (&header.hash_algo_id_4, 2, "unable to read hash ID 4"))
		return 0;
	if (! read_field (&header.sig_scheme_id, 2,
				"unable to read signature scheme ID"))
		return 0;
	if (! read_field (&header.reserved, 2, "unable to read reserved field"))
		return 0;
	if (! read_field (&header.padding_len, 4, "unable to read padding length"))
		return 0;

	if (! (header.hash_algo_id_1 == SBS_HASH_ALGO_SHA2_512
				&& header.hash_algo_id_2 == SBS_HASH_ALGO_NONE
				&& header.hash_algo_id_3 == SBS_HASH_ALGO_NONE
				&& header.hash_algo_id_4 == SBS_HASH_ALGO_NONE))
	{
		grub_printf ("SBS - unsupported hash algorithm config "
				"(1: %u, 2: %u, 3: %u, 4: %u)\n",
				header.hash_algo_id_1, header.hash_algo_id_2,
				header.hash_algo_id_3, header.hash_algo_id_4);
		return 0;
	}
	if (header.hashsum_len != SHA512_HASHSUM_LEN)
	{
		grub_printf ("SBS - unexpected hashsum length %u, expected %u\n",
				header.hashsum_len, SHA512_HASHSUM_LEN);
		return 0;
	}

	if (header.sig_scheme_id != SBS_SIG_SCHEME_PGP)
	{
		grub_printf ("SBS - unsupported signature scheme with ID %u\n",
				header.sig_scheme_id);
		return 0;
	}
	if (header.sig_len != PGP_RSA4096_SIG_LEN)
	{
		grub_printf ("SBS - unexpected sginature length %u, expected %u\n",
				header.sig_len, PGP_RSA4096_SIG_LEN);
		return 0;
	}

	if (! read_field (header.root_hash, header.hashsum_len,
				"unable to read root hash"))
		return 0;

	return 1;
}

/* Open SBS file with given name */
int sbs_open (const char *name)
{
	int fd;

	if (state.sbs_valid)
		return 0;

	fd = file_open (name);
	if (! fd)
	{
		grub_printf ("SBS - unable to open '%s'\n", name);
		return 0;
	}

	if (! read_header ())
	{
		return 0;
	}

	if (! verify ())
	{
		return 0;
	}

	grub_printf ("SBS - block count       : %u\n", header.block_count);
	grub_printf ("SBS - initial padding   : %u\n", header.padding_len);
	grub_printf ("SBS - block size        : %u\n", header.block_size);
	grub_printf ("SBS - signature length  : %u\n", header.sig_len);
	grub_printf ("SBS - hashsum length    : %u\n", header.hashsum_len);
	grub_printf ("SBS - block data length : %u\n", bdl ());

	encoded_file_size = (uint64_t) header.block_count * bdl () - header.padding_len;
	state.bytes_remaining = encoded_file_size;
	grub_printf ("SBS - encoded file size : %llu\n", encoded_file_size);

	grub_printf ("SBS - root hash         : ");
	print_buffer (header.root_hash, SHA512_HASHSUM_LEN);
	grub_printf ("\n");

	state.data = malloc (bdl ());
	if (state.data == NULL)
	{
		grub_printf ("SBS - error allocating data buffer\n");
		invalidate ();
		return 0;
	}

	/* set root hash as next hash and load first block */
	memcpy (state.next_hash, header.root_hash, SHA512_HASHSUM_LEN);
	if (! load_next_block (header.padding_len))
	{
		invalidate ();
		return 0;
	}

	return 1;
}

/* Return size of encoded data */
unsigned long sbs_size (void)
{
	if (!state.sbs_valid)
		return 0;

	return encoded_file_size;
}

/* Read len bytes from SBS file */
int sbs_read (void *buf, unsigned long len)
{
	unsigned long to_read = len;
	void *ptr = buf;

	if (! state.sbs_valid)
		return -1;

	if (to_read > state.bytes_remaining)
		to_read = state.bytes_remaining;

	while (to_read)
	{
		if (buffer_empty ())
			if (! load_next_block (0))
				goto invalid;

		const uint32_t to_copy = to_read > buffer_bytes ()
			? buffer_bytes () : to_read;
		memcpy (ptr, state.read_pos, to_copy);

		state.bytes_remaining -= to_copy;
		state.read_pos        += to_copy;
		to_read               -= to_copy;

		ptr = (uint8_t *) ptr + to_copy;
	}

	return (uint8_t *) ptr - (uint8_t *) buf;

invalid:
	grub_printf("SBS - error in block read\n");
	invalidate ();
	return -1;
}

/* Close SBS file and invalidate state. */
void sbs_close (void)
{
	invalidate ();
	file_close ();
}
