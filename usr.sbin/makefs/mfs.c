/* mkfs  -  make the MINIX filesystem	Authors: Tanenbaum et al. */

/*	Authors: Andy Tanenbaum, Paul Ogilvie, Frans Meulenbroeks, Bruce Evans */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <util.h>
#include <assert.h>
#include "makefs.h"
#include "mfs.h"

#if defined(__minix)
#include <minix/minlib.h>
#include <minix/partition.h>
#include <sys/ioctl.h>
#endif

/* Definition of the file system layout: */
#include "mfs/const.h"
#include "mfs/type.h"
#include "mfs/mfsdir.h"
#include "mfs/super.h"



#define MAX_TOKENS          10
#define LINE_LEN           300



struct fs_size {
  ino_t inocount; /* amount of inodes */
  zone_t zonecount; /* amount of zones */
  block_t blockcount; /* amount of blocks */
};

block_t nrblocks;
unsigned int zone_per_block;
int zone_shift = 0;
zone_t next_zone, zoff, nr_indirzones;
unsigned int inodes_per_block, indir_per_block, indir_per_zone;
unsigned int zone_size;
ino_t nrinodes, inode_offset;
int print = 0;
uint64_t fs_offset_blocks, written_fs_size = 0;

char *zero;
unsigned char *umap_array;	/* bit map tells if block read yet */
size_t umap_array_elements;
block_t zone_map;		/* where is zone map? (depends on # inodes) */
#ifndef MFS_STATIC_BLOCK_SIZE
size_t block_size;
#else
#define block_size	MFS_STATIC_BLOCK_SIZE
#endif

static void mfs_validate(const char *dir, fsnode *root, fsinfo_t *fsopts);
static void mfs_test(fsinfo_t *fsopts);
static void mfs_dump_fsinfo(fsinfo_t *f);
static void mfs_size_dir(struct fs_size * fssize, fsnode *root, fsinfo_t *fsopts);
static ino_t mfs_build_inode(fsnode *cur, fsinfo_t *fsopts);
static int mfs_populate_dir(const char *dir, fsnode *root, fsinfo_t *fsopts);

void detect_fs_size(struct fs_size * fssize, fsnode *root, fsinfo_t *fsopts);
void sizeup_dir(struct fs_size * fssize);
//block_t sizeup(char *device, fsinfo_t *fsopts);
static int bitmapsize(bit_t nr_bits, size_t blk_size);
void super(zone_t zones, ino_t inodes, fsinfo_t *fsopts);
void rootdir(ino_t inode, fsnode *cur, fsinfo_t *fsopts);
int enter_symlink(ino_t inode, char *link, fsnode *cur, fsinfo_t *fsopts);
int dir_try_enter(zone_t z, ino_t child, char const *name, fsinfo_t *fsopts);
void eat_dir(ino_t parent, const char *dir, fsnode *cur, fsinfo_t *fsopts);
void eat_file(ino_t inode, const char *name, fsnode *cur, fsinfo_t *fsopts);
void enter_dir(ino_t parent, char const *name, ino_t child, fsinfo_t *fsopts);
void add_zone(ino_t n, zone_t z, size_t bytes, fsnode *cur, fsinfo_t *fsopts);
void incr_link(ino_t n, fsinfo_t *fsopts);
void incr_size(ino_t n, size_t count, fsinfo_t *fsopts);
static ino_t alloc_inode(int mode, int usrid, int grpid, fsinfo_t *fsopts);
static zone_t alloc_zone(fsinfo_t *fsopts);
void insert_bit(block_t block, bit_t bit, fsinfo_t *fsopts);
int mode_con(char *p);
void get_line(char line[LINE_LEN], char *parse[MAX_TOKENS]);
__dead void pexit(char const *s, ...) __printflike(1,2);
void *alloc_block(void);
void print_fs(fsinfo_t *fsopts);
int read_and_set(block_t n);
static int create_special(const char *image, fsinfo_t *fsopts);
void get_block(block_t n, void *buf, fsinfo_t *fsopts);
void get_super_block(void *buf, fsinfo_t *fsopts);
void put_block(block_t n, void *buf, fsinfo_t *fsopts);
static uint64_t mkfs_seek(uint64_t pos, int whence, fsinfo_t *fsopts);
static ssize_t mkfs_write(void * buf, size_t count, fsinfo_t *fsopts);


void
mfs_prep_opts(fsinfo_t *fsopts)
{
	mfs_opt_t *mfs_opts = ecalloc(1, sizeof(*mfs_opts));

	const option_t mfs_options[] = {
	    { 'b', "bsize", &mfs_opts->bsize, OPT_INT32,
	      1, INT_MAX, "block size" },
	    { 'v', "verbose ", &mfs_opts->verbose , OPT_BOOL,
	      0, 1, "verbose " },
	    { .name = NULL }
	};

	mfs_opts->bsize= -1;
	mfs_opts->verbose= 0;

	fsopts->fs_specific = mfs_opts;
	fsopts->fs_options = copy_opts(mfs_options);
}

void
mfs_cleanup_opts(fsinfo_t *fsopts)
{
	free(fsopts->fs_specific);
	free(fsopts->fs_options);
}

int
mfs_parse_opts(const char *option, fsinfo_t *fsopts)
{

	return set_option_var(fsopts->fs_options, option, "1", NULL, 0) != -1;
}

