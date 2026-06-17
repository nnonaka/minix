# MINIX dev-efi3 — Claude notes

## Build
- `nbmake-amd64` builds x86_64 MINIX; `nbmake-i386` for i386
- `nbmake-amd64` is NOT on PATH in non-interactive shells and `$TOOLDIR` is unset there; `export TOOLDIR=$(ls -d "$PWD"/../build/tooldir.Linux-*-x86_64)` then run `$TOOLDIR/bin/nbmake-amd64`
- Iterate one component: `$TOOLDIR/bin/nbmake-amd64 -C <dir>` builds, `... -C <dir> install` stages into `build/destdir.amd64` (binary lands in `destdir.amd64/service/<name>` for servers/drivers). Boot modules still need a live-image rebuild to take effect (see QEMU section); standalone `/service` drivers can be swapped into the image
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

## libc build / relink
- The installed `libc.a`/`libc.so` is built from **`lib/libc`** (it pulls `minix/lib/libc/sys` in via `.PATH`); `nbmake-amd64 -C minix/lib/libc` does NOT update it — build `-C lib/libc`
- Statically-linked servers/tools must be rebuilt to pick up a libc change; a live-image repackage alone won't relink them
- `lib/libc/stdlib/malloc.c` (phkmalloc) compiles in two configs: libc, and libminc with `-D_LIBSYS` (→ `MALLOC_NO_SYSCALLS`); edits must build in both — guard syscall/`write`-using code with `#ifndef MALLOC_NO_SYSCALLS`. VM links this malloc statically: never add `write(2)`/IPC to it (a write from inside VM mid-alloc deadlocks VFS)

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
- `phys_bytes` is `unsigned long` (64-bit on amd64); `phys_clicks` is `unsigned int` (32-bit). Use `vir_bytes` for kernel virtual addresses like `_kern_vir_base = 0xFFFFFFFF80400000`. A function returning a page number must return `phys_clicks`/`phys_bytes`, never `int` — an `int` `NO_MEM` return sign-extends into the 64-bit caller and bypasses `== NO_MEM` checks (was the VM `findbit` boot-to-login bug)
- Kernel link needs `-Wl,-z,max-page-size=0x1000` (in `minix/kernel/Makefile`, guarded `MACHINE_ARCH == "x86_64"`) — ld defaults to 2 MB max-page-size for x86-64, padding the first `PT_LOAD` to file offset `0x200000` so the multiboot2 header (in `.unpaged_text`) lands ~2 MB in, outside the EFI loader's 32 KB `MULTIBOOT_SEARCH` → "not a multiboot2 kernel". 4 KB page size puts the header at file offset `0x1008`; section addresses are pinned by `kernel.lds` so only the file offset changes. i386 is unaffected (elf_i386 defaults to 4 KB). Details: `docs/kernel-build-x86_64-fixes.md`
- The info-request tag `size` in `head.S` differs by arch (amd64=28 spec-correct excluding pad, i386=32 includes pad) but is not a boot factor — consumers advance with `roundup(size, 8)`, same next-tag offset either way

## Kernel config macros
- Use `#if CONFIG_FOO` (not `#ifdef CONFIG_FOO`) when the macro may be defined as `0` — `#ifdef` is truthy even for `CONFIG_FOO=0` and causes calls to guarded-away functions

## Clang
- x86_64-*-minix triple must be added manually in `external/bsd/llvm/dist/clang/lib/Basic/Targets.cpp`

## IPC message structs (x86_64)
- `_ASSERT_MSG_SIZE` is a no-op on x86_64 (see `minix/include/arch/x86_64/include/ipcconst.h`); enforced at 56 bytes only on i386; `_MSG_PAYLOAD_SIZE` = 88 on x86_64
- Design cross-arch structs to be exactly 56 bytes on i386; they will fit within the 88-byte x86_64 payload automatically
- `uint64_t` has align 4 on i386 and align 8 on x86_64 — a struct with `uint64_t`+`uint32_t`+`uint32_t`+`padding[40]` is 56 bytes on both

