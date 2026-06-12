# MINIX kernel x86_64 — build fixes

Documents the compiler and linker fixes required to produce a working
`kernel` binary for the `dev-efi3` branch (x86_64, `-mcmodel=kernel`).

## Assembler issues

### fxsave64 / fxrstor64 not recognized (klib.S)

Clang 3.6's integrated assembler does not accept `fxsave64`/`fxrstor64`.
Replace with plain `fxsave`/`fxrstor`; the operand size is implied by the
REX.W prefix when the operand is a 64-bit memory reference.

### I/O port stubs (io_in*.S, io_out*.S)

The i386 versions used the frame-pointer calling convention (`push %ebp` /
`mov %esp, %ebp`).  All six files were rewritten for the AMD64 SysV ABI:

- Port in `%di` (first arg), value in `%si`/`%esi` (second arg).
- Return value in `%eax`.
- No frame pointer needed for these leaf functions.

### debugreg.S

Load/store debug registers (DR0–DR3, DR6, DR7) rewritten for AMD64:

```asm
ENTRY(ld_dr0)   movq %rdi, %dr0 ; ret
ENTRY(st_dr0)   movq %dr0, %rax ; ret
```

### multiboot2.h in assembly context (head.S)

`sys/arch/amd64/include/multiboot2.h` contains C typedefs which the
assembler cannot parse.  Guard them with `#ifndef ASM_FILE` and set
`#define ASM_FILE` before including the header from `.S` files:

```asm
#define ASM_FILE
#include <machine/multiboot2.h>
#undef ASM_FILE
```

The architecture field macro is `MULTIBOOT_ARCHITECTURE_I386` (value 0),
**not** `MULTIBOOT2_ARCHITECTURE_I386` (the `2` prefix is absent per spec).

## Compiler flags

### -mcmodel=kernel and -mno-red-zone

The kernel is linked at virtual address `0xFFFFFFFF80400000` (top 2 GB of
the canonical address space).  Without `-mcmodel=kernel`, the compiler emits
`R_X86_64_32` (zero-extended 32-bit absolute) relocations for string literals
and global data.  Those overflow because `0xFFFFFFFF80400000 > 0xFFFFFFFF`.

With `-mcmodel=kernel` the compiler emits `R_X86_64_32S` (sign-extended):
`0x80400000` sign-extended to 64 bits = `0xFFFFFFFF80400000` ✓.

`-mno-red-zone` is required because interrupt handlers can fire at any time;
without this flag the ABI's 128-byte red zone below RSP would be overwritten.

Both flags are added in `minix/kernel/arch/x86_64/Makefile.inc`:

```makefile
CFLAGS += -mcmodel=kernel -mno-red-zone
```

## Type fixes

### phys_bytes vs vir_bytes for kern_vir_start (pg_utils.c)

`phys_bytes` is `u32_t` (32-bit) in MINIX.  The static initializer

```c
static phys_bytes kern_vir_start = (phys_bytes) &_kern_vir_base;
```

generates an `R_X86_64_32` relocation against the linker symbol
`_kern_vir_base = 0xFFFFFFFF80400000`, which does not fit.  Fix:

```c
static vir_bytes kern_vir_start = (vir_bytes) &_kern_vir_base;
```

`vir_bytes` is `unsigned long` (64-bit), so the compiler emits `R_X86_64_64`
(8-byte absolute), which always fits.

### int-to-pointer casts

Many kernel x86_64 files cast 32-bit types (`u32_t`, `phys_bytes`,
`endpoint_t`) to pointer types.  Clang rejects these on LP64.  Pattern:

```c
/* wrong — clang error: cast to pointer from smaller integer type */
struct foo *p = (struct foo *) u32_value;

/* correct */
struct foo *p = (struct foo *)(uintptr_t) u32_value;
```

Affected files: `mb2_getparam.c`, `protect.c`, `acpi.c`, `do_vmctl.c`.

### #if CONFIG_OXPCIE vs #ifdef (memory.c)

`oxpcie.c` guards `oxpcie_set_vaddr()` with `#if CONFIG_OXPCIE` (falsy when
the macro is undefined or defined as 0).  Using `#ifdef` in `memory.c` causes
the call site to be compiled even when the function is absent, producing an
undefined-reference linker error.  Use `#if CONFIG_OXPCIE` consistently.

## Library code model mismatch

### Problem

The kernel links against pre-built archives: `-ltimers -lsys -lexec -lminc`.
These libraries are compiled without `-mcmodel=kernel`, so their object files
contain `R_X86_64_32` relocations.  When placed at the kernel's high virtual
address those relocations overflow.

`libtimers.a` contains only RIP-relative or 64-bit references — no issue.
`libsys.a`, `libexec.a`, and `libminc.a` all contain `R_X86_64_32`.

### Fix: shadow archives with kernel-compiled objects

Objects already compiled in the kernel build context (for the unpaged
section) inherit `-mcmodel=kernel` from `Makefile.inc`.  By also adding them
to `OBJS.kernel`, the linker resolves those symbols from the
kernel-model objects **before** reaching the archives:

```makefile
# In minix/kernel/arch/x86_64/Makefile.inc

# Reuse unpaged objects as paged kernel objects (already -mcmodel=kernel)
OBJS.kernel += ${MINLIB_OBJS_UNPAGED}   # _cpufeature.o _cpuid.o get_bp.o
OBJS.kernel += ${MINC_OBJS_UNPAGED}    # printf.o subr_prf.o atoi.o memcpy.o ...
OBJS.kernel += ${SYS_OBJS_UNPAGED}     # assert.o stacktrace.o

# libexec: only two source files, both needed; compile fresh
exec_elf.o:     ${NETBSDSRCDIR}/minix/lib/libexec/exec_elf.c
exec_general.o: ${NETBSDSRCDIR}/minix/lib/libexec/exec_general.c
OBJS.kernel += exec_elf.o exec_general.o

# libminc extras pulled in transitively
init.o:     ${NETBSDSRCDIR}/minix/lib/libc/sys/init.c
_errno.o:   ${NETBSDSRCDIR}/lib/libc/gen/_errno.c
CPPFLAGS._errno.c += -I${NETBSDSRCDIR}/lib/libc/include -U_REENTRANT
OBJS.kernel += init.o _errno.o
```

`init.o` provides `_minix_kerninfo` / `_minix_ipcvecs` globals used by the
fast-IPC path.  `_errno.o` provides `errno` and `__errno()`.  Their
transitive dependencies (`_ipc_sendrec_intr`, `_do_kernel_call_intr`, etc.)
are defined in `libminc.a` using `R_X86_64_64` relocations (no overflow).

### Why not rebuild the libraries with -mcmodel=kernel?

`libsys` and `libminc` are also used by userspace servers and drivers (normal
virtual addresses, low half).  Adding `-mcmodel=kernel` globally would
generate code that assumes the top-2-GB range, breaking all non-kernel users.
The shadow-object approach avoids this by keeping two separately-compiled
copies: the archives serve userspace, the kernel-built objects serve the
kernel binary.

## Result

After all fixes the kernel binary is an ELF64 `EXEC` for `Advanced Micro
Devices X86-64`, entry point `0x400000` (physical load address), linked at
`0xFFFFFFFF80400000`.