void
mfs_makefs(const char *image, const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	struct timeval	start;
	
  block_t blocks;
  ino_t inodes, root_inum;
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;

	assert(image != NULL);
	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);

	if (debug & DEBUG_FS_MAKEFS)
		printf("mfs_makefs: image %s directory %s root %p\n",
		    image, dir, root);

    mfs_opts->verbose = 2;

		/* validate tree and options */
	TIMER_START(start);
	  
	mfs_validate(dir, root, fsopts);

  	TIMER_RESULTS(start, "mfs_validate");

    inodes = fsopts->inodes;
    blocks = fsopts->size / block_size;

	/* XXX is it OK to write on stdout? Use warn() instead? Also consider using verbose */
	fprintf(stderr, "dynamically sized filesystem: %u blocks, %u inodes\n",
	    (unsigned int) blocks, (unsigned int) inodes);


  if (blocks < 5) errx(1, "Block count too small");
  if (inodes < 1) errx(1, "Inode count too small");

  nrblocks = blocks;
  nrinodes = inodes;

  umap_array_elements = 1 + blocks/8;
  if(!(umap_array = malloc(umap_array_elements)))
	err(1, "can't allocate block bitmap (%u bytes).",
		(unsigned)umap_array_elements);

	/* create image */
	TIMER_START(start);
	if (create_special(image, fsopts) == -1)
		errx(1, "Image file `%s' not created.", image);
	TIMER_RESULTS(start, "mfs_create_image");

  //if (!donttest)
  if (1)
    mfs_test(fsopts);

  /* Make the file-system */

  put_block(BOOT_BLOCK, zero, fsopts);		/* Write a null boot block. */
  put_block(BOOT_BLOCK+1, zero, fsopts);	/* Write another null block. */

  super(nrblocks >> zone_shift, inodes, fsopts);
  
  	fsopts->curinode = ROOT_INODE;

    root_inum = mfs_build_inode(root, fsopts);
    rootdir(root_inum, root, fsopts);
    
    print_fs(fsopts);

		/* populate image */
	printf("Populating `%s'\n", image);
	TIMER_START(start);
	if (! mfs_populate_dir(dir, root, fsopts))
		errx(1, "Image file `%s' not populated.", image);
	TIMER_RESULTS(start, "mfs_populate_dir");

  
  if (print) print_fs(fsopts);
  else if (mfs_opts->verbose > 1) {
	  if (zone_shift)
		fprintf(stderr, "%d inodes used.     %u zones (%u blocks) used.\n",
		    (int)fsopts->curinode, next_zone, next_zone*zone_per_block);
	  else
		fprintf(stderr, "%d inodes used.     %u zones used.\n",
		    (int)fsopts->curinode, next_zone);
  }


	if (close(fsopts->fd) == -1)
		err(1, "Closing `%s'", image);
	fsopts->fd = -1;
	printf("Image `%s' complete\n", image);
}


	/* end of public functions */

static void
mfs_validate(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
  //block_t blocks;
  ino_t inodes;
  off_t imgsize;
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;
  struct fs_size fssize;

#ifndef MFS_STATIC_BLOCK_SIZE
  		/* set mfs defaults */
	if (mfs_opts->bsize == -1)
		mfs_opts->bsize = DEFAULT_BLOCK_SIZE;
    block_size = mfs_opts->bsize;
#else
    block_size = DEFAULT_BLOCK_SIZE;
#endif
  zone_shift = 0;
  if (block_size % SECTOR_SIZE)
	errx(4, "block size must be multiple of sector (%d bytes)", SECTOR_SIZE);
#ifdef MIN_BLOCK_SIZE
  if (block_size < MIN_BLOCK_SIZE)
	errx(4, "block size must be at least %d bytes", MIN_BLOCK_SIZE);
#endif
#ifdef MAX_BLOCK_SIZE
  if (block_size > MAX_BLOCK_SIZE)
	errx(4, "block size must be at most %d bytes", MAX_BLOCK_SIZE);
#endif
  if(block_size%INODE_SIZE)
	errx(4, "block size must be a multiple of inode size (%d bytes)", INODE_SIZE);
		
  if(zone_shift < 0 || zone_shift > 14)
	errx(4, "zone_shift must be a small non-negative integer");
  zone_per_block = 1 << zone_shift;	/* nr of blocks per zone */

  inodes_per_block = INODES_PER_BLOCK(block_size);
  indir_per_block = INDIRECTS(block_size);
  indir_per_zone = INDIRECTS(block_size) << zone_shift;
  /* number of file zones we can address directly and with a single indirect*/
  nr_indirzones = NR_DZONES + indir_per_zone;
  zone_size = block_size << zone_shift;
  /* Checks for an overflow: only with very big block size */
  if (zone_size <= 0)
	errx(4, "Zones are too big for this program; smaller -B or -z, please!");

  /* now that the block size is known, do buffer allocations where
   * possible.
   */
  zero = alloc_block();

  fs_offset_blocks = roundup(fsopts->offset, block_size) / block_size;


	/* calculate size of tree */
	detect_fs_size(&fssize, root, fsopts);
	imgsize = fssize.blockcount * block_size;
	inodes = fssize.inocount;
		
	/* add requested slop */
	imgsize += fsopts->freeblocks;
	inodes += fsopts->freefiles;
	if (fsopts->freefilepc > 0)
		inodes = inodes * (100 + fsopts->freefilepc) / 100;
	if (fsopts->freeblockpc > 0)
		imgsize = imgsize * (100 + fsopts->freeblockpc) / 100;
		    
	if (imgsize < fsopts->minsize)	/* ensure meets minimum size */
		imgsize = fsopts->minsize;
		
	/* round up to fill inode block */
	inodes += inodes_per_block - 1;
	inodes = inodes / inodes_per_block * inodes_per_block;
    fsopts->inodes = inodes;

	/* round up to the next block */
	imgsize = roundup(imgsize, block_size);
	//blocks = imgsize / block_size;
	fsopts->size = imgsize;
		
	if (debug & DEBUG_FS_VALIDATE) {
		printf("ffs_validate: after defaults set:\n");
		mfs_dump_fsinfo(fsopts);
		printf("ffs_validate: dir %s; %lld bytes, %lld inodes\n",
		    dir, (long long)fsopts->size, (long long)fsopts->inodes);
	}
		/* now check calculated sizes vs requested sizes */
	if (fsopts->maxsize > 0 && fsopts->size > fsopts->maxsize) {
		errx(1, "`%s' size of %lld is larger than the maxsize of %lld.",
		    dir, (long long)fsopts->size, (long long)fsopts->maxsize);
	}

}