## LP64: addresses through 32-bit message fields (x86_64) — SYSTEMIC
- A 64-bit virtual address with bit 31 set (user addr `0xefffXXXX`; user stack top is `0xF0000000`) stored into a 32-bit `int` field (`m*_i*`) and read back into a `vir_bytes` is **sign-extended into the kernel half** (`0xffffffffefffXXXX`) → rejected as a kernel address
- Use a 64-bit field (`m*_ll1`, `m*ull1`) or a pointer field (`m*_p*`, 64-bit on amd64) for any message macro carrying an address/pointer; audit `m*_i*` macros in `com.h`
- Fixed instances (all `com.h`/`ipc.h`): `VPF_ADDR` `m1_i1`→`m1_ull1`; `SVMCTL_MRG_ADDR` `m2_i2`→`m2_ll1`; `VFS_PM_PS_STR`/`VFS_PM_NEWPS_STR` `m7_i5`→ new pointer field `m7_p3` added to `mess_7` (steal from padding; i386 stays 56 bytes by alignment)
- Adding a field to a `mess_N`: consume `padding[]` so existing field offsets don't move; keep i386 size 56 (`_ASSERT_MSG_SIZE` enforces it on i386 only)

## LP64: libblockdriver iovec — read iov_addr, never iov_grant
- `bdr_transfer` hands the driver an `iovec_t` whose `iov_addr` (`vir_bytes`, 64-bit) holds a **local address** when `endpt==SELF`, else a **grant ID**; `at_wini` is the reference (reads `iov_addr`)
- Casting to `iovec_s_t` and reading `iov_grant` works on i386 (both 4 bytes at offset 0) but on amd64 `iov_grant` is the low 32 bits and `(vir_bytes)(cp_grant_id_t)` **sign-extends** a SELF address with bit 31 set (user space `0x?0000000`–`0xF0000000`) → `0xffffffff…`, failing `sys_vumap`/VM `handle_memory` with EFAULT (was THE ahci root-mount bug; surfaces only on the SELF partition-table read at open)

## LP64: fixed-width hardware descriptors & size-bound buffers
- DMA/hardware descriptor structs read by devices use **fixed 32-bit** address fields — declare them `u32_t`, never `phys_bytes` (8 bytes on amd64 inflates the struct). e.g. at_wini IDE bus-master PRD `struct prdte.prdte_base` must be `u32_t` (entry must stay 8 bytes); guard/bail to PIO for buffers >4GB
- Buffers sized for a `message` or a struct grow on amd64: `message` is 96 bytes (vs 64 i386). VM `vfs.c STATELEN` must be `sizeof(message)` (FDLOOKUP stores a whole message); VM `slaballoc.c SLABSIZES` cap (200→max 207B) was too small for amd64 structs (`vfs_request_node` = 224B) → bump to 256

## Kernel cross-address-space copy / demand paging (x86_64)
- `lin_lin_copy` (`arch/x86_64/memory.c`): `createpde()` returns **0** for a not-present process page; you MUST `return EFAULT_SRC/DST` on `!ptr` so `virtual_copy_f` routes through `vm_suspend` (VM faults the page in). Do NOT rely on `PHYS_COPY_CATCH` — a not-present dst makes the copy touch vaddr 0, whose caught fault addr `0` == the `if(addr)` "no fault" sentinel, so the copy silently writes nowhere. This was THE exec-frame-copy bug (child got `ps_argvstr==0`)
- `createpde` returns `phys_to_kacc(phys)` (=`DM_BASE+phys`, never 0) for present pages, so `!ptr` cleanly means not-present

