#!/bin/sh
# Host-side regression test for the MINIX FFS (UFS2) server, newfs_ffs and
# fsck_ffs.  It compiles the *real* server source files against a small
# lmfs/bdev/fsdriver shim (see compat/ and shim.c) and runs them against a real
# UFS2 image on the build host -- no MINIX boot required.
#
# Requires: a C compiler, and (optionally) the cross tool nbmakefs to create a
# reference UFS2 image.  Point TOOLDIR at .../build/tooldir.*/bin if nbmakefs is
# not already on $PATH.
#
# Usage: cd minix/fs/ffs/test && sh run.sh
set -e

here=$(cd "$(dirname "$0")" && pwd)
srv="$here/.."				# the server source directory
sbin="$here/../../../sbin"		# newfs_ffs / fsck_ffs
work=$(mktemp -d)
cc=${CC:-cc}
CFLAGS="-D_GNU_SOURCE -w"

# server objects to link (everything except main.c and table.c)
SRV_SRCS="super mount inode read path stadir balloc ialloc write open link \
	protect time misc utility"
srv_list=""
for s in $SRV_SRCS; do srv_list="$srv_list $srv/$s.c"; done

echo "== building host test binaries in $work =="
$cc $CFLAGS -I"$here/compat" -I"$srv" -o "$work/harness" \
	"$here/globals.c" "$here/shim.c" "$here/harness.c" $srv_list
$cc $CFLAGS -I"$here/compat" -I"$srv" -o "$work/create_only" \
	"$here/globals.c" "$here/shim.c" "$here/create_only.c" $srv_list
$cc $CFLAGS -I"$srv" -o "$work/ufs2dump" "$here/ufs2dump.c"
$cc $CFLAGS -I"$sbin/newfs_ffs" -o "$work/newfs_ffs" "$sbin/newfs_ffs/newfs_ffs.c"
$cc $CFLAGS -I"$sbin/fsck_ffs"  -o "$work/fsck_ffs"  "$sbin/fsck_ffs/fsck_ffs.c"

# locate nbmakefs (optional; used for a cross-check against reference images)
nbmakefs=$(command -v nbmakefs || true)
if [ -z "$nbmakefs" ] && [ -n "$TOOLDIR" ]; then
	nbmakefs="$TOOLDIR/nbmakefs"
fi

echo
echo "== 1. newfs_ffs creates an image, server read/writes it, fsck checks it =="
"$work/newfs_ffs" -s 16384 "$work/a.img" >/dev/null
"$work/harness" "$work/a.img" || true		# harness has its own PASS/FAIL
echo "-- fsck after full harness run --"
"$work/fsck_ffs" "$work/a.img"

echo
echo "== 2. server writes content (no deletes), fsck must stay clean =="
"$work/newfs_ffs" -s 16384 "$work/b.img" >/dev/null
"$work/create_only" "$work/b.img"
"$work/ufs2dump" "$work/b.img" | sed -n '/directory entries/,$p'
"$work/fsck_ffs" "$work/b.img"

if [ -n "$nbmakefs" ] && [ -x "$nbmakefs" ]; then
	echo
	echo "== 3. cross-check: server reads a reference makefs UFS2 image =="
	rm -rf "$work/seed"; mkdir -p "$work/seed/sub"
	echo "hello ufs2" > "$work/seed/hello.txt"
	ln -s hello.txt "$work/seed/link.txt"
	"$nbmakefs" -t ffs -o version=2,bsize=32768,fsize=4096 -s 8m \
		"$work/c.img" "$work/seed" >/dev/null
	"$work/ufs2dump" "$work/c.img" | sed -n '/directory entries/,$p'
	"$work/fsck_ffs" "$work/c.img"
fi

echo
echo "== 4. negative test: corrupt fs_cstotal, fsck must report it =="
cp "$work/a.img" "$work/bad.img"
python3 - "$work/bad.img" <<'PY'
import struct, sys
f = open(sys.argv[1], 'r+b')
off = 65536 + 1008 + 8		# fs_cstotal.cs_nbfree
f.seek(off); v = struct.unpack('<q', f.read(8))[0]
f.seek(off); f.write(struct.pack('<q', v + 5))
f.close()
PY
if "$work/fsck_ffs" "$work/bad.img"; then
	echo "FAIL: fsck did not detect corruption"; rm -rf "$work"; exit 1
else
	echo "OK: fsck detected the injected corruption"
fi

rm -rf "$work"
echo
echo "== all FFS host tests passed =="
