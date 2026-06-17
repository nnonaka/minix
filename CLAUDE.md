# MINIX dev-efi3 — Claude notes

## Build
- `nbmake-amd64` builds x86_64 MINIX; `nbmake-i386` for i386
- Architecture naming split: kernel/headers use `amd64` (sys/arch/amd64/), libc/minix use `x86_64` (minix/include/arch/x86_64/, minix/lib/libc/arch/x86_64/)

## Assembly (PIC)
- i386 .S: every `PIC_PLT()` call must be wrapped with `PIC_PROLOGUE` / `PIC_EPILOGUE`
- x86_64 .S: `PIC_PROLOGUE`/`PIC_EPILOGUE` are invalid (i386-only GOT setup); use `PIC_PLT(sym)` bare — no wrappers

## ucontext / libmthread
- `TOOL_GENASSYM` generates struct-offset headers from `.cf` files; i386 Makefile.inc is the reference pattern
- Register indices for amd64: `sys/arch/amd64/include/frame_regs.h` — gregs[0..5] = RDI/RSI/RDX/RCX/R8/R9 (SysV AMD64 ABI order)
- MINIX fast path: `_UC_IGNFPU | _UC_IGNSIGM` flags skip kernel round-trip for signal mask + FPU; libmthread always sets both
- `mc_magic = MCF_MAGIC (0xc0ffee)` is a MINIX-only validity guard in `mcontext_t`; `setcontext` and `makecontext` both check it
- libmthread arch guards use `#if defined(__i386__) || defined(__arm__)` — add `|| defined(__x86_64__)` to extend to amd64

## Headers / destdir
- `minix/include/arch/x86_64/include/Makefile` must use `INCSDIR= /usr/include/amd64` (not i386); `machine` → `amd64` symlink in destdir
- Reinstall MINIX arch headers: `$TOOLDIR/bin/nbmake-amd64 -C minix/include/arch/x86_64/include includes`
- Reinstall NetBSD amd64 system headers: `$TOOLDIR/bin/nbmake-amd64 -C sys/arch/amd64/include includes`
- `minix/include/arch/x86_64/include/fpu.h` gets installed to `amd64/fpu.h`, overwriting `sys/arch/amd64/include/fpu.h`; keep MINIX additions in the sys/ file and reinstall after edits

## libminc
- `minix/lib/libminc/Makefile` adds `-D_LIBMINC` to all files; strtoul.c and strtol.c also get `-D_STANDALONE`
- Headers with MINIX-specific types must use `(defined(_LIBMINC) || !defined(_STANDALONE))` — not just `!defined(_STANDALONE)` — to match `signal.h`'s pattern and work under the libminc combination

## pthread on MINIX (OpenSolaris/CDDL ports)
MINIX has no `pthread.h`. Use mthread with the pthread-compat layer instead:
```c
#ifdef __minix
#define _MTHREADIFY_PTHREADS
#include <minix/mthread.h>
#ifndef PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED MTHREAD_CREATE_DETACHED
#endif
#else
#include <pthread.h>
#ifndef __NetBSD__
#include <pthread_np.h>   /* omit if not needed */
#endif
#endif /* !__minix */
```
- `_MTHREADIFY_PTHREADS` must be defined **before** `#include <minix/mthread.h>`; it activates `pthread_t`, `pthread_mutex_t`, `pthread_cond_t`, `pthread_rwlock_t`, and all `pthread_*` function aliases
- `PTHREAD_CREATE_DETACHED` is not in the compat block; add it explicitly when needed
- `pthread_mutex_isowned_np` (NetBSD NP extension) has no mthread equivalent; stub to `(1)` under `#ifdef __minix`
- Files already pulling in `thread.h` (which does the above) need only guard bare `#include <pthread.h>` with `#ifndef __minix`
- `tsd_get/set/create` (`pthread_getspecific/setspecific/key_create`) work as-is once `_MTHREADIFY_PTHREADS` is active

