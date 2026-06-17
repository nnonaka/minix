# sys/stand/efiboot/bootx64 — x86_64 EFI bootloader build fixes

Documents the changes made to build `bootx64.efi` (the UEFI x86_64 bootloader)
with the MINIX cross-toolchain (Clang 3.6, target `x86_64-elf64-minix`) on the
`dev-efi3` branch.

## Background

`sys/stand/efiboot/bootx64/` is the x86_64 variant of the MINIX/NetBSD EFI
bootloader. It existed in the tree but had never been built with the MINIX
cross-toolchain. The bootloader produces `bootx64.efi`, a PE32+ UEFI application.

## Build overview

The build uses `nbmake-amd64` and the following toolchain:

| Tool | Path |
|------|------|
| Compiler | `x86_64-elf64-minix-clang` (Clang 3.6) |
| Linker | `x86_64-elf64-minix-ld` (GNU ld 2.34) |
| objcopy | `/usr/bin/objcopy` (host — see below) |

The output pipeline: arch source files → object files → `bootx64.efi.so` (ELF
shared object via `elf_x86_64_minix_efi.lds`) → `bootx64.efi` (PE32+ via
`objcopy`).

## Problems and fixes

### 1. Wrong `KLINK_MACHINE` and `LIBKERN_ARCH`

**File:** `sys/stand/efiboot/bootx64/Makefile`

