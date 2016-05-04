/*
 *  Squashfs filesystem backend for GRUB (GRand Unified Bootloader)
 *
 *  Copyright (C) 2006  Luc Saillard  <luc@saillard.org>
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

#include "filesys.h"
#include "squashfs_fs.h"
#include "squashfs_zlib.h"

#define SUPERBLOCK ((struct squashfs_super_block *) (FSYS_BUF))
#define INODE_DATA ((union squashfs_inode_header *)\
    			((int)SUPERBLOCK + (((sizeof(struct squashfs_super_block)>>5)+1)*32)))
/*
 * We need to allocate two buffers of SQUASHFS_FILE_MAX_SIZE.
 * One will be used to lad the compressed data (that can be as large as
 * SQUASHFS_FILE_MAX_SIZE) and one that store the uncompressed data.  Our
 * current gunzip implementation doesn't support uncompress in place.
 *
 * Force a value in the array, to force gcc to allocate the buffer into the
 * data section, and not in bss section. Else grub will not allocate the
 * memory.
 */
static unsigned char cbuf_data[SQUASHFS_FILE_MAX_SIZE] = {1};
static unsigned char file_data[SQUASHFS_FILE_MAX_SIZE] = {1};

#define CBUF_DATA ((unsigned char *)cbuf_data)
#define FILE_DATA ((unsigned char *)file_data)

/* Use to cache the block data that is in FILE_DATA */
static int squashfs_old_block = -1;


#undef SQUASHFS_TRACE