## sys/mbuf.h on MINIX
- MINIX does not support mbuf; `paddr_t` in `sys/mbuf.h` is only defined under `_KERNEL` on amd64, causing "unknown type name 'paddr_t'" in userspace
- Fix: wrap `#include <sys/mbuf.h>` with `#ifndef __minix` / `#endif` — the files typically don't use mbuf types directly

## paddr_t in wscons / userspace drivers
- On amd64, `machine/types.h` defines `paddr_t` only under `_KERNEL || _KMEMUSER || _KERNTYPES || _STANDALONE`; MINIX userspace drivers get nothing
- Fix pattern for headers needing `paddr_t`: `#include <machine/types.h>` (guarded by `!defined(_I386_MACHTYPES_H_) && !defined(_X86_64_TYPES_H_)`), then add amd64 userspace fallback `typedef unsigned long paddr_t` under `defined(__x86_64__) && !defined(_KERNEL) && !defined(_KMEMUSER) && !defined(_KERNTYPES) && !defined(_STANDALONE)`
- i386 userspace always gets `paddr_t` as `__uint64_t` from `machine/types.h` (no fallback needed)

## ZFS / osnet
- `MKZFS` defaults to `yes` for `MACHINE == "amd64"` in `share/mk/bsd.own.mk`; guarded with `!defined(__MINIX)` — MINIX does not support ZFS
- All `external/cddl/osnet/` lib builds are gated on `MKZFS != "no"`; no MINIX code depends on them

## binutils x86_64 config.h
- All five `arch/x86_64/config.h` files (libbfd, libopcodes, common, gas, ld) had `ENABLE_NLS 1`; changed to `/* #undef ENABLE_NLS */` — MINIX has no gettext/`dgettext`

## MINIX libc missing syscall stubs
- Missing NetBSD syscalls are listed in `minix/lib/libc/sys/MISSING_SYSCALLS`
- Add no-op or minimal stubs to `minix/lib/libc/sys/` and wire into `minix/lib/libc/sys/Makefile.inc`
- `_lwp_setprivate` (TLS %fs base): stubbed as no-op; full impl needs kernel FSGSBASE support (`CR4.FSGSBASE=1` + `wrfsbase`) — not yet in MINIX kernel
- `__msync13` (msync renamed by `sys/mman.h`): no-op — MINIX mmap does not cache writes

## Makefiles — MINIX guards
- `__MINIX` is a **make variable** (`__MINIX= yes`) set in `share/mk/sys.mk`; `.if defined(__MINIX)` works correctly in normal nbmake builds
- Exception: Makefiles processed during the **tools/build phase** (before `sys.mk` is available) do not have `__MINIX` defined — use unconditional `?=` defaults (e.g. `USE_FILEMON?= no`) in those Makefiles
- C preprocessor MINIX guards use lowercase: `#ifdef __minix` (not `__MINIX`)

## x86_64 kernel compilation
- Kernel lives at `0xFFFFFFFF80400000`; all objects linked into it need `CFLAGS += -mcmodel=kernel -mno-red-zone` (in `minix/kernel/arch/x86_64/Makefile.inc`)
- Pre-built library archives (`-lsys`, `-lexec`, `-lminc`) use `-mcmodel=small` → `R_X86_64_32` overflows at link time; fix: compile fresh copies in the kernel build and add to `OBJS.kernel` so they shadow the archives (see `Makefile.inc` OBJS.kernel block)
- After changing `CFLAGS` in `Makefile.inc`, run `nbmake-amd64 -C minix/kernel cleandir` before rebuilding — stale cached `.o` files from the old model cause phantom relocation errors
- `phys_bytes` is `u32_t` (32-bit); use `vir_bytes` (`unsigned long`, 64-bit) for kernel virtual addresses like `_kern_vir_base = 0xFFFFFFFF80400000`

## Kernel config macros
- Use `#if CONFIG_FOO` (not `#ifdef CONFIG_FOO`) when the macro may be defined as `0` — `#ifdef` is truthy even for `CONFIG_FOO=0` and causes calls to guarded-away functions

