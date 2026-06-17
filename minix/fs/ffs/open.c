#include "fs.h"
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include "buf.h"
#include "inode.h"
#include "super.h"

static struct inode *new_node(struct inode *ldirp, char *string, mode_t bits,
	uid_t uid, gid_t gid, dev_t rdev);

/*===========================================================================*
 *				fs_create				     *
 *===========================================================================*/
int fs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node)
{
  struct inode *ldirp, *rip;
  int r;

  if ((ldirp = get_inode(fs_dev, dir_nr)) == NULL)
	return(ENOENT);

  rip = new_node(ldirp, name, mode, uid, gid, NO_DEV);
  r = err_code;

  if (r != OK) {
	put_inode(ldirp);
	put_inode(rip);
	return(r);
  }

  node->fn_ino_nr = rip->i_num;
  node->fn_mode = rip->i_din.di_mode;
  node->fn_size = rip->i_din.di_size;
  node->fn_uid = rip->i_din.di_uid;
  node->fn_gid = rip->i_din.di_gid;
  node->fn_dev = NO_DEV;

  put_inode(ldirp);
  return(OK);
}

/*===========================================================================*
 *				fs_mknod				     *
 *===========================================================================*/
int fs_mknod(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	dev_t rdev)
{
  struct inode *ip, *ldirp;

  if ((ldirp = get_inode(fs_dev, dir_nr)) == NULL)
	return(ENOENT);

  ip = new_node(ldirp, name, mode, uid, gid, rdev);

  put_inode(ip);
  put_inode(ldirp);
  return(err_code);
}

/*===========================================================================*
 *				fs_mkdir				     *
 *===========================================================================*/
int fs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid)
{
  struct inode *rip, *ldirp;
  ino_t dot, dotdot;
  int r1, r2;

  if ((ldirp = get_inode(fs_dev, dir_nr)) == NULL)
	return(ENOENT);

  rip = new_node(ldirp, name, mode, uid, gid, NO_DEV);
  if (rip == NULL || err_code == EEXIST) {
	put_inode(rip);
	put_inode(ldirp);
	return(err_code);
  }

  dotdot = ldirp->i_num;		/* parent's inode number */
  dot = rip->i_num;		/* the new directory itself */

  /* Enter "." and ".." in the new directory. */
  r1 = search_dir(rip, ".", &dot, ENTER, I_DIRECTORY);
  r2 = search_dir(rip, "..", &dotdot, ENTER, I_DIRECTORY);

  if (r1 == OK && r2 == OK) {
	rip->i_din.di_nlink++;		/* accounts for "." */
	ldirp->i_din.di_nlink++;	/* accounts for ".." */
	ldirp->i_dirt = IN_DIRTY;
  } else {
	if (search_dir(ldirp, name, NULL, DELETE, 0) != OK)
		panic("ffs: dir disappeared: %llu",
		    (unsigned long long) rip->i_num);
	rip->i_din.di_nlink--;		/* undo new_node()'s increment */
  }
  rip->i_dirt = IN_DIRTY;

  put_inode(ldirp);
  put_inode(rip);
  return(err_code);
}

/*===========================================================================*
 *				fs_slink				     *
 *===========================================================================*/
int fs_slink(ino_t dir_nr, char *name, uid_t uid, gid_t gid,
	struct fsdriver_data *data, size_t bytes)
{
  struct inode *sip, *ldirp;
  struct fs *fs;
  struct buf *bp = NULL;
  char *target = NULL;
  int r;

  if ((ldirp = get_inode(fs_dev, dir_nr)) == NULL)
	return(EINVAL);

  sip = new_node(ldirp, name, (I_SYMBOLIC_LINK | RWX_MODES), uid, gid, NO_DEV);

  if ((r = err_code) == OK) {
	fs = &sip->i_sp->s_fs;
	if (bytes + 1 > (size_t) fs->fs_bsize) {
		r = ENAMETOOLONG;
	} else if ((int) bytes < fs->fs_maxsymlinklen) {
		/* Fast symlink: store the target in the inode itself. */
		target = (char *) sip->i_din.di_db;
		r = fsdriver_copyin(data, 0, target, bytes);
		sip->i_dirt = IN_DIRTY;
	} else {
		/* Slow symlink: store the target in the first data block. */
		if ((bp = new_block(sip, (off_t) 0)) != NULL) {
			target = b_data(bp);
			r = fsdriver_copyin(data, 0, target, bytes);
			lmfs_markdirty(bp);
		} else {
			r = err_code;
		}
	}

	if (r == OK) {
		assert(target != NULL);
		target[bytes] = '\0';
		sip->i_din.di_size = (off_t) strlen(target);
		sip->i_dirt = IN_DIRTY;
		if (sip->i_din.di_size != (u_int64_t) bytes)
			r = ENAMETOOLONG;	/* embedded NUL in the target */
	}

	put_block(bp);			/* accepts NULL */

	if (r != OK) {
		sip->i_din.di_nlink = NO_LINK;
		if (search_dir(ldirp, name, NULL, DELETE, 0) != OK)
			panic("ffs: symlink vanished");
	}
  }

  put_inode(sip);
  put_inode(ldirp);
  return(r);
}

/*===========================================================================*
 *				new_node				     *
 *===========================================================================*/
static struct inode *new_node(struct inode *ldirp, char *string, mode_t bits,
	uid_t uid, gid_t gid, dev_t rdev)
{
/* Allocate a new inode, initialize it, and make a directory entry for it under
 * 'string' in 'ldirp'.  Always sets 'err_code'; returns the inode or NULL. */
  struct inode *rip;
  int r;

  if (ldirp->i_din.di_nlink == NO_LINK) {	/* parent no longer exists */
	err_code = ENOENT;
	return(NULL);
  }

  if (S_ISDIR(bits) && ldirp->i_din.di_nlink >= LINK_MAX) {
	/* Can't add the ".." that the new directory would need. */
	err_code = EMLINK;
	return(NULL);
  }

  /* Does the final path component already exist? */
  rip = advance(ldirp, string);

  if (rip == NULL && err_code == ENOENT) {
	/* It does not: allocate and link a new inode. */
	if ((rip = alloc_inode(ldirp, bits, uid, gid)) == NULL)
		return(NULL);

	/* Write the inode to disk before adding the directory entry, so that a
	 * crash leaves an orphan inode rather than a dangling name. */
	rip->i_din.di_nlink++;
	if (S_ISCHR(bits) || S_ISBLK(bits))
		rip->i_din.di_rdev = (int64_t) rdev;
	rw_inode(rip, WRITING);

	if ((r = search_dir(ldirp, string, &rip->i_num, ENTER,
	    rip->i_din.di_mode & I_TYPE)) != OK) {
		rip->i_din.di_nlink--;
		rip->i_dirt = IN_DIRTY;
		put_inode(rip);		/* frees the disk inode */
		err_code = r;
		return(NULL);
	}
  } else {
	r = (rip != NULL) ? EEXIST : err_code;
	err_code = r;
	return(rip);
  }

  err_code = OK;
  return(rip);
}