## exec / process setup (x86_64)
- `arch_proc_init(pr, ip, sp, ps_str, name)` params are `vir_bytes` (were `u32_t` — truncating on LP64); prototype in `proto.h`, defs in all three `arch/*/memory.c`; `do_exec.c` passes `(vir_bytes)` not `(u32_t)`
- VFS computes `ps_str` authoritatively in `pm_exec` (`vsp + frame_len - sizeof(struct ps_strings)`) instead of echoing the caller's value — robust vs stale-libc callers and script/dynamic frame relocation
- libc `stack_utils.c`/`execve.c`: `vsp` is `vir_bytes`; argc cell is pointer-sized so `argv[]` starts `sizeof(char *)` in and `ps_argvstr = vsp + sizeof(char *)`
- amd64 crt0 ELF entry is asm `__start` (`lib/csu/arch/amd64/crt0.S`): `movq %rbx,%rdx; jmp ___start` — kernel passes `ps_strings` in `p_reg.bx` (RBX); the C `___start` reads it as its 3rd arg (RDX)

## VM pt_free / pt_new (x86_64)
- `pt_new` allocates PML4+PDPT once and reuses them (recovers PDPT phys from the kept `pt_pml4[0]`); `pt_free` must therefore **keep `pt_pdpt`** (never free, like `pt_pml4`) and NULL the freed `pt_pd[]`/`pt_pt[]` slots. Freeing `pt_pdpt` while leaving the pointer non-NULL made the next `pt_new` `memset` a freed page → "pagefault in VM" on `VMPPARAM_CLEAR` (exec) slot reuse

## Kernel device-I/O calls (x86_64)
- `SYS_DEVIO`/`VDEVIO`/`SDEVIO`/`IOPENABLE`/`READBIOS` are needed on amd64 (same x86 port I/O as i386); register in `kernel/system.c` and build `do_devio.c`/`do_vdevio.c` in `kernel/system/Makefile.inc` under `__i386__ || __x86_64__`. Missing → "Unused kernel call 21" and tty/driver `sys_inb`/`sys_outb` failures

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
- `efi_md_init()` (copies `multiboot64`/`startprog64` trampolines into allocated memory, sets the function pointers) **must** be called from `efi_main()` (`efiboot.c`) before `boot()` — it was missing, so `multiboot64` stayed at its `.quad 0` init and `(*multiboot64)()` jumped through NULL to address 0 right after ExitBootServices. Latent until the multiboot2 path reached the jump
- Boot bring-up debugging: a raw COM1 (0x3F8) byte writer (LCR=0x03 to clear DLAB, bounded LSR/THRE poll, `outb`) works both before and after ExitBootServices and bypasses the EFI console + kernel console — drop markers in `head.S` (asm macro) / loader (inline asm) to bisect a silent hang; the EFI `printf` is dead after ExitBootServices

## EFI64 boot bring-up (first successful boot, x86_64)
Sequence of faults fixed to get the UEFI/multiboot2 path running (details: `docs/kernel-arch-x86_64.md` "EFI64 boot bring-up", `docs/apic-x86_64.md`):
- **head.S `.code64` does not switch sections**: after the `.data` boot-GDT block, `multiboot_entry64_efi` was emitted into `.unpaged_data`; add an explicit `.text` before the entry
- **Boot identity map**: `multiboot_entry64_efi` must identity-map the low **4 GB** (4 PD pages), not 16 MB — the EFI loader's `AllocateAnyPages` puts the multiboot info struct ~2 GB up, and `get_parameters_mb2()` dereferences it before the full map exists
- **`pg_identity()` (pg_utils.c) must cover ≥ low 4 GB** (`if (num_pds < 4) num_pds = 4`) — it only mapped RAM (`mem_high_phys`), leaving the LAPIC `0xFEE00000` / IOAPIC `0xFEC00000` MMIO hole unmapped → #PF in APIC init after `prot_init` replaces the head.S boot map
- **No LDT on x86_64**: `prot_init` must `x86_lldt(0)` (null LDTR), not load an 8-byte LDT descriptor — a real LDT descriptor is 16-byte (system) in long mode; the 8-byte one #GPs (`lldt`, error = LDT selector 0x28). Flat GDT, no per-process LDT
- **ACPI `struct acpi_xsdt` must be `__packed`**: the 36-byte `acpi_sdt_header` + 8-aligned `u64_t data[]` gets 4 bytes padding (data at offset 40), but the on-disk XSDT packs entries at offset 36 → `memcpy` misaligns every table pointer → #GP (non-canonical) reading `xsdt.data[i]`