## Clang
- x86_64-*-minix triple must be added manually in `external/bsd/llvm/dist/clang/lib/Basic/Targets.cpp`

## IPC message structs (x86_64)
- `_ASSERT_MSG_SIZE` is a no-op on x86_64 (see `minix/include/arch/x86_64/include/ipcconst.h`); enforced at 56 bytes only on i386; `_MSG_PAYLOAD_SIZE` = 88 on x86_64
- Design cross-arch structs to be exactly 56 bytes on i386; they will fit within the 88-byte x86_64 payload automatically
- `uint64_t` has align 4 on i386 and align 8 on x86_64 — a struct with `uint64_t`+`uint32_t`+`uint32_t`+`padding[40]` is 56 bytes on both

## PCI BAR addresses
- `pci_get_bar()` signature: `u64_t *base, u32_t *size` — callers must declare `u64_t base` (not `u32_t`)
- `pb_base` in `bus/pci/pci.c` struct is now `u64_t`; `complete_bars()` skips BARs with `pb_base > 0xFFFFFFFFULL`
- `PBF_INCOMPLETE` check must test the full 64-bit base: `(bar | ((u64_t)bar_high << 32)) == 0` — checking only `bar == 0` wrongly marks a BAR at an exact 4 GB multiple (bar=0, bar_high≠0) as unallocated

## Format strings — dev_t / ino_t / off_t (cross-arch)
- `dev_t` and `ino_t` are `uint64_t` = `unsigned long long` on i386, `unsigned long` on amd64
- `off_t` (`__off_t`) is `long long` on both arches
- Safe portable pattern: use `%llu`/`%llx` with `(unsigned long long)` cast, or `%lld` with `(long long)` cast — never bare `%lu`/`%ld` for these types

## MMIO driver base widening (LP64)
- Full chain: struct `u32_t base[6]` → `vir_bytes base[6]`; `u32_t *base` params → `vir_bytes *base`; `u32_t base0 = base[0]` → `vir_bytes base0 = base[0]`; `io.h` `my_in*/my_out*` port params `u32_t` → `vir_bytes`; cast `(u32_t)reg` → `(vir_bytes)reg`
- Audio drivers (als4000, cmi8738, cs4281, trident) share identical io.h structure — all four widened; ip1000 and vt6105 also widened
- When widening audio driver `.c` functions, also update: `.h` declarations (`dev_mixer_read`/`dev_mixer_write`) and `mixer.c` definitions (`get_set_volume`, `dev_set_default_volume`) — three files per driver

## phys_bytes vs u32_t pointer mismatch
- `phys_bytes` is `unsigned long`; `u32_t` is `unsigned int` — same 32-bit size on i386 but different types; `&u32_t_var` passed to `phys_bytes *` gives `-Wincompatible-pointer-types`
- Declare variables as `phys_bytes` (not `u32_t`) when passing to system APIs like `sys_vmctl_get_pdbr()`

## EFI linker script (MINIX)
- `elf_x86_64_minix_efi.lds` uses `ImageBase = 0`; do NOT add `FILEHDR PHDRS` to a PT_LOAD — linker tries to fit ELF headers before offset 0 and fails with "not enough room for program headers"
- Correct PHDRS block: `{ text PT_LOAD; data PT_LOAD; dyn PT_DYNAMIC; }` — suppresses auto-PT_PHDR (no "PHDR segment not covered" warning) without conflicting with offset 0

