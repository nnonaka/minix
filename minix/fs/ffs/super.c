/* This file manages the in-core super block.
 *
 * The entry points into this file are:
 *   get_super:        return the (single) in-core super block for a device
 *   get_block_size:   return the cache block size (the fragment size)
 *   read_super:       read and validate the on-disk UFS2 super block
 */

#include "fs.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <minix/com.h>
#include <minix/u64.h>
#include <minix/bdev.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <sys/mman.h>
#include "buf.h"
#include "inode.h"
#include "super.h"

/* Read 'size' bytes from 'dev' at byte offset 'pos' into 'buf', in PAGE_SIZE
 * chunks (the block driver requires contiguous, page-granular transfers).
 * Returns OK or an error code.
 */
static int read_chunked(dev_t dev, u64_t pos, char *buf, size_t size)
{
  size_t off, chunk;
  int r;

  for (off = 0; off < size; off += chunk) {
	chunk = size - off;
	if (chunk > PAGE_SIZE)
		chunk = PAGE_SIZE;

	r = bdev_read(dev, pos + off, buf + off, chunk, BDEV_NOFLAGS);
	if (r != (ssize_t)chunk)
		return(EINVAL);
  }
  return(OK);
}

/* Counterpart of read_chunked() for writing. */
static int write_chunked(dev_t dev, u64_t pos, char *buf, size_t size)
{
  size_t off, chunk;
  int r;

  for (off = 0; off < size; off += chunk) {
	chunk = size - off;
	if (chunk > PAGE_SIZE)
		chunk = PAGE_SIZE;

	r = bdev_write(dev, pos + off, buf + off, chunk, BDEV_NOFLAGS);
	if (r != (ssize_t)chunk)
		return(EINVAL);
  }
  return(OK);
}

/*===========================================================================*
 *                              get_super                                    *
 *===========================================================================*/
struct super_block *get_super(dev_t dev)
{
  if (dev == NO_DEV)
	panic("request for super_block of NO_DEV");
  if (superblock->s_dev != dev)
	panic("wrong superblock: 0x%llx", (unsigned long long) dev);

  return(superblock);
}

/*===========================================================================*
 *                              get_block_size                               *
 *===========================================================================*/
unsigned int get_block_size(dev_t dev)
{
  if (dev == NO_DEV)
	panic("request for block size of NO_DEV");
  return(lmfs_fs_block_size());
}

/*===========================================================================*
 *                              read_super                                   *
 *===========================================================================*/
