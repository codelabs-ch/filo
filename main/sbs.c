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

#include <csl.h>
#include <sbs.h>

static struct sbs_header_t header;

static grub_uint64_t encoded_file_size = 0;

struct reader_state_type
{
	grub_file_t fd;
	grub_uint64_t bytes_remaining;
	unsigned int sbs_valid;
	grub_uint8_t next_hash[SHA512_HASHSUM_LEN];
	grub_uint8_t *read_pos;
	grub_uint8_t *data;
};

static struct reader_state_type state =
{
	.fd              = NULL,
	.bytes_remaining = 0,
	.sbs_valid       = 0,
	.read_pos        = NULL,
	.data            = NULL
};

static const gcry_md_spec_t *hasher = NULL;
static void *hash_ctx = NULL;

/* --- TODO: factor out from pgp.c? --- */

static struct grub_public_key *grub_pk_trusted = NULL;

static grub_ssize_t
pseudo_read (struct grub_file *file, char *buf, grub_size_t len)
{
	grub_memcpy (buf, (grub_uint8_t *) file->data + file->offset, len);
	return len;
}

static grub_err_t
pseudo_close (struct grub_file *file __attribute__ ((unused)))
{
	return GRUB_ERR_NONE;
}

static struct grub_fs pseudo_fs =
{
	.name = "pseudo",
	.fs_read = pseudo_read,
	.fs_close = pseudo_close
};

/* --- */

/* Return length of block data in bytes */
static inline grub_uint32_t bdl (void)
{
	return header.block_size - header.hashsum_len;
}

/*
 * Return current read buffer fill state in bytes. Requires initialized reader
 * state
 */
static inline grub_uint32_t
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

/* Close fd if not already closed */
static void
close_fd (void)
{
	if (state.fd)
	{
		grub_file_close (state.fd);
		state.fd = NULL;
	}
}

/* Invalidate global state and cleanup */
static void
invalidate (void)
{
	state.sbs_valid = 0;
	state.read_pos  = NULL;

	if (state.data)
	{
		grub_free (state.data);
		state.data = NULL;
	}
	if (hash_ctx)
	{
		grub_free (hash_ctx);
		hash_ctx = NULL;
	}
	close_fd ();
}

/* Print len bytes of given buffer to console */
static void
print_buffer (const grub_uint8_t * const b, const unsigned len)
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
	grub_file_t fd_hdr = NULL, fd_sig = NULL;

	grub_uint8_t signature[PGP_RSA4096_SIG_LEN];

	if (grub_file_read (state.fd, signature, header.sig_len)
			!= (grub_ssize_t) header.sig_len)
	{
		grub_printf ("SBS - unable to read signature\n");
		return 0;
	}

	/* must be heap since grub_file_close calls grub_free on it */
	fd_hdr = grub_malloc (sizeof (struct grub_file));
	fd_sig = grub_malloc (sizeof (struct grub_file));
	if (fd_hdr == NULL || fd_sig == NULL)
	{
		grub_printf ("SBS - unable to allocate pseudo fds\n");
		return 0;
	}

	grub_memset (fd_hdr, 0, sizeof (*fd_hdr));
	fd_hdr->fs = &pseudo_fs;
	fd_hdr->size = sizeof (struct sbs_header_t);
	fd_hdr->data = (char *) &header;

	grub_memset (fd_sig, 0, sizeof (*fd_sig));
	fd_sig->fs = &pseudo_fs;
	fd_sig->size = header.sig_len;
	fd_sig->data = signature;

	if (grub_verify_signature2 (fd_hdr, fd_sig, grub_pk_trusted)
			!= GRUB_ERR_NONE)
	{
		grub_printf ("SBS - signature verification failed: %s\n", grub_errmsg);
		return 0;
	}

	grub_printf ("SBS - signature valid\n");
	state.sbs_valid = 1;
	return 1;
}