#ifdef SQUASHFS_TRACE
#define TRACE(s, args...) \
  do { printf("squashfs: <%s> "s, __PRETTY_FUNCTION__, ## args); } while(0)
static const char *get_type(int type);
#else
#define TRACE(s, args...)
#endif

static void dump_memory(const void *buffer, int len);
static void inode_print(union squashfs_inode_header *inode);
static int inode_read(unsigned int inode_block, unsigned int inode_offset);


/*
 * Read a raw block at @address, length @len and put data in @output_data
 *
 * @arg address:
 * @arg len:
 * @arg output_data:
 *
 * @return the number of bytes read
 */
static int read_bytes(long long address, unsigned int len, void *output_data)
{
  int ret;

  TRACE("reading from position 0x%x, bytes %d\n", (int)address, len);
  disk_read_func = disk_read_hook;
  ret = devread(0, (int)address, len, output_data);
  disk_read_func = NULL;
  return ret;
}


/*
 * Read a block located at @start and uncompress it into @output_data
 *
 * @arg start: block to read in the filesystem
 * @arg compressed_size: store in this pointer, the size of the compressed
 *                       block. So you can find the next compressed block using
 *                       @start+@compressed_size.
 * @arg output_data: must an array of at least SQUASHFS_METADATA_SIZE which is the
 *                   maximum size.
 *
 * @return the size of the decompressed block. If an error occur, 0 is returned.
 */
static int read_block(long long start, int *compressed_size, void *output_data)
{
  unsigned short int c_byte;
  int offset = 2;

  if (! read_bytes(start, sizeof(c_byte), &c_byte))
   {
     TRACE("read_block: Failed to read c_byte\n");
     return 0;
   }

  TRACE("read_block: block @0x%x, %d %s bytes\n",
        (int)start,
	SQUASHFS_COMPRESSED_SIZE(c_byte),
	SQUASHFS_COMPRESSED(c_byte) ? "compressed\0" : "uncompressed\0");

  if (SQUASHFS_CHECK_DATA(SUPERBLOCK->flags))
    offset = 3;

  if (SQUASHFS_COMPRESSED(c_byte))
   {
     unsigned int bytes = SQUASHFS_METADATA_SIZE;
     int res;

     c_byte = SQUASHFS_COMPRESSED_SIZE(c_byte);
     if (! read_bytes(start + offset, c_byte, CBUF_DATA))
      {
	TRACE("Failed to read at offset %x, size %d\n", (int)(start + offset), c_byte);
	return 0;
      }

     res = squashfs_uncompress(output_data, &bytes, CBUF_DATA, c_byte);
     dump_memory(output_data, 48);

     if (res != Z_OK)
      {
	if (res == Z_MEM_ERROR)
	  TRACE("zlib::uncompress failed, not enough memory\n");
	else if (res == Z_BUF_ERROR)
	  TRACE("zlib::uncompress failed, not enough room in output buffer\n");
	else
	  TRACE("zlib::uncompress failed, unknown error %d\n", res);
	return 0;
      }

     if (compressed_size)
       *compressed_size = offset + c_byte;
     return bytes;
   }
  else
   {
     c_byte = SQUASHFS_COMPRESSED_SIZE(c_byte);

     if (! read_bytes(start + offset, c_byte, output_data))
      {
	TRACE("Failed to read at offset %x, size %d\n", (int)(start + offset), c_byte);
	return 0;
      }

     if (compressed_size)
       *compressed_size = offset + c_byte;

     return c_byte;
   }
}

/*
 * Read a data block located at @start and uncompress it into @block.
 * The size of a data block is known in advance and is large of SQUASHFS_FILE_MAX_SIZE.
 *
 * @arg start: block to read in the filesystem
 * @arg size: size of the block. The block can be compressed so it will be
 *            uncompressed automatically.
 * @arg output_data: must an array of at least SQUASHFS_METADATA_SIZE which is the
 *             maximum size.
 * @return the size of the decompressed block. If an error occur, 0 is returned.
 */
static int read_data_block(long long start, unsigned int size, void *output_data)
{
  int res;
  unsigned int bytes = SUPERBLOCK->block_size;
  int c_byte = SQUASHFS_COMPRESSED_SIZE_BLOCK(size);

  TRACE("block @0x%x, %d %s bytes\n",
        (int)start,
	SQUASHFS_COMPRESSED_SIZE_BLOCK(c_byte),
	SQUASHFS_COMPRESSED_BLOCK(c_byte) ? "compressed" : "uncompressed");

  if (SQUASHFS_COMPRESSED_BLOCK(size))
   {
     if (! read_bytes(start, c_byte, CBUF_DATA))
       return 0;

     res = squashfs_uncompress(output_data, &bytes, CBUF_DATA, c_byte);
     dump_memory(CBUF_DATA, 48);

     if (res != Z_OK)
      {
	if (res == Z_MEM_ERROR)
	  TRACE("zlib::uncompress failed, not enough memory\n");
	else if (res == Z_BUF_ERROR)
	  TRACE("zlib::uncompress failed, not enough room in output buffer\n");
	else
	  TRACE("zlib::uncompress failed, unknown error %d\n", res);
	return 0;
      }

     return bytes;
   }
  else
   {
     if (! read_bytes(start, c_byte, output_data))
       return 0;

     return c_byte;
   }
}


/*
 * Parse one directory header, and the return the corresponding inode for entry
 * named @entry_name.
 *
 * @param dir_header: struct of a dir_header + a list of dir_entry
 * @param entry_name: entry to find (ended by a nul character)
 *
 * @param result_inode_block: if the entry is present, return in this variable,
 *                            the inode_block number in the inode_table
 * @param result_inode_offset: if the entry is present, return in this variable,
 *                            the offset position in the block_inode_table.
 *
 * @return 0 if the entry is found, (@result_inode_offset and @result_inode_offset is valid)
 *        >0 the number of bytes read to parse the directory header, so this go to the next
 *           directory_header.
 *
 * a directory is represented by a list of squashfs_dir_entry, and a squashfs_dir_header.
 *
 *   .-----------------------------------.
 *   | squashfs_dir_header               |
 *   |   --> count (number of dir_entry) |
 *   |-----------------------------------|
 *   |  squashfs_dir_entry(0)            |
 *   |  ---------------------            |
 *   |  name of squashfs_dir_entry(0)    |
 *   |-----------------------------------|
 *   |  squashfs_dir_entry(...)          |
 *   |  -----------------------          |
 *   |  name of squashfs_dir_entry(...)  |
 *   |-----------------------------------|
 *   |  squashfs_dir_entry(n)            |
 *   |  ---------------------            |
 *   |  name of squashfs_dir_entry(n)    |
 *   |-----------------------------------|
  */
static int directory_lookup_1(const struct squashfs_dir_header *dir_header,
    			      const char   *entry_name,
			      unsigned int *result_inode_block,
			      unsigned int *result_inode_offset)
{
  const unsigned char *data;
  const struct squashfs_dir_entry *dir_entry;
  int dir_count, i, offset;
#ifdef SQUASHFS_TRACE
  char temp_name[SQUASHFS_NAME_LEN+1];
#endif

  data = (const unsigned char *)dir_header;
  offset = sizeof(struct squashfs_dir_header);
  dir_count = dir_header->count + 1;
  TRACE("Searching for %s in this directory (entries:%d inode:%d)\n",
        entry_name, dir_count, dir_header->inode_number);

  while (dir_count--)
   {
     dir_entry = (const struct squashfs_dir_entry *)(data + offset);
     offset += sizeof(struct squashfs_dir_entry);

#ifdef SQUASHFS_TRACE
     memcpy(temp_name, data+offset, dir_entry->size+1);
     temp_name[dir_entry->size+1] = 0;

     TRACE("directory entry [%s]: offset:%d  type:%s  size:%d  inode_number:%x\n",
	   temp_name, dir_entry->offset, get_type(dir_entry->type), dir_entry->size+1,
	   dir_entry->inode_number);
#endif

     /* Do a strcmp between the current entry and the entry_name */
     for (i=0; i<dir_entry->size+1; i++)
      {
	if (data[offset+i] != entry_name[i])
	  break;
      }
     if (i == dir_entry->size+1 && entry_name[i] == 0)
      {
	*result_inode_block = dir_header->start_block;
	*result_inode_offset = dir_entry->offset;
	return 0;
      }

     offset += dir_entry->size+1;
   }

  return offset;
}

/*
 * Lookup for entry @entry_name in the directory located at @@@inode_block:@inode_offset
 * If the entry is present, return the inode block of the entry into
 * @result_inode_block:@result_inode_offset.
 *
 * @param inode_block: inode block location of the directory
 * @param inode_offset: inode offset location of the direcotry
 * @param dir_size: the directory size. We need this inforamtion because a
 *                  directory can be composed of a list of directory_header
 * @param entryname: entry to find (ended by a nul character)
 * @param result_inode_block: if the entry is present, return in this variable,
 *                            the inode_block number in the inode_table
 * @param result_inode_offset: if the entry is present, return in this variable,
 *                            the offset position in the block_inode_table.
 *
 * @result: 0 if the entry was not found
 *          1 if the entry is found, and fill @result_inode_block, @result_inode_offset
 *
 *
 *  , @inode_block
 *  |
 *  |  .............
 *  |  .............
 *  |  ...       ...         ____ .------------------------.
 *  \> .-----------.      __/     | directory_header(0)    | }
 *     |           | ____/        |   ....                 |  }
 *     |------------/             | directory_header(xxx)  |   }
 *  -> |                          |   ...                  |    } dir_size
 *  |  |------------\____         | directory_header(n)    |   }
 *  |  |           |     \__      |   ...                  |  }
 *  |  .           .        \___  |________________________| }
 *  |  .___________.
 *  |  ...       ...
 *  |  .............
 *  |  .............
 *  |
 *  \-- @inode_offset
 *
 */
static int directory_lookup(unsigned int  inode_block,
                            unsigned int  inode_offset,
			    unsigned int  dir_size,
			    const char   *entryname,
			    unsigned int *result_inode_block,
			    unsigned int *result_inode_offset)
{
  int offset = 0, res, compressed_size, bytes;
  long long start = SUPERBLOCK->directory_table_start;
  long long end = SUPERBLOCK->fragment_table_start;

  TRACE("start=0x%x  end=0x%x (len=%d/0x%x)\n", (int)start, (int)end, (int)(end-start));

  while (start < end)
   {
     TRACE("reading block 0x%x (offset=0x%x)\n", (int)start, offset);
     res = read_block(start, &compressed_size, FILE_DATA);
     if (res == 0)
      {
	TRACE("failed to read block\n");
	return 0;
      }

     if (inode_block == offset)
       break;
     start += compressed_size;
     offset += compressed_size;
   }

  if (inode_block != offset)
   {
     TRACE("directory block (0x%x:0x%x) was not found\n", inode_block, inode_offset);
     return 0;
   }

  TRACE("inode block found at @0x%x\n", (int)start);

  /* A block can be composed of several directory header */
  bytes = 0;
  while (bytes < dir_size)
   {
     struct squashfs_dir_header *dir_header;

     dir_header = (struct squashfs_dir_header *)
        		(FILE_DATA+inode_offset+bytes);
     res = directory_lookup_1(dir_header,
			      entryname,
			      result_inode_block,
			      result_inode_offset);
     if (res == 0)
       return 1;
     bytes += res;
   }
  TRACE("entry %s not found in current directory\n", entryname);
  return 0;
}

/*
 * Search in this inode for entry named @entryname. If the entry was found
 * @result_inode_block and @result_inode_offset is filled.
 *
 * INODE_DATA is modified
 *
 * If the inode is not a directory, then return 0.
 *
 * @param inode_block: inode block location of the directory
 * @param inode_offset: inode offset location of the direcotry
 * @param entryname: entry to find (ended by a nul character)
 * @param result_inode_block: if the entry is present, return in this variable,
 *                            the inode_block number in the inode_table
 * @param result_inode_offset: if the entry is present, return in this variable,
 *                            the offset position in the block_inode_table.
 *
 * @return 0 the entry was not found or an error occured
 *         1 the entry is found, and @result_inode_block, @result_inode_offset is filled
 *
 */
static int squashfs_lookup_directory(int inode_block,
    				     int inode_offset,
    				     const char *entryname,
				     int *result_inode_block,
				     int *result_inode_offset)
{
  int dir_start_block, dir_offset, dir_size;
  unsigned int entry_start_block = 0, entry_offset = 0;

  TRACE("Lookup in inode %d:%d for %s\n", inode_block, inode_offset, entryname);

  if (! inode_read(inode_block, inode_offset))
    return 0;

  inode_print(INODE_DATA);

  /* We only support type dir */
  switch (INODE_DATA->base.inode_type)
   {
    case SQUASHFS_DIR_TYPE:
      dir_start_block = INODE_DATA->dir.start_block;
      dir_offset = INODE_DATA->dir.offset;
      dir_size = INODE_DATA->dir.file_size - 3;
      break;

    case SQUASHFS_LDIR_TYPE:
      dir_start_block = INODE_DATA->ldir.start_block;
      dir_offset = INODE_DATA->ldir.offset;
      dir_size = INODE_DATA->ldir.file_size - 3;
      break;

    default:
      TRACE("This inode is not a directory\n");
      errnum = ERR_BAD_FILETYPE;
      return 0;
   }

  if (dir_size > SQUASHFS_METADATA_SIZE)
   {
     TRACE("Dir size is too large for our algorithm 0x23I29\n");
     return 0;
   }

  /* Get the current directory header */
  if (! directory_lookup(dir_start_block, dir_offset, dir_size, entryname, &entry_start_block, &entry_offset))
   {
     errnum = ERR_FILE_NOT_FOUND;
     return 0;
   }

  TRACE("Found %s located at %x:%x\n", entryname, entry_start_block, entry_offset);

  *result_inode_block = entry_start_block;
  *result_inode_offset = entry_offset;

  return 1;
}


/*
 * Read the given inode (inode_block:inode_offset) and write the inode data
 * into INODE_DATA.
 *
 * Description of the Squashfs inode table
 *
 * r---------- @inode_table_start
 * |
 * |
 * |
 * |
 * -> .-----------------.                   array of squashfs_inode_entry
 *    | inode block 1   |                    .---------------------------.
 *    |_________________|                   /| @0                        |
 *    | inode block 2   |                  / |---------------------------|
 *    |_________________|                 /  |                           |
 *    |                 |                /   |                           |
 *    |                 |               /    |---------------------------|
 *    |                 |@inode_block  /     | @inode_offset             |
 *    |_________________|/____________/      |---------------------------|
 *    |                 |                    |                           |
 *    |                 |                    |                           |
 *    |_________________|_____________       |                           |
 *    |                 |             \      |                           |
 *    |                 |              \     |                           |
 *    |                 |               \----|___________________________|
 *    |_________________|
 *
 * an inode block is compressed, so the size length of the block is not known
 * in advance, but an inode block always contains SQUASHFS_METADATA_SIZE length
 * bytes.
 *
 * So we need to uncompressed all inode block to known the offset of the next
 * block.
 *
 * Each inode doesn't have the same size, so we can't known in advance (without
 * looking the type of the inode) which data to copy. So we copy all data until
 * the end of the block.
 *
 */
static int inode_read(unsigned int inode_block, unsigned int inode_offset)
{
  int offset = 0, res, compressed_size;
  long long start = SUPERBLOCK->inode_table_start;
  long long end = SUPERBLOCK->directory_table_start;

  TRACE("start=0x%x  end=0x%x (len=%d/0x%x)   inode_wanted:%d:%d (0x%x:0x%x)\n",
        (int)start, (int)end, (int)(end-start), (int)(end-start),
	inode_block, inode_offset, inode_block, inode_offset);

  while (start < end)
   {
     TRACE("reading block 0x%x (offset=0x%x  inode=0x%x:0x%x)\n", (int)start, offset, inode_block, inode_offset);
     res = read_block(start, &compressed_size, INODE_DATA);
     if (res == 0)
      {
	TRACE("uncompress_directory_table: failed to read block\n");
	return 0;
      }

     if (inode_block == offset)
      {
	TRACE("Inode %d found @0x%x\n", (int)start);
	if (inode_offset)
	  memmove(INODE_DATA, (unsigned char *)INODE_DATA+inode_offset, SQUASHFS_METADATA_SIZE-inode_offset);
	return 1;
      }

     start += compressed_size;
     offset += compressed_size;
   }
  TRACE("Inode %d not found\n");
  return 0;
}

/*
 * Return the data block for the current @fragment_index.
 *
 * This function read each time the fragment_table so this can be slow to read
 * all fragment_table in severall calls.
 *
 * @param fragment_index: the fragment data block.
 * @param fragment_data: where to ouput the data. Need to be SQUASHFS_FILE_MAX_SIZE long.
 *
 * @return 0 if an error occured, or the fragment_block was not found
 *        >0 the size of the fragment_data block
 *
 *
 * Description of the Squashfs fragments table
 *
 * r---------- @fragment_table_start
 * |
 * |
 * |
 * |
 * -> .-----------.     fragment_table(xxx)
 *    |           |   -> .-------------.
 *    |___________|__/   |_____________|
 * -> | (xxx/yyy) | /    |             |       fragment_data(nnn)
 * |  |___________|/     |_____________|       .-----------------.
 * |  |           |   -->|   (nnn)      ====>  |                 |
 * |  |           |   |  |-------------|       |                 |
 * |  |           |   |  |             |       |                 |
 * |  |           |   |  |             |       |                 |
 * |  |___________|   |  |_____________|       |                 |
 * |                  |                        |-----------------|
 * |                  |
 * fragment_index     @offset: yyy
 *
 *
 * fragment_table(xxx) is compressed and contains severall @squashfs_fragment_entry.
 * fragment_data(nnn) can be compressed and is shared between severall files.
 *
 */
static int fragment_read(unsigned int fragment_index, void *fragment_data)
{
  int offset, i, indexes;
  long long fragment_address;
  struct squashfs_fragment_entry *fragment_entry;
  unsigned char *fragments_table = FILE_DATA;

  indexes = SQUASHFS_FRAGMENT_INDEXES(SUPERBLOCK->fragments);

  TRACE("Reading fragment %d/%d  (%d fragments table that start @0x%x)\n",
      INODE_DATA->reg.fragment, SUPERBLOCK->fragments,
      indexes,
      (int)SUPERBLOCK->fragment_table_start);

  for (i=0; i<indexes; i++)
   {
     long long current_fragment_location;
     int length;

     fragment_address = SUPERBLOCK->fragment_table_start + i*sizeof(long long);
     read_bytes(fragment_address, sizeof(long long), &current_fragment_location);

     TRACE("Block fragment %d is located at @%x\n", i, (int)current_fragment_location);

     length = read_block(current_fragment_location, NULL, fragments_table);
     if (length == 0)
       return 0;

     TRACE("Read fragment block %d, length=%d\n", i, length);

     if (SQUASHFS_FRAGMENT_INDEX(fragment_index) == i)
       break;
   }

  if (i == indexes)
   {
     TRACE("Fragment %d not found\n", fragment_index);
     return 0;
   }

  offset = SQUASHFS_FRAGMENT_INDEX_OFFSET(fragment_index);

  fragment_entry = (struct squashfs_fragment_entry *)(fragments_table + offset);
  TRACE("fragment %d: start_block=0x%x size=%d pending=%d\n",
        fragment_index, (int)fragment_entry->start_block,
	fragment_entry->size, fragment_entry->pending);


  i = read_data_block(fragment_entry->start_block, fragment_entry->size, fragment_data);
  if (i == 0)
   {
     TRACE("failed to read fragment %d\n", fragment_index);
     return i;
   }
  return i;
}

/*
 *
 * Read one block from a inode file.
 *
 * The block_number is position in the list block.
 * This function is not designed to be speedup, but accurate, and allocate the least memory.
 *
 * @arg file_inode: global inode header
 * @arg SUPERBLOCK: description of the superblock
 */
static int squashfs_read_file_one_block(int block_number)
{
  long long start;
  int fragment_size;
  int blocks;
  int i;

  if (INODE_DATA->reg.fragment == SQUASHFS_INVALID_FRAG)
   {
     fragment_size = 0;
     blocks = (INODE_DATA->reg.file_size + SUPERBLOCK->block_size - 1) >> SUPERBLOCK->block_log;
   }
  else
   {
     fragment_size = INODE_DATA->reg.file_size % SUPERBLOCK->block_size;
     blocks = INODE_DATA->reg.file_size >> SUPERBLOCK->block_log;
   }

  TRACE("block_number=%d  fragment_size=%d  blocks=%d\n", block_number, fragment_size, blocks);

  /* Only decompress block when the block_number is lesser than the total number of blocks */
  if (block_number < blocks)
   {
     /*
      * List of blocks to read is after the inode. When copying data, we have also
      * copied a part of this data
      */
     start = INODE_DATA->reg.start_block;
     for (i=0; i<blocks; i++)
      {
	int bytes;
	unsigned int *c_block_list = (unsigned int *)((struct squashfs_reg_inode_header *)INODE_DATA+1);

	if (i == block_number)
	 {
	   TRACE("Reading block %d\n", i);
	   bytes = read_data_block(start, c_block_list[i], FILE_DATA);
	   if (bytes == 0)
	    {
	      TRACE("failed to read data block at 0x%x\n", (int)start);
	      return 0;
	    }
	   TRACE("Data block:\n");
	   dump_memory(FILE_DATA, 48);
	   TRACE("read %d bytes\n", bytes);
	   return bytes;
	 }
	start += SQUASHFS_COMPRESSED_SIZE_BLOCK(c_block_list[i]);
      }
   }

  if (fragment_size)
   {
     int bytes;

     bytes = fragment_read(INODE_DATA->reg.fragment, FILE_DATA);
     if (bytes == 0)
       return 0;
     /* data begins at FILE_DATA+INODE_DATA->reg.offset */
     if (INODE_DATA->reg.offset)
       memmove(FILE_DATA, FILE_DATA+INODE_DATA->reg.offset, INODE_DATA->reg.file_size);
     TRACE("Data block:\n");
     dump_memory(FILE_DATA, 48);
     return INODE_DATA->reg.file_size;
   }

  return 0;
}


/*
 *
 *
 */
int
squashfs_mount (void)
{
  TRACE("squashfs_mount()\n");

  /* Check partition type for harddisk */
  if (((current_drive & 0x80) || (current_slice != 0))
      && current_slice != PC_SLICE_TYPE_EXT2FS
      && (! IS_PC_SLICE_TYPE_BSD_WITH_FS (current_slice, FS_EXT2FS)))
    return 0;

  /* Read bpb */
  if (! devread(SQUASHFS_START, 0, sizeof(struct squashfs_super_block), (char *) SUPERBLOCK))
    return 0;

  if (SUPERBLOCK->s_magic != SQUASHFS_MAGIC)
   {
     if (SUPERBLOCK->s_magic != SQUASHFS_MAGIC_SWAP)
       return 0;

     TRACE("Reading a different endian SQUASHFS filesystem\n");
     TRACE("Not supported\n");
     errnum = ERR_FSYS_MOUNT;
     return 0;
   }

  /* Check the MAJOR & MINOR versions */
  if (   SUPERBLOCK->s_major != SQUASHFS_MAJOR
      || SUPERBLOCK->s_minor > SQUASHFS_MINOR)
   {
     TRACE("Major/Minor mismatch, filesystem is (%d:%d)\n",
	 SUPERBLOCK->s_major, SUPERBLOCK->s_minor);
     printf("I only support Squashfs 3.0 filesystems!\n");
     errnum = ERR_FSYS_MOUNT;
     return 0;
   }

  if (SUPERBLOCK->block_size > SQUASHFS_FILE_MAX_SIZE)
   {
     TRACE("Bad squashfs partition, block size is greater than SQUASHFS_FILE_MAX_SIZE\n");
     errnum = ERR_FSYS_MOUNT;
     return 0;
   }

  TRACE("Found a SQUASHFS partition\n");
  TRACE("\tInodes are %scompressed\n", SQUASHFS_UNCOMPRESSED_INODES(SUPERBLOCK->flags) ? "un" : "");
  TRACE("\tData is %scompressed\n", SQUASHFS_UNCOMPRESSED_DATA(SUPERBLOCK->flags) ? "un" : "");
  TRACE("\tFragments are %scompressed\n", SQUASHFS_UNCOMPRESSED_FRAGMENTS(SUPERBLOCK->flags) ? "un" : "");
  TRACE("\tCheck data is %s present in the filesystem\n", SQUASHFS_CHECK_DATA(SUPERBLOCK->flags) ? "" : "not");
  TRACE("\tFragments are %s present in the filesystem\n", SQUASHFS_NO_FRAGMENTS(SUPERBLOCK->flags) ? "not" : "");
  TRACE("\tAlways_use_fragments option is %s specified\n", SQUASHFS_ALWAYS_FRAGMENTS(SUPERBLOCK->flags) ? "" : "not");
  TRACE("\tDuplicates are %s removed\n", SQUASHFS_DUPLICATES(SUPERBLOCK->flags) ? "" : "not");
  TRACE("\tFilesystem size %d Kbytes (%d Mbytes)\n", (unsigned long long)SUPERBLOCK->bytes_used >> 10, (unsigned long long)SUPERBLOCK->bytes_used >> 20);
  TRACE("\tBlock size %d\n", SUPERBLOCK->block_size);
  TRACE("\tNumber of fragments %d\n", SUPERBLOCK->fragments);
  TRACE("\tNumber of inodes %d\n", SUPERBLOCK->inodes);
  TRACE("\tNumber of uids %d\n", SUPERBLOCK->no_uids);
  TRACE("\tNumber of gids %d\n", SUPERBLOCK->no_guids);
  TRACE("SUPERBLOCK->inode_table_start 0x%x\n", SUPERBLOCK->inode_table_start);
  TRACE("SUPERBLOCK->directory_table_start 0x%x\n", SUPERBLOCK->directory_table_start);
  TRACE("SUPERBLOCK->uid_start 0x%x\n", SUPERBLOCK->uid_start);
  TRACE("SUPERBLOCK->fragment_table_start 0x%x\n\n", SUPERBLOCK->fragment_table_start);

  return 1;
}

/*
 *
 *
 */
int
squashfs_dir (char *dirname)
{
  char *filename;
  int d_inode_start_block;
  int d_inode_offset;
  int res;
  int found_last_part = 0;

  TRACE("squashfs_dir(%s)\n", dirname);

  d_inode_start_block = SQUASHFS_INODE_BLK(SUPERBLOCK->root_inode);
  d_inode_offset = SQUASHFS_INODE_OFFSET(SUPERBLOCK->root_inode);

  while (found_last_part == 0)
   {
     /* Skip all concatened / */
     while (*dirname == '/')
       dirname++;

     /* Keep only the filename */
     filename = dirname;
     while (*dirname && *dirname != '/' && *dirname != ' ')
       dirname++;

     /*
      * No more / ? so this is the last part of the path, leave the while at the
      * end. Grub give use the full command line kernel /xxxx toto=azeaze ....
      * So stop after the first space too.
      */
     if (*dirname == 0 || *dirname == ' ')
      {
	*dirname = 0;
	found_last_part = 1;
      }
     else
      {
	/* filename point to the current entry, make then terminated by \0 */
	*dirname++ = 0;
      }

     res = squashfs_lookup_directory(d_inode_start_block, d_inode_offset,
				     filename,
				     &d_inode_start_block, &d_inode_offset);
     if (res == 0)
      {
	TRACE("Path %s component not found\n", filename);
	return 0;
      }
   }

  TRACE("Nearly finish we just look for %s\n", filename);
  TRACE("inode for %s is %d:%d\n", filename, d_inode_start_block, d_inode_offset);

  if (! inode_read(d_inode_start_block, d_inode_offset))
    return 0;

  inode_print(INODE_DATA);

  switch (INODE_DATA->base.inode_type)
   {
    case SQUASHFS_FILE_TYPE:
      filemax = INODE_DATA->reg.file_size;
      break;

    case SQUASHFS_LREG_TYPE:
      filemax = INODE_DATA->lreg.file_size;
      break;

    default:
      errnum = ERR_BAD_FILETYPE;
      return 0;
   }

  filepos = 0;
  squashfs_old_block = -1;

  TRACE("Size of %s is %d\n", filename, filemax);
  return 1;
}


/*
 *
 *
 */
int
squashfs_read(char *buf, int len)
{
  int bytes, size, block, ret, offset;

  TRACE("buf=0x%x, len=%d, position=%d\n", (unsigned long)buf, len, filepos);

  bytes = 0;
  while (len > 0)
   {
     /* Calculate the block number to read for the current position */
     block = filepos >> SUPERBLOCK->block_log;
     offset = filepos % SUPERBLOCK->block_size;

     if (block != squashfs_old_block)
      {
	ret = squashfs_read_file_one_block(block);
	if (ret == 0)
	  return 0;
	squashfs_old_block = block;
      }

     size = SUPERBLOCK->block_size - offset;
     if (size > len)
       size = len;

     TRACE("Copying into buffer at @0x%x, %d bytes, position=%d (offset=%d)\n", (unsigned long) buf, size, filepos, offset);
     memmove(buf, FILE_DATA+offset, size);
     dump_memory(buf, size);

     filepos += size;
     len -= size;
     bytes += size;
     buf += size;
   }

  return bytes;
}



/*
 *
 *
 *
 *
 *
 */

#ifdef SQUASHFS_TRACE

static int __isalnum(int c)
{
  if (c>=0x20 && c<=0x7e)
    return 1;
  else
    return 0;
}

static char tohex(char c)
{
  return c>=10?c-10+'a':c+'0';
}

static unsigned int fmt_xlong(char *dest, unsigned long i, int precision)
{
  register unsigned long len,tmp;
  /* first count the number of bytes needed */
  for (len=1, tmp=i; tmp>15; ++len)
    tmp>>=4;

  if (precision)
   {
     int x = 0;
     for (x=0; x<(precision-len); x++)
       *dest++='0';
   }

  tmp = i;
  dest+=len;
  *dest = 0;
  while (1)
   {
     *--dest = tohex(tmp&15);
     if (!(tmp>>=4)) break;
   }
  return len;
}

static void print_fmt_xlong(int i, int precision)
{
  char temp[48];
  fmt_xlong(temp, i, precision);
  printf("%s", temp);
}

static void dump_memory(const void *data, int len)
{
  int i, address, count;
  const unsigned char *buffer = data;

  return;
  address = 0;
  while (address < len )
   {
     print_fmt_xlong(address, 8);
     for(count=i=0; address+i < len && i<16; count++,i++)
      {
	printf(" ");
	print_fmt_xlong(buffer[address+i], 2);
      }
     for(;count<=16;count++)
       printf("   ");
     for(i=0; address < len && i<16; i++,address++)
       printf("%c", __isalnum(buffer[address]) ? buffer[address] : '.');
     printf("\n");
   }
}

static const char *get_type(int type)
{
  switch (type)
   {
    case SQUASHFS_DIR_TYPE:
      return "directory";
    case SQUASHFS_FILE_TYPE:
      return "file";
    case SQUASHFS_SYMLINK_TYPE:
      return "symlink";
    case SQUASHFS_BLKDEV_TYPE:
      return "block device";
    case SQUASHFS_CHRDEV_TYPE:
      return "char device";
    case SQUASHFS_FIFO_TYPE:
      return "fifo";
    case SQUASHFS_SOCKET_TYPE:
      return "socket";
    case SQUASHFS_LDIR_TYPE:
      return "ldir";
    case SQUASHFS_LREG_TYPE:
      return "lreg";
    default:
      return "unknown";
   }
}

static void print_inode_directory(struct squashfs_dir_inode_header *inode)
{
  TRACE("inode DIR: inode_number=%d nlink=%d file_size=%d start_block=%d\n",
        inode->inode_number,
	inode->nlink,
	inode->file_size,
	inode->start_block);

  dump_memory(inode, sizeof(struct squashfs_dir_inode_header));
}

static void print_inode_file(struct squashfs_reg_inode_header *inode)
{
  TRACE("inode FILE: inode_number=%d mode=%d uid=%d gid=%d file_size=%d ",
        inode->inode_number,
	inode->mode,
	inode->uid,
	inode->guid,
	inode->file_size
      );

  if (inode->fragment == SQUASHFS_INVALID_FRAG)
   {
     TRACE("fragment_bytes=0 location=%d:%d blocks=%d\n",
	   inode->start_block, inode->offset,
	   (inode->file_size + SUPERBLOCK->block_size - 1) >> SUPERBLOCK->block_log);
   }
  else
   {
     TRACE("fragment_bytes=%d location=%d:%d blocks=%d\n",
	   inode->file_size % SUPERBLOCK->block_size,
	   (int)inode->start_block, inode->offset,
	   inode->file_size >> SUPERBLOCK->block_log);
   }
}



static void inode_print(union squashfs_inode_header *inode)
{
  switch (inode->base.inode_type)
   {
    case SQUASHFS_DIR_TYPE:
      print_inode_directory(&inode->dir);
      break;
    case SQUASHFS_FILE_TYPE:
      print_inode_file(&inode->reg);
    default:
      TRACE("inode %s\n", get_type(inode->base.inode_type));
      break;
   }
}

#else

static void inode_print(union squashfs_inode_header *inode)
{
}

static void dump_memory(const void *data, int len)
{
}

#endif