int read_super(struct super_block *sp)
{
/* Read the on-disk UFS2 super block at SBLOCK_UFS2 and the cylinder-group
 * summary area, and fill in the in-core super block.
 */
  static const off_t sblocksearch[] = SBLOCKSEARCH;
  struct fs *fs;
  char *sbbuf;
  size_t cssize;
  dev_t dev;
  int i, r, found, saw_ufs1;

  sp->s_csp = NULL;		/* so free_super() is safe on early failure */
  sp->s_csp_size = 0;
  sp->s_sboff = -1;

  dev = sp->s_dev;
  if (dev == NO_DEV)
	panic("request for super_block of NO_DEV");

  /* Read the standard superblock into a page-aligned bounce buffer. */
  sbbuf = mmap(NULL, SBLOCKSIZE, PROT_READ|PROT_WRITE,
	MAP_ANON|MAP_PRIVATE, -1, 0);
  if (sbbuf == MAP_FAILED)
	panic("can't allocate buffer for super block");

  fs = &sp->s_fs;

  /* Search the candidate superblock locations.  Accept native-little-endian
   * UFS2 in either the plain or the extended-attribute (EA) flavour; both use
   * the same on-disk inode and block layout. */
  found = FALSE;
  saw_ufs1 = FALSE;
  for (i = 0; sblocksearch[i] != -1; i++) {
	r = read_chunked(dev, (u64_t) sblocksearch[i], sbbuf, SBLOCKSIZE);
	if (r != OK)
		continue;
	memcpy(fs, sbbuf, sizeof(struct fs));
	if (fs->fs_magic == FS_UFS1_MAGIC)
		saw_ufs1 = TRUE;	/* note it for an accurate message */
	if (fs->fs_magic == FS_UFS2_MAGIC || fs->fs_magic == FS_UFS2EA_MAGIC) {
		/* Guard against picking up a stale/aliased superblock. */
		if (fs->fs_sblockloc == sblocksearch[i]) {
			sp->s_sboff = sblocksearch[i];
			found = TRUE;
			break;
		}
	}
  }
  munmap(sbbuf, SBLOCKSIZE);

  if (!found) {
	if (saw_ufs1)
		printf("ffs: UFS1 (FFSv1) is not supported; "
		    "create the filesystem with newfs -O2 (UFS2)\n");
	else
		printf("ffs: no supported UFS2 superblock found\n");
	return(EINVAL);
  }

  /* Sanity-check the geometry fields newfs computed for us. */
  if (fs->fs_bsize < MINBSIZE || fs->fs_bsize > MAXBSIZE ||
      (fs->fs_bsize & (fs->fs_bsize - 1)) != 0) {
	printf("ffs: invalid block size %d\n", fs->fs_bsize);
	return(EINVAL);
  }
  if (fs->fs_fsize < DEV_BSIZE || fs->fs_fsize > fs->fs_bsize ||
      (fs->fs_fsize & (fs->fs_fsize - 1)) != 0) {
	printf("ffs: invalid frag size %d\n", fs->fs_fsize);
	return(EINVAL);
  }
  if (fs->fs_bsize / fs->fs_fsize != fs->fs_frag || fs->fs_frag > MAXFRAG) {
	printf("ffs: inconsistent frag count\n");
	return(EINVAL);
  }
  if (fs->fs_inopb != (u_int32_t)(fs->fs_bsize / (int)DINODE2_SIZE)) {
	printf("ffs: inconsistent inopb\n");
	return(EINVAL);
  }
  if (fs->fs_ncg < 1 || fs->fs_ipg < 1 || fs->fs_cssize < 1) {
	printf("ffs: degenerate super block\n");
	return(EINVAL);
  }

  /* The cache operates in fragment-sized blocks (see CLAUDE.md). */
  sp->s_block_size = (unsigned int) fs->fs_fsize;

  /* Limit the maximum file size to what an off_t can represent. */
  if (fs->fs_maxfilesize > (u_int64_t) INT64_MAX)
	sp->s_max_size = INT64_MAX;
  else
	sp->s_max_size = (off_t) fs->fs_maxfilesize;

  /* Read the cylinder-group summary array (used by the allocator and for
   * accurate free-space accounting).
   */
  cssize = (size_t) ffs_fragroundup(fs, (off_t) fs->fs_cssize);
  sp->s_csp = mmap(NULL, cssize, PROT_READ|PROT_WRITE,
	MAP_ANON|MAP_PRIVATE, -1, 0);
  if (sp->s_csp == MAP_FAILED)
	panic("can't allocate cylinder-group summary buffer");

  sp->s_csp_size = cssize;
  r = read_chunked(dev, (u64_t) fs->fs_csaddr * fs->fs_fsize,
	(char *) sp->s_csp, cssize);
  if (r != OK) {
	munmap(sp->s_csp, cssize);
	sp->s_csp = NULL;
	sp->s_csp_size = 0;
	printf("ffs: cannot read cylinder-group summary\n");
	return(r);
  }
  fs->fs_csp = sp->s_csp;

  return(OK);
}

/*===========================================================================*
 *                              free_super                                   *
 *===========================================================================*/
void free_super(struct super_block *sp)
{
/* Release the in-core super block and its cylinder-group summary mapping. */
  if (sp == NULL)
	return;
  if (sp->s_csp != NULL && sp->s_csp_size > 0) {
	munmap(sp->s_csp, sp->s_csp_size);
	sp->s_csp = NULL;
	sp->s_csp_size = 0;
  }
  free(sp);
}

/*===========================================================================*
 *                              write_super                                  *
 *===========================================================================*/
void write_super(struct super_block *sp)
{
/* Write the in-core superblock and the cylinder-group summary array back to
 * disk.  The in-core-only pointer fields are zeroed in the on-disk copy. */
  struct fs *fs = &sp->s_fs;
  char *buf;
  size_t wsize, cssize;
  int r;

  if (sp->s_rd_only)
	return;

  /* Write the superblock from a sector-rounded bounce buffer. */
  wsize = (size_t) roundup(sizeof(struct fs), DEV_BSIZE);
  buf = mmap(NULL, wsize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
  if (buf == MAP_FAILED)
	panic("ffs: cannot allocate superblock write buffer");
  memset(buf, 0, wsize);
  memcpy(buf, fs, sizeof(struct fs));
  {
	struct fs *d = (struct fs *) buf;	/* clear in-core-only pointers */
	memset(d->fs_ocsp, 0, sizeof(d->fs_ocsp));
	d->fs_contigdirs = NULL;
	d->fs_csp = NULL;
	d->fs_maxcluster = NULL;
	d->fs_active = NULL;
	d->fs_fmod = 0;
  }
  r = write_chunked(sp->s_dev, (u64_t) sp->s_sboff, buf, wsize);
  munmap(buf, wsize);
  if (r != OK) {
	printf("ffs: failed to write superblock\n");
	return;
  }

  /* Write the cylinder-group summary array. */
  cssize = (size_t) ffs_fragroundup(fs, (off_t) fs->fs_cssize);
  r = write_chunked(sp->s_dev, (u64_t) fs->fs_csaddr * fs->fs_fsize,
	(char *) sp->s_csp, cssize);
  if (r != OK)
	printf("ffs: failed to write cylinder-group summary\n");

  fs->fs_fmod = 0;
}