## Booting/testing under QEMU (x86_64 UEFI)
- **The user builds the live image and runs QEMU themselves** — do NOT rebuild the live image or launch QEMU. Build/install only the component(s) under change (`nbmake-amd64 -C <dir> [install]`), then hand off to the user to rebuild the image and boot; they paste back the serial/console log. When a fix needs runtime verification, prepare the change and tell the user what to test, rather than driving the boot
- Live image: `build/distrib/amd64/liveimage/emuimage/Minix-3.4.0-x86_64-live.img`; OVMF at `/usr/share/OVMF/OVMF_CODE_4M.fd` + a writable copy of `OVMF_VARS_4M.fd`
- Boot modules (kernel, tty, vm, etc. — see `boot.cfg` `multiboot2`/`load` lines) require rebuilding the live image to take effect; standalone servers/drivers (at_wini, ffs) can be swapped into the image's `/service`
- **q35 has no legacy IDE**: its disk is AHCI/SATA, so `at_wini` (legacy-IDE only, ports 0x1F0/0x170) reads 0x00 status and fails IDENTIFY → root won't mount. Use a legacy-IDE disk (`-machine pc` + `-drive if=ide`) until an AHCI driver exists
- `-serial file:LOG` captures kernel/driver output; `-no-reboot` makes a triple-fault/panic exit QEMU (vs reboot loop)
- Background QEMU via the Bash tool reports "completed" while the VM keeps running → stale procs hold the image write-lock; `pkill -9 qemu-system-x86` and use unique image/serial/vars filenames per run

## Debugging crashes (x86_64)
- Symbolize a panic/coredump stack (`vm 8 0x..`, `kernel on CPU 0: 0x..`, `<prog> <pid> 0x..`) with `$TOOLDIR/bin/x86_64-elf64-minix-addr2line -f -e <binary> <hex...>` — servers/userland are static non-PIE (text at 0x400000) so raw addresses map directly; gives function names even with line info stripped
- Binaries: kernel `build/destdir.amd64/usr/sbin/kernel` (symbols via `nm kernel`, not `kernel.debug`), servers `build/destdir.amd64/service/<name>`, tools in `destdir.amd64/usr/{bin,sbin}`
- Do NOT hand-roll `nm`+`awk` symbolization: awk's doubles lose precision on 64-bit kernel addresses (`0xffffffff8...`)
- LP64 crash signatures: `0xffffffffffffffff`/`...fffe` = a sign-extended `int` used as a 64-bit value (missing prototype, or `int` return into pointer/`phys_bytes`); a small addr like `0x17` = NULL-struct base + field offset
- Read-back-verify catches bad pages at the source: a temporary "write a sentinel, read it back" check in `vm_memset` (kernel) / `vm_allocpages` (VM) / `alloc_pages` pinpointed a non-RAM ROM page and a garbage page number during the boot-to-login corruption hunt

## Console routing (x86_64) — serial vs framebuffer
- `tty/arch/x86_64/console.c scr_init` picks the console backend by `kinfo.boot_mode`: UEFI (1) → `fb_cons_sw` (framebuffer), else `ser_cons_sw`. amd64 has no `bios_console.c` (i386-only, BIOS text mode)
- `etc/ttys`/`etc.amd64/ttys` run getty on `console` (=ttyc0=framebuffer under UEFI), with `tty00` (serial) **off** → **rc/login output goes to the GFX window, not serial**
- Serial only carries **kernel/driver `printf`** (kernel console = `consdev=com0`); a quiet serial after driver init usually means userland is live on the framebuffer, not a hang
- `fb_console.c` has `DUP_CONS_TO_SER=1` (mirrors console→serial); i386 `bios_console.c` has it `0`

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
