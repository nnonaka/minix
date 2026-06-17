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
- `.if defined(__MINIX)` does **not** work in Makefiles processed by `nbmake` during the tools/build phase — `__MINIX` is a C preprocessor symbol, not a make variable, so it is never defined in make variable scope
- Use unconditional `?=` defaults instead (e.g. `USE_FILEMON?= no`), which can still be overridden from the command line
- Reserve `#ifdef __minix` / `#ifdef __MINIX` guards for C/C++ source and header files only

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

## VM pagetable PTF flags
- `PTF_ALLFLAGS` in `minix/servers/vm/arch/x86_64/pagetable.h` must include every PTF_ flag callers may pass — `assert(!(flags & ~PTF_ALLFLAGS))` in `pt_writemap` rejects unknown flags at runtime
- When widening `pt_writemap` / `pt_ptalloc_in_range` flags to `u64_t`, cast `(u32_t)flags` in the i386/arm branch to silence truncation warnings (all i386 PTF_ values fit in 32 bits)

## Distribution sets
- New arch port: create `distrib/sets/lists/minix-base/md.<machine>`, `minix-comp/md.<machine>`, `minix-debug/md.<machine>`; `md.i386` is the reference in each
- Set file format: `./path  set-name  [tag]`; entries with a tag (e.g. `binutils`, `nls`, `llvmcmds`) are only included in the flist when the corresponding `MK*` var is in `MKEXTRAVARS` in `distrib/sets/mkvars.mk` AND is not "no"
- `mkvars.mk` `MKEXTRAVARS` missing by default: `MKBINUTILS`, `MKLLVM`, `MKLLVMCMDS`, `MKNLS` — without them ldscripts/clang-headers/locale are silently omitted from the flist
- MINIX sets (`minix-base` etc.) do not cover all NetBSD files installed to DESTDIR; use `SLOPPY_FLIST=YES` (allow extras, keep missing fatal) — already set in root Makefile checkflist call
- `makeflist` manual test: `MACHINE=amd64 MACHINE_ARCH=x86_64 MACHINE_CPU=x86_64 ./makeflist -L base,x` from `distrib/sets/`; note MK* tag filtering only works correctly when nbmake provides the environment (not plain sh)