static void
mfs_test(fsinfo_t *fsopts)
{
    block_t blocks;
	uint16_t *testb;
	ssize_t w;
	size_t nread;
	
    blocks = fsopts->size / block_size;

	testb = alloc_block();

	/* Try writing the last block of partition or diskette. */
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET, fsopts);
	testb[0] = 0x3245;
	testb[1] = 0x11FF;
	testb[block_size/2-1] = 0x1F2F;
	w=mkfs_write(testb, block_size, fsopts);
	sync();			/* flush write, so if error next read fails */
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET, fsopts);
	testb[0] = 0;
	testb[1] = 0;
	testb[block_size/2-1] = 0;
	nread = read(fsopts->fd, testb, block_size);
	if (nread != block_size || testb[0] != 0x3245 || testb[1] != 0x11FF ||
	    testb[block_size/2-1] != 0x1F2F) {
		warn("nread = %zu\n", nread);
		warnx("testb = 0x%x 0x%x 0x%x\n",
		    testb[0], testb[1], testb[block_size-1]);
		errx(1, "File system is too big for minor device (read)");
	}
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET, fsopts);
	testb[0] = 0;
	testb[1] = 0;
	testb[block_size/2-1] = 0;
	mkfs_write(testb, block_size, fsopts);
	mkfs_seek(0L, SEEK_SET, fsopts);
	free(testb);
}

static void
mfs_dump_fsinfo(fsinfo_t *f)
{

	//mfs_opt_t	*fs = f->fs_specific;

	printf("fsopts at %p\n", f);

	printf("\tsize %lld, inodes %lld, curinode %u\n",
	    (long long)f->size, (long long)f->inodes, f->curinode);

	printf("\tminsize %lld, maxsize %lld\n",
	    (long long)f->minsize, (long long)f->maxsize);
	printf("\tfree files %lld, freefile %% %d\n",
	    (long long)f->freefiles, f->freefilepc);
	printf("\tfree blocks %lld, freeblock %% %d\n",
	    (long long)f->freeblocks, f->freeblockpc);

}

static void
mfs_size_dir(struct fs_size * fssize, fsnode *root, fsinfo_t *fsopts)
{
	fsnode *	node;
	//mfs_opt_t	*mfs_opts = fsopts->fs_specific;
  off_t size;
  int dir_entries = 2;
  zone_t dir_zones = 0, fzones, indirects;

	/* node may be NULL (empty directory) */
	assert(fsopts != NULL);
	//assert(mfs_opts != NULL);

	for (node = root; node != NULL; node = node->next) {
    	fssize->inocount++;
	    dir_entries++;
		if ((node->inode->flags & FI_SIZED) == 0) {
				/* don't count duplicate names */
			node->inode->flags |= FI_SIZED;
			if (node->type == S_IFREG) {
				//ADDSIZE(node->inode->st.st_size);
			    size = node->inode->st.st_size;
			fzones = roundup(size, zone_size) / zone_size;
			indirects = 0;
			/* XXX overflow? fzones is u32, size is potentially 64-bit */
			if (fzones > NR_DZONES)
				indirects++; /* single indirect needed */
			if (fzones > nr_indirzones) {
				/* Each further group of 'indir_per_zone'
				 * needs one supplementary indirect zone:
				 */
				indirects += roundup(fzones - nr_indirzones,
				    indir_per_zone) / indir_per_zone;
				indirects++;	/* + double indirect needed!*/
			}
			fssize->zonecount += fzones + indirects;
			} else if (node->type == S_IFLNK) {
			    /* Symlink contents is always stored a block */
        		fssize->zonecount++; 
			}
		}
		if (node->type == S_IFDIR)
			mfs_size_dir(fssize, node->child, fsopts);
	}

	dir_zones = (dir_entries / (NR_DIR_ENTRIES(block_size) * zone_per_block));
	if(dir_entries % (NR_DIR_ENTRIES(block_size) * zone_per_block))
		dir_zones++;
	if(dir_zones > NR_DZONES)
		dir_zones++;	/* Max single indir */
	fssize->zonecount += dir_zones;
		
	return;
}

static ino_t
mfs_build_inode(fsnode *cur, fsinfo_t *fsopts)
{
	ino_t in;
	
    in = alloc_inode(cur->inode->st.st_mode,
        cur->inode->st.st_uid, cur->inode->st.st_gid, fsopts);
        
	cur->inode->ino = in;
	cur->inode->flags |= FI_ALLOCATED;
    cur->inode->flags |= FI_WRITTEN;

    return in;
}

/*================================================================
 *	    eat_dir  -  recursively install directory
 *===============================================================*/
