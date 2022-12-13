#ifndef _MKFS_MFS_TYPE_H__
#define _MKFS_MFS_TYPE_H__

/* some Minix specific types that do not conflict with Posix */
#ifndef block_t
typedef uint32_t block_t;	/* block number */
#endif
#ifndef zone_t
typedef uint32_t zone_t;	/* zone number */
#endif
#ifndef bit_t
typedef uint32_t bit_t;		/* bit number in a bit map */
#endif
#ifndef bitchunk_t
typedef uint32_t bitchunk_t;	/* collection of bits in a bitmap */
#endif

/* Declaration of the V2 inode as it is on the disk (not in core). */
/* The same structure is used for V3. */
struct inode {	/* V2/V3 disk inode */
  uint16_t i_mode;		/* file type, protection, etc. */
  uint16_t i_nlinks;		/* how many links to this file. */
  int16_t i_uid;		/* user id of the file's owner. */
  uint16_t i_gid;		/* group number */
  uint32_t i_size;		/* current file size in bytes */
  uint32_t i_atime;		/* when was file data last accessed */
  uint32_t i_mtime;		/* when was file data last changed */
  uint32_t i_ctime;		/* when was inode data last changed */
  uint32_t i_zone[NR_TZONES];	/* zone nums for direct, ind, and dbl ind */
};

#endif