The original values were `x86-64` (a non-existent arch name). This caused
`bsd.klinks.mk` to create a `machine` symlink pointing to
`sys/arch/x86-64/include` (doesn't exist), so `#include <machine/cdefs.h>`
failed immediately.

```makefile
# Before
KLINK_MACHINE=  x86-64
LIBKERN_ARCH=   x86-64

# After
KLINK_MACHINE=  amd64      # sys/arch/amd64/include exists
LIBKERN_ARCH=   x86_64     # sys/lib/libkern/arch/x86_64/ exists
```

`bsd.klinks.mk` with `KLINK_MACHINE=amd64` creates:
- `machine → sys/arch/amd64/include`
- `amd64 → sys/arch/amd64/include`
- `i386 → sys/arch/i386/include` (amd64 also gets this)
- `x86 → sys/arch/x86/include`
- `x86_64 → sys/arch/x86_64/include` (via `MACHINE_CPU=x86_64`)

### 2. Unknown Clang warning flag

**File:** `sys/stand/efiboot/bootx64/Makefile`

`-Wno-error=address-of-packed-member` was added in Clang 4.0; Clang 3.6 rejects
it as an error (with `-Werror`). Changed to `-Wno-unknown-warning-option` so
that any unrecognised warning flag is silently suppressed.

### 3. `efibootx64.c` used wrong include path

**File:** `sys/stand/efiboot/bootx64/efibootx64.c`

`#include "efiboot.h"` failed because `efiboot.h` lives in the parent
`efiboot/` directory and the `-I` path for `${.CURDIR}/../` is not on the
compiler's search path. Fixed to `#include "../efiboot.h"`, matching the
pattern used in `bootaa64/efibootaa64.c`.

### 4. Missing arch-specific function implementations

**Files:** `bootx64/efiboot_machdep.h`, `bootx64/efibootx64.c`

`exec.c` calls `efi_dcache_flush()` and `efi_boot_kernel()`, while
`exec_multiboot2.c` calls `multiboot2()`.  None of these were declared or
implemented for x86_64.

The `bootia32/efiboot_machdep.h` is the reference pattern. Updated
`bootx64/efiboot_machdep.h` to declare:

```c
typedef unsigned long physaddr_t;
void multiboot2(physaddr_t, physaddr_t, uint32_t);
int exec_multiboot2(const char *, const char *);
void *probe_multiboot2(const char *);
void efi_dcache_flush(u_long, u_long);
void efi_boot_kernel(u_long[]);
void efi_md_init(void);
void efi_md_show(void);
```

Implementations added to `efibootx64.c`:

- **`efi_dcache_flush`** — no-op; x86_64 has cache-coherent DMA.
- **`efi_boot_kernel`** — calls the `startprog64` trampoline (which transitions
  from 64-bit EFI mode to 32-bit protected mode before jumping to the NetBSD
  kernel entry).
- **`efi_md_show`** — empty; no CPU info to display at boot prompt.
- **`multiboot2`** — calls through the `multiboot64` function pointer (the
  trampoline allocated by `efi_md_init` from the `multiboot64.S` code).

The old `startprog()` and `multiboot()` functions that referenced undefined
variables (`efi_kernel_start`, `efi_loadaddr`, `efi_kernel_size`) from an
earlier API were removed.

### 5. `DEV_BSIZE` and related constants undefined for x86_64

**File:** `sys/arch/amd64/include/param.h`

Under `#ifdef __x86_64__` (i.e., 64-bit native mode), `DEV_BSIZE`, `DEV_BSHIFT`,
`BLKDEV_IOSIZE`, and `MAXPHYS` were not defined and `i386/param.h` was not
included.  These are needed by `libsa` (`stand.h`, `disklabel.c`, etc.).

Added inside the `#ifdef __x86_64__` block:

```c
#define DEV_BSHIFT      9
#define DEV_BSIZE       (1 << DEV_BSHIFT)
#define BLKDEV_IOSIZE   2048
#ifndef MAXPHYS
#define MAXPHYS         (64 * 1024)
#endif
```

### 6. `<elf.h>` not available under `-nostdinc`

**File:** `sys/external/bsd/gnu-efi/dist/gnuefi/reloc_x86_64.c`

The EFI standalone build uses `-nostdinc` so system headers like `<elf.h>`
(normally in `/usr/include`) are not reachable. All needed types (`Elf64_Dyn`,
`Elf64_Rel`) and macros (`ELF64_R_TYPE`, `DT_NULL`, etc.) are available via
`<sys/exec_elf.h>`.  The x86_64-specific relocation constants (`R_X86_64_NONE`,
`R_X86_64_RELATIVE`) live in `<machine/elf_machdep.h>` (resolved via the
`machine →` symlink created by `bsd.klinks.mk`).

Also added a forward declaration for `_relocate` to satisfy `-Wmissing-prototypes`
(following the pattern in `reloc_aarch64.c`).

```c
/* Before */
#include <elf.h>

/* After */
#if defined(__NetBSD__) || defined(__minix__)
#include <sys/types.h>
#include <sys/exec_elf.h>
#include <machine/elf_machdep.h>
#else
#include <elf.h>
#endif

EFI_STATUS _relocate(long, Elf64_Dyn *, EFI_HANDLE, EFI_SYSTEM_TABLE *);
```

### 7. Missing MINIX-specific EFI linker script for x86_64

**New file:** `sys/external/bsd/gnu-efi/dist/gnuefi/elf_x86_64_minix_efi.lds`

The `x86_64-elf64-minix-ld` linker only understands `elf64-x86-64-minix`
output format; it does not know the generic `elf64-x86-64` format used by the
standard `elf_x86_64_efi.lds`.  Created a MINIX-specific copy with:

```
OUTPUT_FORMAT("elf64-x86-64-minix", "elf64-x86-64-minix", "elf64-x86-64-minix")
```

`Makefile.efiboot`'s linker script selection was extended to cover `x86_64`
alongside `ia32`:

```makefile
.if defined(__MINIX) && (${GNUEFIARCH} == "ia32" || ${GNUEFIARCH} == "x86_64")
LDSCRIPT?= ${EFIDIR}/gnuefi/elf_${GNUEFIARCH}_minix_efi.lds
```

### 8. MINIX cross-objcopy lacks PE/COFF support

**File:** `sys/stand/efiboot/bootx64/Makefile`

`x86_64-elf64-minix-objcopy` was built without PE/COFF (`pei-x86-64`) target
support (the MINIX `config.bfd` entry only includes ELF targets). The host
system's `objcopy` (binutils on the build machine) supports both
`elf64-x86-64-minix` input and `pei-x86-64` output.

Overridden in `bootx64/Makefile` after all BSD `.mk` includes:

```makefile
# MINIX cross-objcopy lacks PE/COFF support; override after bsd includes
.if defined(__MINIX)
OBJCOPY= objcopy
.endif
```

The override must come after `.include "${.CURDIR}/../Makefile.efiboot"` because
`bsd.init.mk` (included from `Makefile.efiboot`) unconditionally sets
`OBJCOPY= ${TOOLDIR}/bin/...`.

The long-term fix is to add `x86_64_pei_vec` and `i386_pei_vec` to the
`x86_64-*-minix*` entry in `external/gpl3/binutils/dist/bfd/config.bfd` and
rebuild the cross tools.

## Files changed

| File | Change |
|------|--------|
| `sys/stand/efiboot/bootx64/Makefile` | Fix `KLINK_MACHINE`, `LIBKERN_ARCH`, warning flag; add `OBJCOPY` override |
| `sys/stand/efiboot/bootx64/efiboot_machdep.h` | Add `physaddr_t`, function declarations |
| `sys/stand/efiboot/bootx64/efibootx64.c` | Fix include path; add `efi_dcache_flush`, `efi_boot_kernel`, `efi_md_show`, `multiboot2` |
| `sys/arch/amd64/include/param.h` | Add `DEV_BSHIFT`, `DEV_BSIZE`, `BLKDEV_IOSIZE`, `MAXPHYS` for x86_64 |
| `sys/external/bsd/gnu-efi/dist/gnuefi/reloc_x86_64.c` | Replace `<elf.h>` with portable headers; add `_relocate` prototype |
| `sys/external/bsd/gnu-efi/dist/gnuefi/elf_x86_64_minix_efi.lds` | New: MINIX-specific EFI linker script |
| `sys/stand/efiboot/Makefile.efiboot` | Extend MINIX linker script selection to x86_64 |

## Known issues / future work

- **PHDR linker warning**: `ld` emits `bootx64.efi.so.tmp: error: PHDR segment
  not covered by LOAD segment` during the link step. This is harmless — the ELF
  `.so` is only an intermediate artefact before `objcopy` converts it to PE/COFF.
  The warning can be suppressed by adding a `PHDRS` command to
  `elf_x86_64_minix_efi.lds`.

- **Cross-objcopy PE support**: The MINIX cross-toolchain `objcopy` should gain
  `pei-x86-64` support. Add `x86_64_pei_vec` (and `i386_pei_vec`) to
  `targ_selvecs` in the `x86_64-*-minix*` stanza of
  `external/gpl3/binutils/dist/bfd/config.bfd`, then rebuild cross tools.

- **`efi_boot_kernel` wrong for native 64-bit kernel** (latent): The
  `efi_boot_kernel` implementation calls `startprog64`, which transitions from
  EFI 64-bit mode → 32-bit protected mode before jumping to the kernel entry
  point.  This is the NetBSD i386-compat path and is incorrect for a native
  64-bit MINIX kernel (which enters directly in long mode via the EFI64 entry
  tag).  The MINIX primary boot path goes through `exec_multiboot2` →
  `multiboot2()` → `multiboot64` (stays in 64-bit mode throughout), so
  `efi_boot_kernel` is not currently exercised and this is a latent bug rather
  than an active blocker.