static int
mfs_populate_dir(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	fsnode		*cur;
	ino_t   	in, parent;
	//void		*membuf;
	char		path[MAXPATHLEN + 1];
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;
  int maj, min;
  size_t size;

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);
	assert(mfs_opts != NULL);


	if (debug & DEBUG_FS_POPULATE)
		printf("mfs_populate_dir: PASS 1  dir %s node %p\n", dir, root);

		/*
		 * pass 1: allocate inode numbers, build directory `file'
		 */
	for (cur = root; cur != NULL; cur = cur->next) {
		if ((cur->inode->flags & FI_ALLOCATED) == 0) {
			cur->inode->flags |= FI_ALLOCATED;
    		cur->inode->flags |= FI_WRITTEN;
			//if (cur == root && cur->parent != NULL) {
			//	in = cur->parent->inode->ino;
			//} else {
				/* build on-disk inode */
				in = mfs_build_inode(cur, fsopts);
				cur->inode->ino = in;
			//}
		}
		in = cur->inode->ino;
		if (cur->parent == NULL)
		    parent = ROOT_INODE;
		else
		    parent = cur->parent->inode->ino;
		    
		enter_dir(parent, cur->name, in, fsopts);
    	incr_size(parent, sizeof(struct direct), fsopts);
    	incr_link(in, fsopts);

		if (cur == root) {		/* we're at "."; add ".." */
    		enter_dir(in, "..", parent, fsopts);
        	incr_size(in, sizeof(struct direct), fsopts);
    		incr_link(parent, fsopts);  	/* count my parent's link */
		} else if (cur->child != NULL)
		    incr_link(in, fsopts);   /* count my child's link */

	}

		/*
		 * pass 2: write out dirbuf, then non-directories at this level
		 */
	if (debug & DEBUG_FS_POPULATE)
		printf("mfs_populate_dir: PASS 2  dir %s\n", dir);
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->inode->flags & FI_WRITTEN)
			continue;		/* skip hard-linked entries */
		cur->inode->flags |= FI_WRITTEN;
		
        in = cur->inode->ino;
        
		if ((size_t)snprintf(path, sizeof(path), "%s/%s/%s", cur->root,
		    cur->path, cur->name) >= sizeof(path))
			errx(1, "Pathname too long.");

		if (cur->child != NULL)
			continue;		/* child creates own inode */

		if (debug & DEBUG_FS_POPULATE_NODE) {
			printf("mfs_populate_dir: writing ino %d, %s",
			    cur->inode->ino, inode_type(cur->type));
			if (cur->inode->nlink > 1)
				printf(", nlink %d", cur->inode->nlink);
			putchar('\n');
		}
		if (S_ISBLK(cur->type) || S_ISCHR(cur->type)) {
    		/* Special file. */
    		maj = major(cur->inode->st.st_rdev);
    		min = minor(cur->inode->st.st_rdev);
    		size = cur->inode->st.st_size;
    		size = block_size * size;
    		add_zone(in, (zone_t) (makedev(maj,min)), size, cur, fsopts);
	    } else if (S_ISLNK(cur->type)) {
	    	/* symlink */
    		enter_symlink(in, cur->symlink, cur, fsopts);
		} else if (S_ISREG(cur->type)) {
		/* Regular file. Go read it. */
			eat_file(in, path, cur, fsopts);
		} 
	}

		/*
		 * pass 3: write out sub-directories
		 */
	if (debug & DEBUG_FS_POPULATE)
		printf("mfs_populate_dir: PASS 3  dir %s\n", dir);
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->child == NULL)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir,
		    cur->name) >= sizeof(path))
			errx(1, "Pathname too long.");
		if (! mfs_populate_dir(path, cur->child, fsopts))
			return (0);
	}

	if (debug & DEBUG_FS_POPULATE)
		printf("mfs_populate_dir: DONE dir %s\n", dir);

	return (1);

}
	

/*================================================================
 *        detect_fs_size  -  determine image size dynamically
 *===============================================================*/
void
detect_fs_size(struct fs_size * fssize, fsnode *root, fsinfo_t *fsopts)
{
  block_t initb;
  zone_t initz;  


  fssize->inocount = 1;	/* root directory node */
  fssize->zonecount = 0;
  fssize->blockcount = 0;

  mfs_size_dir(fssize, root, fsopts);

  initb = bitmapsize(1 + fssize->inocount, block_size);
  initb += bitmapsize(fssize->zonecount, block_size);
  initb += START_BLOCK;
  initb += (fssize->inocount + inodes_per_block - 1) / inodes_per_block;
  initz = (initb + zone_per_block - 1) >> zone_shift;

  fssize->blockcount = initb+ fssize->zonecount;
}

# if 0
/*================================================================
 *                    sizeup  -  determine device size
 *===============================================================*/
block_t
sizeup(char * device, fsinfo_t *fsopts)
{
  block_t d;
#if defined(__minix)
  uint64_t bytes, resize;
  uint32_t rem;
#else
  off_t size;
#endif


  if ((fsopts->fd = open(device, O_RDONLY)) == -1) {
	if (errno != ENOENT)
		perror("sizeup open");
	return 0;
  }

#if defined(__minix)
  if(minix_sizeup(device, &bytes) < 0) {
       perror("sizeup");
       return 0;
  }

  d = (uint32_t)(bytes / block_size);
  rem = (uint32_t)(bytes % block_size);

  resize = ((uint64_t)d * block_size) + rem;
  if(resize != bytes) {
	/* Assume block_t is unsigned */
	d = (block_t)(-1ul);
	fprintf(stderr, "%s: truncating FS at %lu blocks\n",
		"makefs", (unsigned long)d);
  }
#else
  size = mkfs_seek(0, SEEK_END, fsopts);
  /* Assume block_t is unsigned */
  if (size / block_size > (block_t)(-1ul)) {
	d = (block_t)(-1ul);
	fprintf(stderr, "%s: truncating FS at %lu blocks\n",
		"makefs", (unsigned long)d);
  } else
	d = size / block_size;
#endif

  return d;
}
#endif

/*
 * copied from fslib
 */
static int
bitmapsize(bit_t nr_bits, size_t blk_size)
{
  block_t nr_blocks;

  nr_blocks = nr_bits / FS_BITS_PER_BLOCK(blk_size);
  if (nr_blocks * FS_BITS_PER_BLOCK(blk_size) < nr_bits)
	++nr_blocks;
  return(nr_blocks);
}


/*================================================================
 *                 super  -  construct a superblock
 *===============================================================*/

