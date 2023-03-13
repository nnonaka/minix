/* IBM device driver utility functions.			Author: Kees J. Bot
 *								7 Dec 1995
 * Entry point:
 *   partition:	partition a disk to the partition table(s) on it.
 */

/*	$NetBSD: biosdisk.c,v 1.49.6.4 2019/12/17 13:01:39 martin Exp $	*/

/*
 * Copyright (c) 1996, 1998
 *	Matthias Drochner.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * raw BIOS disk device for libsa.
 * needs lowlevel parts from bios_disk.S and biosdisk_ll.c
 * partly from netbsd:sys/arch/i386/boot/disk.c
 * no bad144 handling!
 *
 * A lot of this must match sys/kern/subr_disk_mbr.c
 */

/*
 * Ported to boot 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 *
 * Mach Operating System
 * Copyright (c) 1992, 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stdbool.h>
#include <unistd.h>
#include <sys/disklabel.h>
#include <sys/disklabel_gpt.h>
#include <sys/uuid.h>

#include <minix/blockdriver.h>
#include <minix/drvlib.h>

extern uint32_t crc32(uint32_t crc, const uint8_t *buf, size_t len);

/* Extended partition? */
#define ext_part(s)	((s) == 0x05 || (s) == 0x0F)


static void parse_part_table(struct blockdriver *bdp, int device,
	int style, int atapi, u8_t *tmp_buf);

static void extpartition(struct blockdriver *bdp, int extdev,
	unsigned long extbase, u8_t *tmp_buf);

static int get_part_table(struct blockdriver *bdp, int device,
	unsigned long offset, struct part_entry *table, u8_t *tmp_buf);

static void sort(struct part_entry *table);

static void biosdisk_readpartition(struct blockdriver *bdp, int device, daddr_t offset, daddr_t size, u8_t *tmp_buf);


#define BIOSDISK_PART_NAME_LEN 36

#define BIOSDISKNPART 26

struct biosdisk_partition {
	daddr_t offset;
	daddr_t size;
	int     fstype;
	const struct gpt_part {
		const struct uuid *guid;
		const char *name;
	} *guid;
	uint64_t attr;
	char *part_name; /* maximum BIOSDISK_PART_NAME_LEN */
};


struct uuid;
bool guid_is_nil(const struct uuid *);
bool guid_is_equal(const struct uuid *, const struct uuid *);

/* GPT TYPE TABLE */
const struct uuid GET_nbsd_raid = GPT_ENT_TYPE_NETBSD_RAIDFRAME;
const struct uuid GET_nbsd_ffs = GPT_ENT_TYPE_NETBSD_FFS;
const struct uuid GET_nbsd_lfs = GPT_ENT_TYPE_NETBSD_LFS;
const struct uuid GET_nbsd_swap = GPT_ENT_TYPE_NETBSD_SWAP;
const struct uuid GET_nbsd_ccd = GPT_ENT_TYPE_NETBSD_CCD;
const struct uuid GET_nbsd_cgd = GPT_ENT_TYPE_NETBSD_CGD;

const struct uuid GET_efi = GPT_ENT_TYPE_EFI;
const struct uuid GET_mbr = GPT_ENT_TYPE_MBR;
const struct uuid GET_fbsd = GPT_ENT_TYPE_FREEBSD;
const struct uuid GET_fbsd_swap = GPT_ENT_TYPE_FREEBSD_SWAP;
const struct uuid GET_fbsd_ufs = GPT_ENT_TYPE_FREEBSD_UFS;
const struct uuid GET_fbsd_vinum = GPT_ENT_TYPE_FREEBSD_VINUM;
const struct uuid GET_fbsd_zfs = GPT_ENT_TYPE_FREEBSD_ZFS;
const struct uuid GET_ms_rsvd = GPT_ENT_TYPE_MS_RESERVED;
const struct uuid GET_ms_basic_data = GPT_ENT_TYPE_MS_BASIC_DATA;
const struct uuid GET_ms_ldm_metadata = GPT_ENT_TYPE_MS_LDM_METADATA;
const struct uuid GET_ms_ldm_data = GPT_ENT_TYPE_MS_LDM_DATA;
const struct uuid GET_linux_data = GPT_ENT_TYPE_LINUX_DATA;
const struct uuid GET_linux_raid = GPT_ENT_TYPE_LINUX_RAID;
const struct uuid GET_linux_swap = GPT_ENT_TYPE_LINUX_SWAP;
const struct uuid GET_linux_lvm = GPT_ENT_TYPE_LINUX_LVM;
const struct uuid GET_apple_hfs = GPT_ENT_TYPE_APPLE_HFS;
const struct uuid GET_apple_ufs = GPT_ENT_TYPE_APPLE_UFS;
const struct uuid GET_minix_mfs = GPT_ENT_TYPE_MINIX_MFS;
const struct uuid GET_bios = GPT_ENT_TYPE_BIOS;