/* Read next block hash and data */
static unsigned
read_block (void)
{
	if (grub_file_read (state.fd, state.next_hash, header.hashsum_len)
			!= header.hashsum_len)
	{
		grub_printf ("SBS - unable to read block hash value\n");
		return 0;
	}
	if (grub_file_read (state.fd, state.data, bdl ()) != (grub_ssize_t) bdl ())
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
hash_valid (const grub_uint8_t * const h)
{
	unsigned res;

	hasher->init (hash_ctx);
	hasher->write (hash_ctx, state.next_hash, SHA512_HASHSUM_LEN);
	hasher->write (hash_ctx, state.data, bdl ());
	hasher->final (hash_ctx);

	if (grub_crypto_memcmp (h, hasher->read (hash_ctx), hasher->mdlen) == 0)
	{
		res = 1;
	}
	else
	{
		res = 0;

		grub_printf ("SBS - ERROR invalid hash detected\n");
		grub_printf ("SBS - computed hash ");
		print_buffer (hasher->read (hash_ctx), SHA512_HASHSUM_LEN);
		grub_printf ("\n");
		grub_printf ("SBS - stored hash   ");
		print_buffer (h, SHA512_HASHSUM_LEN);
		grub_printf ("\n");
	}

	return res;
}

/* Read next block and verify hashsum */
static unsigned
load_next_block (const grub_uint32_t offset)
{
	grub_uint8_t current_hash[SHA512_HASHSUM_LEN];

	grub_memcpy (current_hash, state.next_hash, SHA512_HASHSUM_LEN);

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
		const grub_size_t width,
		const char * const err_msg)
{
	if (grub_file_read (state.fd, field, width) != (grub_ssize_t) width)
	{
		grub_printf ("SBS - %s\n", err_msg);
		close_fd ();
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
		goto header_invalid;
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
		grub_printf ("SBS - incorrect header size %u [expected: %"
				PRIuGRUB_SIZE "]\n",
				header.header_size, sizeof (struct sbs_header_t));
		goto header_invalid;
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
		goto header_invalid;
	}
	if (header.hashsum_len != SHA512_HASHSUM_LEN)
	{
		grub_printf ("SBS - unexpected hashsum length %u, expected %u\n",
				header.hashsum_len, SHA512_HASHSUM_LEN);
		goto header_invalid;
	}

	if (header.sig_scheme_id != SBS_SIG_SCHEME_PGP)
	{
		grub_printf ("SBS - unsupported signature scheme with ID %u\n",
				header.sig_scheme_id);
		goto header_invalid;
	}
	if (header.sig_len != PGP_RSA4096_SIG_LEN)
	{
		grub_printf ("SBS - unexpected sginature length %u, expected %u\n",
				header.sig_len, PGP_RSA4096_SIG_LEN);
		goto header_invalid;
	}

	if (! read_field (header.root_hash, header.hashsum_len,
				"unable to read root hash"))
		goto header_invalid;

	return 1;

header_invalid:
	close_fd ();
	return 0;
}

/* Open SBS file with given name */
static grub_file_t
sbs_open (const char *name,
		enum grub_file_type type __attribute__ ((unused)))
{
	if (state.sbs_valid)
		return NULL;

	state.fd = grub_file_open (name, GRUB_FILE_TYPE_SBS);
	if (! state.fd)
	{
		grub_printf ("SBS - unable to open '%s'\n", name);
		return NULL;
	}

	if (! read_header ())
	{
		close_fd ();
		return NULL;
	}

	if (! verify ())
	{
		close_fd ();
		return NULL;
	}

	/* initialize hasher and associated context */
	hasher = grub_crypto_lookup_md_by_name ("sha512");
	if (! hasher)
	{
		grub_printf ("SBS - unable to init hasher\n");
		close_fd ();
		return NULL;
	}
	hash_ctx = grub_zalloc (hasher->contextsize);
	if (! hash_ctx)
	{
		grub_printf ("SBS - unable to allocate hasher ctx\n");
		close_fd ();
		return NULL;
	}

	grub_printf ("SBS - block count       : %u\n", header.block_count);
	grub_printf ("SBS - initial padding   : %u\n", header.padding_len);
	grub_printf ("SBS - block size        : %u\n", header.block_size);
	grub_printf ("SBS - signature length  : %u\n", header.sig_len);
	grub_printf ("SBS - hashsum length    : %u\n", header.hashsum_len);
	grub_printf ("SBS - block data length : %u\n", bdl ());

	encoded_file_size = (grub_uint64_t) header.block_count * bdl () - header.padding_len;
	state.bytes_remaining = encoded_file_size;
	grub_printf ("SBS - encoded file size : %" PRIuGRUB_UINT64_T "\n", encoded_file_size);

	grub_printf ("SBS - root hash         : ");
	print_buffer (header.root_hash, SHA512_HASHSUM_LEN);
	grub_printf ("\n");

	state.data = grub_malloc (bdl ());
	if (state.data == NULL)
	{
		grub_printf ("SBS - error allocating data buffer\n");
		invalidate ();
		return NULL;
	}

	/* set root hash as next hash and load first block */
	grub_memcpy (state.next_hash, header.root_hash, SHA512_HASHSUM_LEN);
	if (! load_next_block (header.padding_len))
	{
		invalidate ();
		return NULL;
	}

	/* indicate success via fake file */
	return (grub_file_t) GRUB_ULONG_MAX;
}

/* Return size of encoded data */
static grub_off_t
sbs_size (grub_file_t file __attribute__ ((unused)))
{
	if (!state.sbs_valid)
		return 0;

	return encoded_file_size;
}

/* Read len bytes from SBS file */
static grub_ssize_t
sbs_read (grub_file_t file __attribute__ ((unused)),
		void *buf,
		grub_size_t len)
{
	grub_size_t to_read = len;
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

		const grub_uint32_t to_copy = to_read > buffer_bytes ()
			? buffer_bytes () : to_read;
		grub_memcpy (ptr, state.read_pos, to_copy);

		state.bytes_remaining -= to_copy;
		state.read_pos        += to_copy;
		to_read               -= to_copy;

		ptr = (grub_uint8_t *) ptr + to_copy;
	}

	return (grub_uint8_t *) ptr - (grub_uint8_t *) buf;

invalid:
	grub_printf("SBS - error in block read\n");
	invalidate ();
	return -1;
}

