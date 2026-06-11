# MINIX dev-efi3 — Claude notes

## Build
- `nbmake-amd64` builds x86_64 MINIX; `nbmake-i386` for i386
- Architecture naming split: kernel/headers use `amd64` (sys/arch/amd64/), libc/minix use `x86_64` (minix/include/arch/x86_64/, minix/lib/libc/arch/x86_64/)

## Assembly (PIC)
- Every `PIC_PLT()` call in a .S file must be wrapped with `PIC_PROLOGUE` / `PIC_EPILOGUE` — no exceptions

## ucontext / libmthread
- `TOOL_GENASSYM` generates struct-offset headers from `.cf` files; i386 Makefile.inc is the reference pattern
- Register indices for amd64: `sys/arch/amd64/include/frame_regs.h` — gregs[0..5] = RDI/RSI/RDX/RCX/R8/R9 (SysV AMD64 ABI order)
- MINIX fast path: `_UC_IGNFPU | _UC_IGNSIGM` flags skip kernel round-trip for signal mask + FPU; libmthread always sets both
- `mc_magic = MCF_MAGIC (0xc0ffee)` is a MINIX-only validity guard in `mcontext_t`; `setcontext` and `makecontext` both check it
- libmthread arch guards use `#if defined(__i386__) || defined(__arm__)` — add `|| defined(__x86_64__)` to extend to amd64

## Headers / destdir
- `minix/include/arch/x86_64/include/Makefile` must use `INCSDIR= /usr/include/amd64` (not i386); `machine` → `amd64` symlink in destdir
- After changing arch include Makefiles, reinstall with: `$TOOLDIR/bin/nbmake-amd64 -C minix/include/arch/x86_64/include includes`

## Clang
- x86_64-*-minix triple must be added manually in `external/bsd/llvm/dist/clang/lib/Basic/Targets.cpp`