const struct gpt_part gpt_parts[] = {
	{ &GET_nbsd_raid,	"NetBSD RAID" },
	{ &GET_nbsd_ffs,	"NetBSD FFS" },
	{ &GET_nbsd_lfs,	"NetBSD LFS" },
	{ &GET_nbsd_swap,	"NetBSD Swap" },
	{ &GET_nbsd_ccd,	"NetBSD ccd" },
	{ &GET_nbsd_cgd,	"NetBSD cgd" },
	{ &GET_efi,		"EFI System" },
	{ &GET_mbr,		"MBR" },
	{ &GET_fbsd,		"FreeBSD" },
	{ &GET_fbsd_swap,	"FreeBSD Swap" },
	{ &GET_fbsd_ufs,	"FreeBSD UFS" },
	{ &GET_fbsd_vinum,	"FreeBSD Vinum" },
	{ &GET_fbsd_zfs,	"FreeBSD ZFS" },
	{ &GET_ms_rsvd,		"Microsoft Reserved" },
	{ &GET_ms_basic_data,	"Microsoft Basic data" },
	{ &GET_ms_ldm_metadata,	"Microsoft LDM metadata" },
	{ &GET_ms_ldm_data,	"Microsoft LDM data" },
	{ &GET_linux_data,	"Linux data" },
	{ &GET_linux_raid,	"Linux RAID" },
	{ &GET_linux_swap,	"Linux Swap" },
	{ &GET_linux_lvm,	"Linux LVM" },
	{ &GET_apple_hfs,	"Apple HFS" },
	{ &GET_apple_ufs,	"Apple UFS" },
	{ &GET_minix_mfs,	"Minix MFS" },
	{ &GET_bios,		"BIOS Boot (GRUB)" },
};


/*============================================================================*
 *				partition				      *
 *============================================================================*/
void partition(
	struct blockdriver *bdp,	/* device dependent entry points */
	int device,             	/* device to partition */
	int style,              	/* partitioning style: floppy, primary, sub. */
	int atapi               	/* atapi device */
)
{
/* This routine is called on first open to initialize the partition tables
 * of a device.
 */
  u8_t *tmp_buf;

  if ((*bdp->bdr_part)(device) == NULL)
	return;

  /* For multithreaded drivers, multiple partition() calls may be made on
   * different devices in parallel. Hence we need a separate temporary buffer
   * for each request.
   */
  if (!(tmp_buf = alloc_contig(CD_SECTOR_SIZE, AC_ALIGN4K, NULL)))
	panic("partition: unable to allocate temporary buffer");

  parse_part_table(bdp, device, style, atapi, tmp_buf);

  free_contig(tmp_buf, CD_SECTOR_SIZE);
}

/*============================================================================*
 *				parse_part_table			      *
 *============================================================================*/