/* Close SBS file. Invalidates state */
static grub_err_t
sbs_close (grub_file_t file __attribute__ ((unused)))
{
	invalidate ();
	return GRUB_ERR_NONE;
}

/* Override CSL file operations */
static grub_err_t
sbs_init (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char **argv __attribute__ ((unused)))
{
	grub_printf ("SBS - overriding CSL file ops...\n");
	csl_fs_ops.open  = sbs_open;
	csl_fs_ops.read  = sbs_read;
	csl_fs_ops.size  = sbs_size;
	csl_fs_ops.close = sbs_close;

	return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(sbs)
{
	struct grub_module_header *mod_header;

	/* only look for one key. */
	// TODO: factor out from pgp.c?
	FOR_MODULES (mod_header)
	{
		struct grub_file pseudo_file;
		struct grub_public_key *pk = NULL;

		grub_memset (&pseudo_file, 0, sizeof (pseudo_file));

		/* not a pubkey, skip.  */
		if (mod_header->type != OBJ_TYPE_PUBKEY)
			continue;

		pseudo_file.fs = &pseudo_fs;
		pseudo_file.size = (mod_header->size - sizeof (struct grub_module_header));
		pseudo_file.data = (char *) mod_header + sizeof (struct grub_module_header);

		pk = grub_load_public_key (&pseudo_file);
		if (!pk)
			grub_fatal ("SBS - error loading initial key: %s\n", grub_errmsg);

		grub_pk_trusted = pk;
		break;
	}
	if (! grub_pk_trusted)
		grub_fatal ("SBS - unable to init trusted pubkey\n");

	cmd = grub_register_extcmd ("sbs_init", sbs_init, 0, 0,
			"Initialize Signed Block Stream (SBS) processing.", 0);
}

GRUB_MOD_FINI(sbs)
{
	invalidate();
	grub_unregister_extcmd (cmd);
}