void
super(zone_t zones, ino_t inodes, fsinfo_t *fsopts)
{
  block_t inodeblks, initblks, i;
  unsigned long nb;
  long long ind_per_zone, zo;
  void *buf;
  struct super_block *sup;
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;

  sup = buf = alloc_block();

#ifdef MFSFLAG_CLEAN
  /* The assumption is that mkfs will create a clean FS. */
  sup->s_flags = MFSFLAG_CLEAN;
#endif

  sup->s_ninodes = inodes;
  sup->s_nzones = 0;	/* not used in V2 - 0 forces errors early */
  sup->s_zones = zones;
  
#ifndef MFS_STATIC_BLOCK_SIZE
#define BIGGERBLOCKS "Please try a larger block size for an FS of this size."
#else
#define BIGGERBLOCKS "Please use MinixFS V3 for an FS of this size."
#endif
  sup->s_imap_blocks = nb = bitmapsize(1 + inodes, block_size);
  /* Checks for an overflow: nb is uint32_t while s_imap_blocks is of type
   * int16_t */
  if((unsigned long)sup->s_imap_blocks != nb) {
	errx(1, "too many inode bitmap blocks.\n" BIGGERBLOCKS);
  }
  sup->s_zmap_blocks = nb = bitmapsize(zones, block_size);
  /* Idem here check for overflow */
  if(nb != (unsigned long)sup->s_zmap_blocks) {
	errx(1, "too many block bitmap blocks.\n" BIGGERBLOCKS);
  }
  inode_offset = START_BLOCK + sup->s_imap_blocks + sup->s_zmap_blocks;
  inodeblks = (inodes + inodes_per_block - 1) / inodes_per_block;
  initblks = inode_offset + inodeblks;
  sup->s_firstdatazone_old = nb =
	(initblks + (1 << zone_shift) - 1) >> zone_shift;
  if(nb >= zones) errx(1, "bit maps too large");
  if(nb != sup->s_firstdatazone_old) {
	/* The field is too small to store the value. Fortunately, the value
	 * can be computed from other fields. We set the on-disk field to zero
	 * to indicate that it must not be used. Eventually, we can always set
	 * the on-disk field to zero, and stop using it.
	 */
	sup->s_firstdatazone_old = 0;
  }
  sup->s_firstdatazone = nb;
  zoff = sup->s_firstdatazone - 1;
  sup->s_log_zone_size = zone_shift;
  sup->s_magic = SUPER_MAGIC;
#ifdef MFS_SUPER_BLOCK_SIZE
  sup->s_block_size = block_size;
  /* Check for overflow */
  if(block_size != sup->MFS_SUPER_BLOCK_SIZE)
	errx(1, "block_size too large.");
  sup->s_disk_version = 0;
#endif

  ind_per_zone = (long long) indir_per_zone;
  zo = NR_DZONES + ind_per_zone + ind_per_zone*ind_per_zone;
#ifndef MAX_MAX_SIZE
#define MAX_MAX_SIZE 	(INT32_MAX)
#endif
  if(MAX_MAX_SIZE/block_size < zo) {
	sup->s_max_size = MAX_MAX_SIZE;
  }
  else {
	sup->s_max_size = zo * block_size;
  }

  if (mfs_opts->verbose>1) {
	fprintf(stderr, "Super block values:\n"
	    "\tnumber of inodes\t%12d\n"
	    "\tnumber of zones \t%12d\n"
	    "\tinode bit map blocks\t%12d\n"
	    "\tzone bit map blocks\t%12d\n"
	    "\tfirst data zone \t%12d\n"
	    "\tblocks per zone shift\t%12d\n"
	    "\tmaximum file size\t%12d\n"
	    "\tmagic number\t\t%#12X\n",
	    sup->s_ninodes, sup->s_zones,
	    sup->s_imap_blocks, sup->s_zmap_blocks, sup->s_firstdatazone,
	    sup->s_log_zone_size, sup->s_max_size, sup->s_magic);
#ifdef MFS_SUPER_BLOCK_SIZE
	fprintf(stderr, "\tblock size\t\t%12d\n", sup->s_block_size);
#endif
  }

  mkfs_seek((off_t) SUPER_BLOCK_BYTES, SEEK_SET, fsopts);
  mkfs_write(buf, SUPER_BLOCK_BYTES, fsopts);
  
  /* Clear maps and inodes. */
  for (i = START_BLOCK; i < initblks; i++) put_block((block_t) i, zero, fsopts);

  next_zone = sup->s_firstdatazone;
  //next_inode = 1;

  zone_map = INODE_MAP + sup->s_imap_blocks;

  insert_bit(zone_map, 0, fsopts);	/* bit zero must always be allocated */
  insert_bit((block_t) INODE_MAP, 0, fsopts);	/* inode zero not used but
					 * must be allocated */

  free(buf);
}


/*================================================================
 *              rootdir  -  install the root directory
 *===============================================================*/
void
rootdir(ino_t inode, fsnode *cur, fsinfo_t *fsopts)
{
  zone_t z;

  z = alloc_zone(fsopts);
  add_zone(inode, z, 2 * sizeof(struct direct), cur, fsopts);
  enter_dir(inode, ".", inode, fsopts);
  enter_dir(inode, "..", inode, fsopts);
  incr_link(inode, fsopts);
  incr_link(inode, fsopts);
}

int
enter_symlink(ino_t inode, char *lnk, fsnode *cur, fsinfo_t *fsopts)
{
  zone_t z;
  size_t len;
  char *buf;

  buf = alloc_block();
  z = alloc_zone(fsopts);
  len = strlen(lnk);
  if (len >= block_size) {
	pexit("symlink too long, max length is %u", (unsigned)block_size - 1);
	return -1;
  }
  strcpy(buf, lnk);
  put_block((z << zone_shift), buf, fsopts);

  add_zone(inode, z, len, cur, fsopts);

  free(buf);
  return 0;
}




/*================================================================
 * 		eat_file  -  copy file to MINIX
 *===============================================================*/
/* Zonesize >= blocksize */
void
eat_file(ino_t inode, const char *name, fsnode *cur, fsinfo_t *fsopts)
{
    size_t ct = 0;
  size_t i, j;
  zone_t z = 0;
  char *buf;
  //time_t timeval;
  int f;
  
		if ((f = open(name, O_RDONLY, 0444)) == -1) {
			warn("Can't open `%s' for reading", name);
			return;
		}

  buf = alloc_block();

  do {
	for (i = 0, j = 0; i < (size_t)zone_per_block; i++, j += ct) {
		memset(buf, 0, block_size);
		if ((ct = read(f, buf, block_size)) > 0) {
			if (i == 0) z = alloc_zone(fsopts);
			put_block((z << zone_shift) + i, buf, fsopts);
		}
	}
	if (ct) add_zone(inode, z, (size_t) j, cur, fsopts);
  } while (ct == block_size);
  close(f);
  free(buf);
}