static void parse_part_table(
	struct blockdriver *bdp,	/* device dependent entry points */
	int device,             	/* device to partition */
	int style,              	/* partitioning style: floppy, primary, sub. */
	int atapi,              	/* atapi device */
	u8_t *tmp_buf           	/* temporary buffer */
)
{
/* This routine reads and parses a partition table.  It may be called
 * recursively.  It makes sure that each partition falls safely within the
 * device's limits.  Depending on the partition style we are either making
 * floppy partitions, primary partitions or subpartitions.  Only primary
 * partitions are sorted, because they are shared with other operating
 * systems that expect this.
 */
  struct part_entry table[NR_PARTITIONS], *pe;
  int disk, par;
  int ptype;
  struct device *dv;
  unsigned long base, limit, part_limit;

  /* Get the geometry of the device to partition */
  if ((dv = (*bdp->bdr_part)(device)) == NULL
				|| dv->dv_size == 0) return;
  base = (unsigned long)(dv->dv_base / SECTOR_SIZE);
  limit = base + (unsigned long)(dv->dv_size / SECTOR_SIZE);

  /* Read the partition table for the device. */
  ptype = get_part_table(bdp, device, 0L, table, tmp_buf);
  if (ptype == 0) { /* No Partition */
	  return;
  }
  if ((ptype == 1) && (style == P_PRIMARY)) { /* GPT Partition */
    biosdisk_readpartition(bdp, device, 0, 0, tmp_buf);
    return;
  }

  /* Compute the device number of the first partition. */
  switch (style) {
  case P_FLOPPY:
	device += MINOR_fd0p0;
	break;
  case P_PRIMARY:
	sort(table);		/* sort a primary partition table */
	device += 1;
	break;
  case P_SUB:
	disk = device / DEV_PER_DRIVE;
	par = device % DEV_PER_DRIVE - 1;
	device = MINOR_d0p0s0 + (disk * NR_PARTITIONS + par) * NR_PARTITIONS;
  }

  /* Find an array of devices. */
  if ((dv = (*bdp->bdr_part)(device)) == NULL) return;

  /* Set the geometry of the partitions from the partition table. */
  for (par = 0; par < NR_PARTITIONS; par++, dv++) {
	/* Shrink the partition to fit within the device. */
	pe = &table[par];
	part_limit = pe->lowsec + pe->size;
	if (part_limit < pe->lowsec) part_limit = limit;
	if (part_limit > limit) part_limit = limit;
	if (pe->lowsec < base) pe->lowsec = base;
	if (part_limit < pe->lowsec) part_limit = pe->lowsec;

	dv->dv_base = (u64_t)pe->lowsec * SECTOR_SIZE;
	dv->dv_size = (u64_t)(part_limit - pe->lowsec) * SECTOR_SIZE;

	if (style == P_PRIMARY) {
		/* Each Minix primary partition can be subpartitioned. */
		if (pe->sysind == MINIX_PART)
			parse_part_table(bdp, device + par, P_SUB, atapi,
				tmp_buf);

		/* An extended partition has logical partitions. */
		if (ext_part(pe->sysind))
			extpartition(bdp, device + par, pe->lowsec, tmp_buf);
	}
  }
}

/*============================================================================*
 *				extpartition				      *
 *============================================================================*/
static void extpartition(
	struct blockdriver *bdp,	/* device dependent entry points */
	int extdev,             	/* extended partition to scan */
	unsigned long extbase,  	/* sector offset of the base ext. partition */
	u8_t *tmp_buf           	/* temporary buffer */
)
{
/* Extended partitions cannot be ignored alas, because people like to move
 * files to and from DOS partitions.  Avoid reading this code, it's no fun.
 */
  struct part_entry table[NR_PARTITIONS], *pe;
  int subdev, disk, par;
  struct device *dv;
  unsigned long offset, nextoffset;

  disk = extdev / DEV_PER_DRIVE;
  par = extdev % DEV_PER_DRIVE - 1;
  subdev = MINOR_d0p0s0 + (disk * NR_PARTITIONS + par) * NR_PARTITIONS;

  offset = 0;
  do {
	if (get_part_table(bdp, extdev, offset, table, tmp_buf) != 1) return;
	sort(table);

	/* The table should contain one logical partition and optionally
	 * another extended partition.  (It's a linked list.)
	 */
	nextoffset = 0;
	for (par = 0; par < NR_PARTITIONS; par++) {
		pe = &table[par];
		if (ext_part(pe->sysind)) {
			nextoffset = pe->lowsec;
		} else
		if (pe->sysind != NO_PART) {
			if ((dv = (*bdp->bdr_part)(subdev)) == NULL) return;

			dv->dv_base = (u64_t)(extbase + offset + pe->lowsec) *
								SECTOR_SIZE;
			dv->dv_size = (u64_t)pe->size * SECTOR_SIZE;

			/* Out of devices? */
			if (++subdev % NR_PARTITIONS == 0) return;
		}
	}
  } while ((offset = nextoffset) != 0);
}

/*============================================================================*
 *				get_part_table				      *
 *============================================================================*/
