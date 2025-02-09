/*	$NetBSD: conf.c,v 1.2.6.1 2019/09/27 09:32:22 martin Exp $	 */

/*
 * Copyright (c) 1997
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
 */

#include <sys/cdefs.h>
#include <sys/types.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/lfs.h>
#ifdef SUPPORT_EXT2FS
#include <lib/libsa/ext2fs.h>
#endif
#ifdef SUPPORT_MINIXFS3
#include <lib/libsa/minixfs3.h>
#endif
#ifdef SUPPORT_DOSFS
#include <lib/libsa/dosfs.h>
#endif
#ifdef SUPPORT_CD9660
#include <lib/libsa/cd9660.h>
#endif
#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
#include <lib/libsa/net.h>
#include <lib/libsa/dev_net.h>
#ifdef SUPPORT_NFS
#include <lib/libsa/nfs.h>
#endif
#ifdef SUPPORT_TFTP
#include <lib/libsa/tftp.h>
#endif
#endif
#include <biosdisk.h>
#include "devopen.h"
#include "efinet.h"

struct devsw devsw[] = {
	{ "disk", biosdisk_strategy, biosdisk_open, biosdisk_close, biosdisk_ioctl },
#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
	{ "net", net_strategy, net_open, net_close, net_ioctl },
#endif
};
int ndevs = __arraycount(devsw);

struct netif_driver *netif_drivers[] = {
#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
	&efinetif,
#endif
};
int n_netif_drivers = __arraycount(netif_drivers);

struct fs_ops file_system[] = {
#ifdef SUPPORT_CD9660
	FS_OPS(null),
#endif
	FS_OPS(null), FS_OPS(null),
	FS_OPS(null), FS_OPS(null),
#ifdef SUPPORT_EXT2FS
	FS_OPS(null),
#endif
	FS_OPS(null),
#ifdef SUPPORT_DOSFS
	FS_OPS(null),
#endif
};
int nfsys = __arraycount(file_system);

struct fs_ops file_system_disk[] = {
#ifdef SUPPORT_CD9660
	FS_OPS(cd9660),
#endif
	FS_OPS(ffsv1), FS_OPS(ffsv2),
	FS_OPS(lfsv1), FS_OPS(lfsv2),
#ifdef SUPPORT_EXT2FS
	FS_OPS(ext2fs),
#endif
	FS_OPS(minixfs3),
#ifdef SUPPORT_DOSFS
	FS_OPS(dosfs),
#endif
};
__CTASSERT(__arraycount(file_system) == __arraycount(file_system_disk));
const int nfsys_disk = __arraycount(file_system_disk);

struct fs_ops file_system_null = FS_OPS(null);
#ifdef SUPPORT_NFS
struct fs_ops file_system_nfs = FS_OPS(nfs);
#endif
#ifdef SUPPORT_TFTP
struct fs_ops file_system_tftp = FS_OPS(tftp);
#endif

#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
const struct netboot_fstab netboot_fstab[] = {
#ifdef SUPPORT_NFS
	{ "nfs", &file_system_nfs },
#endif
#ifdef SUPPORT_TFTP
	{ "tftp", &file_system_tftp },
#endif
};
const int nnetboot_fstab = __arraycount(netboot_fstab);
#endif