int
dir_try_enter(zone_t z, ino_t child, char const *name, fsinfo_t *fsopts)
{
  struct direct *dir_entry = alloc_block();
  int r = 0;
  block_t b;
  uint i, l;
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;

  b = z << zone_shift;
  for (l = 0; l < zone_per_block; l++, b++) {
	get_block(b, dir_entry, fsopts);

	for (i = 0; i < NR_DIR_ENTRIES(block_size); i++)
		if (!dir_entry[i].d_ino)
			break;

	if(i < NR_DIR_ENTRIES(block_size)) {
		r = 1;
		dir_entry[i].d_ino = child;
		assert(sizeof(dir_entry[i].d_name) == MFS_DIRSIZ);
		if (mfs_opts->verbose && strlen(name) > MFS_DIRSIZ)
			fprintf(stderr, "File name %s is too long, truncated\n", name);
		strncpy(dir_entry[i].d_name, name, MFS_DIRSIZ);
		put_block(b, dir_entry, fsopts);
		break;
	}
  }

  free(dir_entry);

  return r;
}

/*================================================================
 *	    directory & inode management assist group
 *===============================================================*/
void
enter_dir(ino_t parent, char const *name, ino_t child, fsinfo_t *fsopts)
{
  /* Enter child in parent directory */
  /* Works for dir > 1 block and zone > block */
  unsigned int k;
  block_t b, indir;
  zone_t z;
  int off;
  struct inode *ino;
  struct inode *inoblock = alloc_block();
  zone_t *indirblock = alloc_block();

  assert(!(block_size % sizeof(struct direct)));

    if (debug) printf("enter_dir: parent=%llu, child=%llu, name=%s\n", parent, child, name);
    assert(parent>0);
    assert(child>0);
    
  /* Obtain the inode structure */
  b = ((parent - 1) / inodes_per_block) + inode_offset;
  off = (parent - 1) % inodes_per_block;
  get_block(b, inoblock, fsopts);
  ino = inoblock + off;

  for (k = 0; k < NR_DZONES; k++) {
	z = ino->i_zone[k];
	if (z == 0) {
		z = alloc_zone(fsopts);
		ino->i_zone[k] = z;
	}

	if(dir_try_enter(z, child, __UNCONST(name), fsopts)) {
		put_block(b, inoblock, fsopts);
		free(inoblock);
		free(indirblock);
		return;
	}
  }

  /* no space in directory using just direct blocks; try indirect */
  if (ino->i_zone[S_INDIRECT_IDX] == 0)
  	ino->i_zone[S_INDIRECT_IDX] = alloc_zone(fsopts);

  indir = ino->i_zone[S_INDIRECT_IDX] << zone_shift;
  --indir; /* Compensate for ++indir below */
  for(k = 0; k < (indir_per_zone); k++) {
	if (k % indir_per_block == 0)
		get_block(++indir, indirblock, fsopts);
  	z = indirblock[k % indir_per_block];
	if(!z) {
		z = indirblock[k % indir_per_block] = alloc_zone(fsopts);
		put_block(indir, indirblock, fsopts);
	}
	if(dir_try_enter(z, child, __UNCONST(name), fsopts)) {
		put_block(b, inoblock, fsopts);
		free(inoblock);
		free(indirblock);
		return;
	}
  }

  pexit("Directory-inode %u beyond single indirect blocks.  Could not enter %s",
         (unsigned)parent, name);
}


void
add_zone(ino_t n, zone_t z, size_t bytes, fsnode *cur, fsinfo_t *fsopts)
{
  /* Add zone z to inode n. The file has grown by 'bytes' bytes. */

  unsigned int off, i, j;
  block_t b;
  zone_t indir, dindir;
  struct inode *p, *inode;
  zone_t *blk, *dblk;
	struct stat *st = stampst.st_ino ? &stampst : &cur->inode->st;

  assert(inodes_per_block*sizeof(*inode) == block_size);
  if(!(inode = alloc_block()))
  	err(1, "Couldn't allocate block of inodes");

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;
  get_block(b, inode, fsopts);
  p = &inode[off];
  p->i_size += bytes;
  p->i_mtime = st->st_mtime;
#ifndef MFS_INODE_ONLY_MTIME /* V1 file systems did not have them... */
  p->i_atime = st->st_atime;
  p->i_ctime = st->st_ctime;
#endif
  for (i = 0; i < NR_DZONES; i++)
	if (p->i_zone[i] == 0) {
		p->i_zone[i] = z;
		put_block(b, inode, fsopts);
  		free(inode);
		return;
	}

  assert(indir_per_block*sizeof(*blk) == block_size);
  if(!(blk = alloc_block()))
  	err(1, "Couldn't allocate indirect block");

  /* File has grown beyond a small file. */
  if (p->i_zone[S_INDIRECT_IDX] == 0)
	p->i_zone[S_INDIRECT_IDX] = alloc_zone(fsopts);
  indir = p->i_zone[S_INDIRECT_IDX] << zone_shift;
  put_block(b, inode, fsopts);
  --indir; /* Compensate for ++indir below */
  for (i = 0; i < (indir_per_zone); i++) {
	if (i % indir_per_block == 0)
		get_block(++indir, blk, fsopts);
	if (blk[i % indir_per_block] == 0) {
		blk[i] = z;
		put_block(indir, blk, fsopts);
  		free(blk);
  		free(inode);
		return;
	}
  }

  /* File has grown beyond single indirect; we need a double indirect */
  assert(indir_per_block*sizeof(*dblk) == block_size);
  if(!(dblk = alloc_block()))
  	err(1, "Couldn't allocate double indirect block");

  if (p->i_zone[D_INDIRECT_IDX] == 0)
	p->i_zone[D_INDIRECT_IDX] = alloc_zone(fsopts);
  dindir = p->i_zone[D_INDIRECT_IDX] << zone_shift;
  put_block(b, inode, fsopts);
  --dindir; /* Compensate for ++indir below */
  for (j = 0; j < (indir_per_zone); j++) {
	if (j % indir_per_block == 0)
		get_block(++dindir, dblk, fsopts);
	if (dblk[j % indir_per_block] == 0)
		dblk[j % indir_per_block] = alloc_zone(fsopts);
	indir = dblk[j % indir_per_block] << zone_shift;
	--indir; /* Compensate for ++indir below */
	for (i = 0; i < (indir_per_zone); i++) {
		if (i % indir_per_block == 0)
			get_block(++indir, blk, fsopts);
		if (blk[i % indir_per_block] == 0) {
			blk[i] = z;
			put_block(dindir, dblk, fsopts);
			put_block(indir, blk, fsopts);
	  		free(dblk);
	  		free(blk);
	  		free(inode);
			return;
		}
	}
  }

  pexit("File has grown beyond double indirect");
}