static int get_part_table(
	struct blockdriver *bdp,
	int device,
	unsigned long offset,    	/* sector offset to the table */
	struct part_entry *table,	/* four entries */
	u8_t *tmp_buf)           	/* temporary buffer */
{
/* Read the partition table for the device, return true iff there were no
 * errors.
 * NN: extend to recognize gpt.
 */
  iovec_t iovec1;
  u64_t position;
  int r;

  position = (u64_t)offset * SECTOR_SIZE;
  iovec1.iov_addr = (vir_bytes) tmp_buf;
  iovec1.iov_size = CD_SECTOR_SIZE;
  r = (*bdp->bdr_transfer)(device, FALSE /*do_write*/, position, SELF,
	&iovec1, 1, BDEV_NOFLAGS);
  if (r != CD_SECTOR_SIZE) {
	return 0;
  }
  if (tmp_buf[510] != 0x55 || tmp_buf[511] != 0xAA) {
	/* Invalid partition table. */
	return 0;
  }
  memcpy(table, (tmp_buf + PART_TABLE_OFF), NR_PARTITIONS * sizeof(table[0]));
  if (table[0].sysind == 0xEE) {
  	/* GPT Protective */
  	return 2;
  }
  return 1;
}

/*===========================================================================*
 *				sort					     *
 *===========================================================================*/
static void sort(struct part_entry *table)
{
/* Sort a partition table. */
  struct part_entry *pe, tmp;
  int n = NR_PARTITIONS;

  do {
	for (pe = table; pe < table + NR_PARTITIONS-1; pe++) {
		if (pe[0].sysind == NO_PART
			|| (pe[0].lowsec > pe[1].lowsec
					&& pe[1].sysind != NO_PART)) {
			tmp = pe[0]; pe[0] = pe[1]; pe[1] = tmp;
		}
	}
  } while (--n > 0);
}

/*
 *  GPT functions. 
 *  Copied from NetBSD sys/arch/i386/stand/lib/biosdisk.c
 */

static int
readsects(struct blockdriver *bdp, int device, daddr_t dblk, int num, u8_t *tmp_buf)

{
  iovec_t iovec1;
  u64_t position;
  int r;

  position = (u64_t)dblk * SECTOR_SIZE;
  iovec1.iov_addr = (vir_bytes) tmp_buf;
  iovec1.iov_size = CD_SECTOR_SIZE;
  r = (*bdp->bdr_transfer)(device, FALSE /*do_write*/, position, SELF,
	&iovec1, 1, BDEV_NOFLAGS);
  if (r != CD_SECTOR_SIZE) {
	return -1;
  }
  return 0;
}

bool
guid_is_nil(const struct uuid *u)
{
	static const struct uuid nil = { .time_low = 0 };
	return (memcmp(u, &nil, sizeof(*u)) == 0 ? true : false);
}

bool
guid_is_equal(const struct uuid *a, const struct uuid *b)
{
	return (memcmp(a, b, sizeof(*a)) == 0 ? true : false);
}