## EFI bootloader (sys/stand/efiboot)
- Multiboot2 entry tag selection on EFI64: prefer `mpp_entry_elf64` (type-9, `MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS_EFI64`) over `mpp_entry` (type-3) under `#ifdef __LP64__` — type-3 points to `multiboot_entry32` (32-bit PM code), type-9 to `multiboot_entry64_efi` (64-bit LM code); jumping to 32-bit code in long mode faults immediately
- `efi_gop_found()` returns NULL on headless/GOP-less systems; always null-check before dereferencing `gop->Mode`
- EFI `FreePages(addr, n)`: `n` is a **page count**, not bytes — always use `EFI_SIZE_TO_PAGES(size)`; passing raw bytes frees ~16 MB instead of 1 page
- Block I/O IoAlign allocation: use `blkbuf_size + IoAlign - 1` (not `roundup(blkbuf_size, IoAlign)`) — `roundup2(blkbuf, IoAlign)` advances the start pointer by up to `IoAlign-1` bytes, so the allocation must accommodate both the shift and the full transfer

## VM pagetable PTF flags
- `PTF_ALLFLAGS` in `minix/servers/vm/arch/x86_64/pagetable.h` must include every PTF_ flag callers may pass — `assert(!(flags & ~PTF_ALLFLAGS))` in `pt_writemap` rejects unknown flags at runtime
- When widening `pt_writemap` / `pt_ptalloc_in_range` flags to `u64_t`, cast `(u32_t)flags` in the i386/arm branch to silence truncation warnings (all i386 PTF_ values fit in 32 bits)
- `PTF_NOEXEC` (bit 63 = `AMD64_VM_NX`) must NOT be set in a PDE — a PDE with NX=1 blocks instruction-fetch from all 512 PTEs under it regardless of per-PTE NX; strip it in `pt_ptalloc`: `(u64_t)flags & ~PTF_NOEXEC`
- Full NX propagation chain must be `u64_t` throughout: `pt_flags` callback in `memtype.h`, all six `mem_*.c` implementations, `flags` variable in `region.c:map_ph_writept`, and `kern_mappings[].flags` struct field — any `int` in this chain silently truncates bit 63

## SMP AP startup (x86_64)
- `copy_trampoline()` in `arch_smp.c` copies both GDT and IDT into the trampoline page; the `__ap_idt_tab` source must be `idt` not `gdt` — using `gdt` fills the AP IDT with segment descriptors and causes a triple-fault on the first interrupt on every AP
- `startup_ap_32` in `mpx.S` is 64-bit code despite the name (historical artifact from i386 port); it is called from the `.code64` stub `__ap_startup_64` in `trampoline.S`
- `__ap_jmpvec` is a 6-byte far pointer `{ u32_t phys_addr, u16_t selector }` filled at runtime in real mode; `arch_smp.c:copy_trampoline()` uses `ap_lin_addr()` to pre-fill `__ap_gdt.base` / `__ap_idt.base` but NOT `__ap_jmpvec` — that is computed in 16-bit assembly as `CS*16 + offset`
- x86_64 descriptor sizes differ: segment descriptor `struct segdesc_s` = `DESC_SIZE` (8 bytes, GDT element); long-mode gate descriptor `struct gatedesc_s` = 16 bytes (`GATE_DESC_SIZE`, IDT element); `struct desctableptr_s` = 10 bytes packed (u16 limit + u64 base). Trampoline `.space` reservations must use the right one — `__ap_idt_tab` needs `IDT_SIZE*GATE_DESC_SIZE`, not `*DESC_SIZE`
- `cpuid` macro (`arch/x86_64/include/arch_smp.h`) must use `(vir_bytes)` for the stack arithmetic and read a `reg_t` (`[-1]` on a `reg_t *`), not i386's `u32_t` — cpu id is stored as a full `reg_t` at `kernel_stack_top - sizeof(reg_t)` by `tss_init`; the `u32_t` version truncates the kernel stack VA and reads the zero high half
- Any kernel pointer (`struct proc *` etc.) stored in a struct or passed between CPUs must be `vir_bytes`/`uintptr_t`, never `u32_t` (e.g. `sched_ipi_data.data` in `smp.c`) — these SMP bugs are latent under UP (`#define cpuid 0`, no scheduling IPIs)

