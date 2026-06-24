# FFS / UFS2 Filesystem Port

## Background

MINIX had no support for the Berkeley Fast File System (FFS / UFS). This work
ports the NetBSD FFS **on-disk format** (UFS2 variant) to MINIX as a new
userspace file-system server, `minix/fs/ffs`, plus the userland tools
`newfs_ffs` (create) and `fsck_ffs` (check).

The NetBSD kernel code in `sys/ufs/ffs` cannot be reused directly: it is bound
to the BSD VFS (`struct vnode`, `vfsops`, `struct buf`, kernel locking, WAPBL
journaling, snapshots, quotas, dirhash, extended attributes). A MINIX FS server
instead speaks the message-based VFS-FS protocol through **libfsdriver**
(`fsdriver_task`) and caches blocks through **libminixfs** (`lmfs_*`), exactly
like the existing `minix/fs/ext2` server — which is itself a from-scratch
reimplementation of the ext2 *on-disk format*, not a port of `sys/ufs/ext2fs`.

So this port keeps the FFS/UFS2 **on-disk format and the pure allocation
algorithms**, and replaces all BSD VFS plumbing with the MINIX fsdriver + lmfs
framework, modeled file-for-file on `minix/fs/ext2`.

Scope: **UFS2 only, native little-endian, read-write.**

## Components

| Path | Role |
|------|------|
| `minix/fs/ffs/` | The file-system server (~4,500 lines, 26 files) |
| `minix/sbin/newfs_ffs/` | Create a UFS2 file system (~570 lines) |
| `minix/sbin/fsck_ffs/` | Read-only consistency checker (~220 lines) |
| `minix/fs/ffs/test/` | Host-side regression harness (developer tool, not built) |

Build/sets wiring: `minix/fs/Makefile` (`SUBDIR+=ffs`),
`minix/sbin/Makefile` (`SUBDIR+=newfs_ffs fsck_ffs`), and
`distrib/sets/lists/minix-base/mi` (`./service/ffs`, `./sbin/newfs_ffs`,
`./sbin/fsck_ffs`).

`mount -t ffs <dev> <dir>` works with **no `mount_ffs` binary**: MINIX libc
`minix_mount()` execs `/service/<type>`, so installing `/service/ffs` is the
entire mount mechanism.

## On-disk format

The on-disk definitions are vendored, trimmed to UFS2 / native-LE, into
`minix/fs/ffs/ffs_disk.h` (from `sys/ufs/ffs/fs.h`, `sys/ufs/ufs/dinode.h`,
`sys/ufs/ufs/dir.h`). The layout was verified to match NetBSD/amd64 (LP64):

| Structure | Size / offset |
|-----------|---------------|
| `struct fs` | 1376 bytes; `fs_magic` @ 1372; `fs_sblockloc` @ 1000; `fs_cstotal` @ 1008 |
| `struct ufs2_dinode` | 256 bytes; `di_db` @ 112; `di_ib` @ 208 |
| `struct direct` | variable; entries never cross a 512-byte (`UFS_DIRBLKSIZ`) boundary |

The in-core inode embeds the on-disk dinode directly (`rip->i_din.di_*`); no
field-by-field byte swapping is needed because the server is native-LE.

## Central design decision: fragments vs. a fixed cache block size

`lmfs` uses a single fixed block size per mount, but UFS splits its large block
(`fs_bsize`) into `fs_frag` fragments (`fs_fsize`); the tail block of a file and
all cylinder-group bitmaps are fragment-granular.

The server sets the lmfs cache block size to the **fragment** size:

```c
lmfs_set_blocksize(fs->fs_fsize);
```

Consequences:

- All on-disk addresses (`di_db[]`, `di_ib[]`, indirect-block entries, cg
  bitmaps) are fragment numbers that map **1:1** onto lmfs cache block numbers
  (`block64_t`).
- A full file-system block occupies `fs_frag` consecutive cache blocks.
- An indirect block (`fs_bsize`) is read one fragment at a time
  (`read.c:indir_get`, `write.c:read_indir_entry`).
- Fragments occur **only in the direct-block range**: `ffs_blksize()` returns a
  full `bsize` for every `lbn >= UFS_NDADDR`, so indirect-addressed data blocks
  and all metadata blocks are always whole blocks. This is what keeps
  `ffs_balloc` tractable.

The VM second-level (inode-keyed) cache is **disabled**
(`lmfs_may_use_vmcache(0)`): fragment relocation in `ffs_realloccg` moves data
between device blocks, and device-block-keyed caching keeps that coherent
without per-block VM invalidation.

## Allocator