static int
check_gpt(struct blockdriver *bdp, int device, struct biosdisk_partition *part, daddr_t rf_offset, daddr_t sector, u8_t *tmp_buf)
{
	struct gpt_hdr gpth;
	const struct gpt_ent *ep;
	const struct uuid *u;
	daddr_t entblk;
	size_t size;
	uint32_t crc;
	int sectors;
	int entries;
	uint32_t entry;
	int i, j;

	/* read in gpt_hdr sector */
	if (readsects(bdp, device, sector, 1, tmp_buf)) {
		panic("Error reading GPT header.");
		return -1;
	}

	memcpy(&gpth, tmp_buf, sizeof(gpth));

	if (memcmp(GPT_HDR_SIG, gpth.hdr_sig, sizeof(gpth.hdr_sig)))
		return -1;

	crc = gpth.hdr_crc_self;
	gpth.hdr_crc_self = 0;
	gpth.hdr_crc_self = crc32(0, (const void *)&gpth, GPT_HDR_SIZE);
	if (gpth.hdr_crc_self != crc) {
		return -1;
	}

	if (gpth.hdr_lba_self + rf_offset != (uint64_t)sector)
		return -1;

	sectors = CD_SECTOR_SIZE/SECTOR_SIZE; /* sectors per buffer */
	entries = CD_SECTOR_SIZE/gpth.hdr_entsz; /* entries per buffer */
	entblk = gpth.hdr_lba_table + rf_offset;
	crc = crc32(0, NULL, 0);

	j = 0;
	ep = (const struct gpt_ent *)tmp_buf;

	for (entry = 0; entry < gpth.hdr_entries; entry += entries) {
		size = MIN(CD_SECTOR_SIZE,
		    (gpth.hdr_entries - entry) * gpth.hdr_entsz);
		entries = size / gpth.hdr_entsz;
		sectors = roundup(size, SECTOR_SIZE) / SECTOR_SIZE;
		if (readsects(bdp, device, entblk, sectors, tmp_buf))
			return -1;
		entblk += sectors;
		crc = crc32(crc, (const void *)tmp_buf, size);

		for (i = 0; j < BIOSDISKNPART && i < entries; i++) {
			u = (const struct uuid *)ep[i].ent_type;
			if (!guid_is_nil(u)) {
				part[j].offset = ep[i].ent_lba_start;
				part[j].size = ep[i].ent_lba_end -
				    ep[i].ent_lba_start + 1;
				if (guid_is_equal(u, &GET_nbsd_ffs))
					part[j].fstype = FS_BSDFFS;
				else if (guid_is_equal(u, &GET_nbsd_lfs))
					part[j].fstype = FS_BSDLFS;
				else if (guid_is_equal(u, &GET_nbsd_raid))
					part[j].fstype = FS_RAID;
				else if (guid_is_equal(u, &GET_nbsd_swap))
					part[j].fstype = FS_SWAP;
				else if (guid_is_equal(u, &GET_nbsd_ccd))
					part[j].fstype = FS_CCD;
				else if (guid_is_equal(u, &GET_nbsd_cgd))
					part[j].fstype = FS_CGD;
				else if (guid_is_equal(u, &GET_minix_mfs))
					part[j].fstype = FS_MINIXFS3;
				else
					part[j].fstype = FS_OTHER;
				for (int k = 0;
				     k < (int)__arraycount(gpt_parts);
				     k++) {
					if (guid_is_equal(u, gpt_parts[k].guid))
						part[j].guid = &gpt_parts[k];
				}
				part[j].attr = ep[i].ent_attr;

				j++;
			}
		}

	}

	if (crc != gpth.hdr_crc_table) {
		panic("GPT table CRC invalid\n");
		return -1;
	}

	return 0;
}

static int
read_gpt(struct blockdriver *bdp, int device, struct biosdisk_partition *part, daddr_t rf_offset, daddr_t rf_size, u8_t *tmp_buf)
{
	daddr_t gptsector[2];
	int i, error;

	if (rf_offset && rf_size) {
		gptsector[0] = rf_offset + GPT_HDR_BLKNO;
		gptsector[1] = rf_offset + rf_size - 1;
	} else {
		gptsector[0] = GPT_HDR_BLKNO;
		gptsector[1] = rf_size - 1;
	}

	for (i = 0; i < (int)__arraycount(gptsector); i++) {
		error = check_gpt(bdp, device, part, rf_offset, gptsector[i], tmp_buf);
		if (error == 0)
			break;
	}

	if (i >= (int)__arraycount(gptsector)) {
		memset(part, 0, sizeof(struct biosdisk_partition));
		return -1;
	}

#ifndef USE_SECONDARY_GPT
	if (i > 0) {
#ifdef DISK_DEBUG
		printf("ignoring valid secondary GPT\n");
#endif
		return -1;
	}
#endif

#ifdef DISK_DEBUG
	printf("using %s GPT\n", (i == 0) ? "primary" : "secondary");
#endif
	return 0;
}

/*
 * TODO: support more than 4 gpt partition.
 */
static void
biosdisk_readpartition(struct blockdriver *bdp, int device, daddr_t offset, daddr_t size, u8_t *tmp_buf)
{
	struct biosdisk_partition gpt_partitions[BIOSDISKNPART];
	
    struct device *dv;
	struct biosdisk_partition *part;
	int i;

	/* Look for netbsd partition that is the dos boot one */

	if (read_gpt(bdp, device, gpt_partitions, offset, size, tmp_buf)) {
		return;
	}
	
	/* Find an array of devices. */
    if ((dv = (*bdp->bdr_part)(device)) == NULL) return;

    /* Set the geometry of the partitions from the partition table. */
    part = gpt_partitions;
    for (i = 0; i < NR_PARTITIONS; i++, dv++) {
    	dv->dv_base = part[i].offset * SECTOR_SIZE;
		dv->dv_size = part[i].size * SECTOR_SIZE;
		dv->fstype = part[i].fstype;
    }

}