## ACPI XSDT (x86_64)
- UEFI x86_64 is ACPI revision 2: the RSDP points at an **XSDT** with 64-bit table addresses (vs the legacy 32-bit **RSDT**); `acpi.c` selects the branch on `acpi_rsdp.revision` (0 = RSDT, 2 = XSDT)
- `phys_bytes` is `unsigned long` (64-bit) on amd64 — keep XSDT table addresses full-width; the old `rsdt.data[i] = (u32_t)xsdt.data[i]` copy silently truncated tables placed above 4 GB. Resolved address is stored in `sdt_trans[i].base` (`phys_bytes`), the single source of truth returned by `acpi_get_table_base()`; `acpi_phys2vir` takes/returns `phys_bytes`
- `sdt_count` is bounded by `MAX_RSDT` (35) because `acpi_read_sdt_at()` rejects an XSDT longer than the `xsdt.data[MAX_RSDT]` read buffer
- The RSDP reaches the kernel via the multiboot2 ACPI2 tag (`kinfo.rsdp_p`, `kinfo.mb_version == 2`) on the EFI path — the legacy EBDA/`0xE0000`–`0x100000` BIOS scan finds nothing under pure UEFI
- Caveat: pre-VM, `acpi_phys_copy` reads a phys address *as* virtual via the boot identity/direct map; a table >4 GB also needs that range mapped to be reachable — firmware keeps ACPI tables <4 GB on QEMU/OVMF so this works today
- `mb2_acpi2.c` (standalone experimental `acpi2_init()`) was unused (not in `Makefile.inc`, never called) and is deleted — don't resurrect it; `acpi.c` is the one wired into APIC/SMP/poweroff
- Details: `docs/apic-x86_64.md` (ACPI feeds MADT/APIC discovery)

## FFS / UFS2 filesystem (minix/fs/ffs, minix/sbin/{newfs,fsck}_ffs)
- Port of NetBSD `sys/ufs/ffs` as a MINIX userspace FS server modeled on `minix/fs/ext2` (libfsdriver `fsdriver_task` + libminixfs `lmfs_*`), NOT a build of the kernel `sys/ufs` code; on-disk format is **UFS2 only, native little-endian**
- On-disk layout vendored, trimmed, into `minix/fs/ffs/ffs_disk.h` (struct fs/cg/ufs2_dinode/direct + macros from `sys/ufs/{ffs/fs.h,ufs/dinode.h,ufs/dir.h}`); verified against NetBSD/amd64: `sizeof(struct fs)`=1376 (`fs_magic`@1372), `ufs2_dinode`=256 (`di_db`@112, `di_ib`@208), `fs_sblockloc`@1000
- **Fragment = lmfs cache block**: `lmfs_set_blocksize(fs->fs_fsize)`; UFS frag/block/cg-bitmap addresses map 1:1 onto cache block numbers (`block64_t`); a full block = `fs_frag` consecutive cache blocks; indirect blocks (`fs_bsize`) are read one fragment at a time
- Fragments occur **only in the direct-block range**: `ffs_blksize` returns full `bsize` for every `lbn >= UFS_NDADDR`, so all indirect-addressed and metadata blocks are whole blocks; only the last *direct* block can be a fragment (this keeps `ffs_balloc` tractable)
- VM second-level cache is **disabled** (`lmfs_may_use_vmcache(0)`): fragment relocation in `ffs_realloccg` moves data between device blocks; device-block-keyed caching stays coherent without per-block VM invalidation
- Cylinder groups are read/modified/written through a contiguous malloc'd `fs_bsize` bounce buffer assembled from the `fs_frag` cache blocks (`ffs_read_cg`/`ffs_write_cg` in balloc.c); fragment accounting (`cg_frsum`/`cg_cs`/`fs_cstotal`/`fs_csp[]`) maintained exactly as NetBSD via the `fragtbl`/`around`/`inside` tables + `ffs_fragacct` so images stay fsck-clean
- `read_super` must do the full **`SBLOCKSEARCH`** (65536 → 8192 → 0 → 256K) and accept **both** `FS_UFS2_MAGIC` (0x19540119) and `FS_UFS2EA_MAGIC` (0x19012038), validating `fs_sblockloc == offset`; `write_super` writes back to the discovered offset. Real-world gotcha: `nbmakefs -t ffs -o version=2` writes the superblock at **8192** with the **UFS2EA** magic, while standard `newfs` uses 65536 / plain UFS2 (our `newfs_ffs` does the latter)
- `ffs_isblock` (`cp[h]==0xff`, "already free") vs `ffs_isfreeblock` (`cp[h]==0`, "fully allocated") have *opposite* meaning — the whole-block double-free guard in `ffs_blkfree` must use `ffs_isblock`; using `ffs_isfreeblock` there silently skips every directory's block free (leak)
- Directories are grown one full `fs_bsize` block at a time, subdivided into empty `UFS_DIRBLKSIZ` (512-byte) entry chunks; entry add/delete in `path.c:search_dir` and `read.c:fs_getdents` iterate per-DIRBLKSIZ so no `direct` ever crosses a 512-byte boundary (fsck requirement)
- `mount -t ffs` needs no `mount_ffs` binary: MINIX libc `minix_mount` execs `/service/ffs` (set entry `./service/ffs`); the server binary is the whole mount mechanism
- **Host regression test**: `minix/fs/ffs/test/` compiles the real server `.c` files against a shim (`compat/`, `shim.c`) over an image file and exercises the actual read/write path on the build host (no MINIX boot needed) — `sh run.sh`. This is how the server, `newfs_ffs` and `fsck_ffs` were validated and how the `ffs_isblock` leak was found
- `fsck_ffs` is a **read-only** consistency checker (no repair): superblock geometry + per-cg bitmap recount vs `cg_cs`/`fs_cs`/`fs_cstotal` + root inode sanity; exits 4 on inconsistency
- Details: `docs/ffs-ufs2-port.md`