/* Increment the link count to inode n */
void
incr_link(ino_t n, fsinfo_t *fsopts)
{
  int off;
  static int enter = 0;
  static struct inode *inodes = NULL;
  block_t b;

  if (enter++) pexit("internal error: recursive call to incr_link()");

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;
  {
	assert(sizeof(*inodes) * inodes_per_block == block_size);
	if(!inodes && !(inodes = alloc_block()))
		err(1, "couldn't allocate a block of inodes");

	get_block(b, inodes, fsopts);
	inodes[off].i_nlinks++;
	/* Check overflow (particularly on V1)... */
	if (inodes[off].i_nlinks <= 0)
		pexit("Too many links to a directory");
	put_block(b, inodes, fsopts);
  }
  enter = 0;
}


/* Increment the file-size in inode n */
void
incr_size(ino_t n, size_t count, fsinfo_t *fsopts)
{
  block_t b;
  int off;

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;
  {
	struct inode *inodes;

	assert(inodes_per_block * sizeof(*inodes) == block_size);
	if(!(inodes = alloc_block()))
		err(1, "couldn't allocate a block of inodes");

	get_block(b, inodes, fsopts);
	/* Check overflow; avoid compiler spurious warnings */
	if (inodes[off].i_size+(int)count < inodes[off].i_size ||
	    (int)inodes[off].i_size > MAX_MAX_SIZE-(int)count)
		pexit("File has become too big to be handled by MFS");
	inodes[off].i_size += count;
	put_block(b, inodes, fsopts);
	free(inodes);
  }
}


/*================================================================
 * 	 	     allocation assist group
 *===============================================================*/
static ino_t
alloc_inode(int mode, int usrid, int grpid, fsinfo_t *fsopts)
{
  ino_t num;
  int off;
  block_t b;
  struct inode *inodes;
	mfs_opt_t	*mfs_opts = fsopts->fs_specific;

  num = fsopts->curinode++;
  if (num > nrinodes) {
  	pexit("File system does not have enough inodes (only %llu)", nrinodes);
  }
  b = ((num - 1) / inodes_per_block) + inode_offset;
  off = (num - 1) % inodes_per_block;

  assert(inodes_per_block * sizeof(*inodes) == block_size);
  if(!(inodes = alloc_block()))
	err(1, "couldn't allocate a block of inodes");

  get_block(b, inodes, fsopts);
  if (inodes[off].i_mode) {
	pexit("allocation new inode %llu with non-zero mode - this cannot happen",
		num);
  }
  inodes[off].i_mode = mode;
  inodes[off].i_uid = usrid;
  inodes[off].i_gid = grpid;
  if (mfs_opts->verbose && (inodes[off].i_uid != usrid || inodes[off].i_gid != grpid))
	fprintf(stderr, "Uid/gid %d.%d do not fit within inode, truncated\n", usrid, grpid);
  put_block(b, inodes, fsopts);

  free(inodes);

  /* Set the bit in the bit map. */
  insert_bit((block_t) INODE_MAP, num, fsopts);
  return(num);
}


/* Allocate a new zone */
static zone_t
alloc_zone(fsinfo_t *fsopts)
{
  /* Works for zone > block */
  block_t b;
  unsigned int i;
  zone_t z;

  z = next_zone++;
  b = z << zone_shift;
  if (b > nrblocks - zone_per_block)
	pexit("File system not big enough for all the files");
  for (i = 0; i < zone_per_block; i++)
	put_block(b + i, zero, fsopts);	/* give an empty zone */
  
  insert_bit(zone_map, z - zoff, fsopts);
  return z;
}


/* Insert one bit into the bitmap */
void
insert_bit(block_t map, bit_t bit, fsinfo_t *fsopts)
{
  unsigned int boff, w, s;
  unsigned int bits_per_block;
  block_t map_block;
  bitchunk_t *buf;

  buf = alloc_block();

  bits_per_block = FS_BITS_PER_BLOCK(block_size);
  map_block = map + bit / bits_per_block;
  if (map_block >= inode_offset)
	pexit("insertbit invades inodes area - this cannot happen");
  boff = bit % bits_per_block;

  assert(boff >=0);
  assert(boff < FS_BITS_PER_BLOCK(block_size));
  get_block(map_block, buf, fsopts);
  w = boff / FS_BITCHUNK_BITS;
  s = boff % FS_BITCHUNK_BITS;
  buf[w] |= (1 << s);
  put_block(map_block, buf, fsopts);

  free(buf);
}





/*================================================================
 *			other stuff
 *===============================================================*/




__dead void
pexit(char const * s, ...)
{
  va_list va;

  va_start(va, s);
  vwarn(s, va);
  va_end(va);
  exit(2);
}


void *
alloc_block(void)
{
	void *buf;

	if(!(buf = malloc(block_size))) {
		err(1, "couldn't allocate filesystem buffer");
	}
	memset(buf, 0, block_size);

	return buf;
}