`balloc.c` is a self-contained port of the on-disk semantics of NetBSD's
`ffs_alloc.c`. Cylinder groups are read into and written back from a contiguous
`fs_bsize` bounce buffer assembled out of the `fs_frag` cache blocks
(`ffs_read_cg` / `ffs_write_cg`). Fragment accounting (`cg_frsum`, `cg_cs`,
`fs_cstotal`, `fs_csp[]`) is maintained exactly as the original FFS, using the
`fragtbl` / `around` / `inside` tables and `ffs_fragacct`, so images stay
fsck-clean. `ialloc.c` ports inode allocation including the UFS2 lazy inode-block
initialization (`cg_initediblk`).

One subtle, important predicate:

```c
/* ffs_isblock(cp,h)     -> cp[h]==0xff : block is already FREE (allocatable) */
/* ffs_isfreeblock(cp,h) -> cp[h]==0    : block is fully ALLOCATED            */
```

The whole-block double-free guard in `ffs_blkfree` must use **`ffs_isblock`**
(already-free). Using `ffs_isfreeblock` there inverts the test and silently
skips the free of every directory's block — a leak that the host harness caught.

## Superblock location

`read_super` performs the standard `SBLOCKSEARCH` (65536 → 8192 → 0 → 256K) and
accepts **both** `FS_UFS2_MAGIC` (0x19540119) and `FS_UFS2EA_MAGIC`
(0x19012038), validating `fs_sblockloc == offset`; `write_super` writes back to
the discovered offset.

Real-world gotcha discovered by testing actual images:

| Tool | Superblock offset | Magic |
|------|-------------------|-------|
| `nbmakefs -t ffs -o version=2` | **8192** | UFS2EA (0x19012038) |
| standard `newfs` / our `newfs_ffs` | 65536 | UFS2 (0x19540119) |

The server therefore must not hardcode the offset or a single magic.

## newfs_ffs

A focused port of the geometry / superblock / cylinder-group logic from
`usr.sbin/makefs/ffs/mkfs.c`, restricted to native-LE UFS2 with no rotational /
cluster optimization (`fs_contigsumsize == 0`). Unlike makefs it writes directly
to the device and creates the root directory itself (makefs delegates directory
creation to a higher layer). It writes a standard superblock at 65536 with the
plain UFS2 magic.

## fsck_ffs

A **read-only** checker (like `fsck -n`, no repair): validates superblock
geometry, then for every cylinder group recomputes free-block / free-fragment /
free-inode counts directly from the bitmaps and cross-checks them against
`cg_cs`, the `fs_cs` summary array, and `fs_cstotal`; it also sanity-checks the
root inode. Exit code 4 on inconsistency. A full pass1–4 repairing fsck is left
as future work.

## Validation

The server has not yet been exercised on a running MINIX (the x86_64 port now
boots to a multiuser login, but the FFS server has not been mounted there). It
is instead validated on the build host: `minix/fs/ffs/test/` compiles the
**real, unmodified server source files** on the build host against a small
lmfs / bdev / fsdriver shim (`compat/`, `shim.c`, `globals.c`) and runs the
actual VFS-FS handlers against a real UFS2 image file. Run with `sh run.sh`.

The test cross-validates the whole stack:

1. `newfs_ffs` creates an image → the server does create / multi-fragment write /
   mkdir / nested file / unlink / rmdir → free blocks and inodes reclaim exactly
   → `fsck_ffs` reports clean.
2. The server writes content (no deletes) → `fsck_ffs` stays clean (the server
   keeps the file system fsck-consistent).
3. The server reads a reference `nbmakefs` image (the 8192 / UFS2EA case).
4. Negative test: corrupting `fs_cstotal` is detected by `fsck_ffs`.

Read-path internals were additionally checked with an independent reader
(`test/ufs2dump.c`) that uses only the on-disk header.

## Bugs found and fixed during the port

- Two directory link-count double-decrements (rmdir, cross-directory rename) —
  `unlink_file("..")` already adjusts the parent.
- Stale data exposed when a tail fragment is grown to a full block — the grown
  region must be zeroed (`write.c:zero_block_range`).
- 64-bit `lastblock` truncated to `int` in `truncate_inode`.
- Superblock `s_csp` mmap leaked on mount failure / across mounts.
- The inverted `ffs_isblock` / `ffs_isfreeblock` predicate in `ffs_blkfree`
  (directory-block leak) — found by the host harness.

## Limitations / future work

- UFS2 only; no UFS1, no opposite-endian images, no extended attributes,
  journaling (WAPBL), snapshots, quotas, or dirhash.
- New directories are allocated a full block (simple and fsck-clean, but uses
  more space than a fragment-sized directory).
- `fsck_ffs` checks but does not repair.
- Not yet exercised on a running MINIX (the x86_64 port boots to a multiuser
  login, but the FFS server has not been mounted there); all validation to date
  is via the host harness.