## Port documentation (docs/)
- Per-subsystem x86_64 port notes live in `docs/*-x86_64*.md` (e.g. `kernel-arch-x86_64.md`, `apic-x86_64.md`, `vm-x86_64-port.md`, `kernel-build-x86_64-fixes.md`); add new kernel/SMP findings to `docs/kernel-arch-x86_64.md`, matching its before/after code-block style

## /code-review on this repo
- No GitHub PRs exist (`origin` = github.com/nnonaka/minix); review the diff against base branch `uefiboot` (= `origin/uefiboot`), or review a subsystem holistically when the topic is broader than the diff
- Local `HEAD` is typically NOT pushed — for GitHub citation links use the latest pushed SHA (`git branch -r --contains <sha>` to confirm; usually `origin/uefiboot`), not local HEAD, or links 404

## Distribution sets
- New arch port: create `distrib/sets/lists/minix-base/md.<machine>`, `minix-comp/md.<machine>`, `minix-debug/md.<machine>`; `md.i386` is the reference in each
- Set file format: `./path  set-name  [tag]`; entries with a tag (e.g. `binutils`, `nls`, `llvmcmds`) are only included in the flist when the corresponding `MK*` var is in `MKEXTRAVARS` in `distrib/sets/mkvars.mk` AND is not "no"
- `mkvars.mk` `MKEXTRAVARS` missing by default: `MKBINUTILS`, `MKLLVM`, `MKLLVMCMDS`, `MKNLS` — without them ldscripts/clang-headers/locale are silently omitted from the flist
- MINIX sets (`minix-base` etc.) do not cover all NetBSD files installed to DESTDIR; use `SLOPPY_FLIST=YES` (allow extras, keep missing fatal) — already set in root Makefile checkflist call
- `makeflist` manual test: `MACHINE=amd64 MACHINE_ARCH=x86_64 MACHINE_CPU=x86_64 ./makeflist -L base,x` from `distrib/sets/`; note MK* tag filtering only works correctly when nbmake provides the environment (not plain sh)