void
print_fs(fsinfo_t *fsopts)
{
  unsigned int i, j;
  ino_t k;
  struct inode *inode2;
  unsigned short *usbuf;
  block_t b;
  struct direct *dir;

  assert(inodes_per_block * sizeof(*inode2) == block_size);
  if(!(inode2 = alloc_block()))
	err(1, "couldn't allocate a block of inodes");

  assert(NR_DIR_ENTRIES(block_size)*sizeof(*dir) == block_size);
  if(!(dir = alloc_block()))
	err(1, "couldn't allocate a block of directory entries");

  usbuf = alloc_block();
  get_super_block(usbuf, fsopts);
  printf("\nSuperblock: ");
  for (i = 0; i < 8; i++) printf("%06ho ", usbuf[i]);
  printf("\n            ");
  for (i = 0; i < 8; i++) printf("%#04hX ", usbuf[i]);
  printf("\n            ");
  for (i = 8; i < 15; i++) printf("%06ho ", usbuf[i]);
  printf("\n            ");
  for (i = 8; i < 15; i++) printf("%#04hX ", usbuf[i]);
  get_block((block_t) INODE_MAP, usbuf, fsopts);
  printf("...\nInode map:  ");
  for (i = 0; i < 9; i++) printf("%06ho ", usbuf[i]);
  get_block((block_t) zone_map, usbuf, fsopts);
  printf("...\nZone  map:  ");
  for (i = 0; i < 9; i++) printf("%06ho ", usbuf[i]);
  printf("...\n");

  free(usbuf);
  usbuf = NULL;

  k = 0;
  for (b = inode_offset; k < nrinodes; b++) {
	get_block(b, inode2, fsopts);
	for (i = 0; i < inodes_per_block; i++) {
		k = inodes_per_block * (int) (b - inode_offset) + i + 1;
		/* Lint but OK */
		if (k > nrinodes) break;
		{
			if (inode2[i].i_mode != 0) {
				printf("Inode %3u:  mode=", (unsigned)k);
				printf("%06o", (unsigned)inode2[i].i_mode);
				printf("  uid=%2d  gid=%2d  size=",
					(int)inode2[i].i_uid, (int)inode2[i].i_gid);
				printf("%6ld", (long)inode2[i].i_size);
				printf("  zone[0]=%u\n", (unsigned)inode2[i].i_zone[0]);
			}
			if ((inode2[i].i_mode & S_IFMT) == S_IFDIR) {
				/* This is a directory */
				get_block(inode2[i].i_zone[0] << zone_shift, dir, fsopts);
				for (j = 0; j < NR_DIR_ENTRIES(block_size); j++)
					if (dir[j].d_ino)
						printf("\tInode %2u: %s\n",
							(unsigned)dir[j].d_ino,
							dir[j].d_name);
			}
		}
	}
  }

  if (zone_shift)
	printf("%d inodes used.     %u zones (%u blocks) used.\n",
		(int)fsopts->curinode, next_zone, next_zone*zone_per_block);
  else
	printf("%d inodes used.     %u zones used.\n", (int)fsopts->curinode, next_zone);
  free(dir);
  free(inode2);
}


/*
 * The first time a block is read, it returns all 0s, unless there has
 * been a write.  This routine checks to see if a block has been accessed.
 */
int
read_and_set(block_t n)
{
  unsigned int w, s, mask, r;

  w = n / 8;
  
  assert(n < nrblocks);
  if(w >= umap_array_elements) {
	errx(1, "umap array too small - this can't happen");
  }
  s = n % 8;
  mask = 1 << s;
  r = (umap_array[w] & mask ? 1 : 0);
  umap_array[w] |= mask;
  return(r);
}


int
create_special(const char *image, fsinfo_t *fsopts)
{
	int	oflags = O_RDWR | O_CREAT;
  		/* create image */
	if (fsopts->offset == 0)
		oflags |= O_TRUNC;
	if ((fsopts->fd = open(image, oflags, 0666)) == -1) {
		warn("Can't open `%s' for writing", image);
		return (-1);
	}
	
	if (fsopts->offset != 0)
		if (lseek(fsopts->fd, fsopts->offset, SEEK_SET) == -1) {
			warn("can't seek");
			return -1;
		}
		
	return (fsopts->fd);
}



/* Read a block. */
void
get_block(block_t n, void *buf, fsinfo_t *fsopts)
{
  ssize_t k;
  
  //if (debug) printf("get_block; block = %u\n", n);

  /* First access returns a zero block */
  if (read_and_set(n) == 0) {
	memcpy(buf, zero, block_size);
	return;
  }
  mkfs_seek((uint64_t)(n) * block_size, SEEK_SET, fsopts);
  k = read(fsopts->fd, buf, block_size);
  if (k != (ssize_t)block_size)
	pexit("get_block couldn't read block #%u", (unsigned)n);
}

/* Read the super block. */
void
get_super_block(void *buf, fsinfo_t *fsopts)
{
  ssize_t k;

  mkfs_seek((off_t) SUPER_BLOCK_BYTES, SEEK_SET, fsopts);
  k = read(fsopts->fd, buf, SUPER_BLOCK_BYTES);
  if (k != (ssize_t)SUPER_BLOCK_BYTES)
	err(1, "get_super_block couldn't read super block");
}

/* Write a block. */
void
put_block(block_t n, void *buf, fsinfo_t *fsopts)
{

  //if (debug) printf("put_block; block = %u\n", n);

  (void) read_and_set(n);

  mkfs_seek((uint64_t)(n) * block_size, SEEK_SET, fsopts);
  mkfs_write(buf, block_size, fsopts);
}

static ssize_t
mkfs_write(void * buf, size_t count, fsinfo_t *fsopts)
{
	uint64_t fssize;
	ssize_t w;

	/* Perform & check write */
	w = write(fsopts->fd, buf, count);
	if(w < 0)
		err(1, "mkfs_write: write failed");
	if(w != (ssize_t)count)
		errx(1, "mkfs_write: short write: %zd != %zu", w, count);

	/* Check if this has made the FS any bigger; count bytes after offset */
	fssize = mkfs_seek(0, SEEK_CUR, fsopts);

	assert(fssize >= (uint64_t)fsopts->offset);
	fssize -= fsopts->offset;
	fssize = roundup(fssize, block_size);
	if(fssize > written_fs_size)
		written_fs_size = fssize;

	return w;
}

/* Seek to position in FS we're creating. */
static uint64_t
mkfs_seek(uint64_t pos, int whence, fsinfo_t *fsopts)
{
	if(whence == SEEK_SET) pos += fsopts->offset;
	off_t newpos;
	if((newpos=lseek(fsopts->fd, pos, whence)) == (off_t) -1)
		err(1, "mkfs_seek: lseek failed");
	return newpos;
}
