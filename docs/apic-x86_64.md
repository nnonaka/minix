# APIC x86_64 Port

## Background

The x86_64 MINIX kernel inherited i386 APIC assembly verbatim.  All four APIC
assembly files (`apic_asm.S` for both arches) were byte-for-byte identical,
which meant the x86_64 copy used 32-bit calling convention, 32-bit register
names, `pusha`/`popa`, and `iret` — none of which are valid in AMD64 long mode.
Additionally, `hw_intr.h` had a hardcoded `kernel/arch/i386/apic.h` include
path, and `ioapic_read`/`ioapic_write` accepted `u32_t` base addresses that
silently truncated 64-bit `vir_bytes` MMIO pointers.

This document describes the fixes applied on the `dev-efi3` branch.

## Files Changed

| File | Change |
|------|--------|
| `minix/kernel/arch/x86_64/apic_asm.S` | Full rewrite for AMD64 ABI |
| `minix/kernel/arch/x86_64/sconst.h` | Added `pushaq`/`popaq`/`KERNEL_IRQ_RFLAGS_OFF` |
| `minix/kernel/arch/x86_64/mpx.S` | Removed now-duplicate macro definitions |
| `minix/kernel/arch/x86_64/include/hw_intr.h` | Fixed hardcoded i386 include path |
| `minix/kernel/arch/x86_64/apic.c` | Fixed MMIO base address types |
| `minix/kernel/arch/x86_64/acpi.c` | Full 64-bit XSDT table addresses |
| `minix/kernel/arch/x86_64/mb2_acpi2.c` | Deleted (unused experimental XSDT parser) |
| `minix/kernel/proto.h` | Added forward declaration for `struct sigframe_sigcontext` |

---

## apic_asm.S — AMD64 Rewrite

### CS displacement in TEST_INT_IN_KERNEL

In i386, a same-privilege interrupt pushes only three words (EIP, CS, EFLAGS),
so CS is at `4(%esp)`.  In x86_64 the CPU **always** pushes a full five-word
frame (RIP, CS, RFLAGS, RSP, SS) — even for ring-0 → ring-0 — so CS is always
at `8(%rsp)`.

```diff
-TEST_INT_IN_KERNEL(4, 0f)
+TEST_INT_IN_KERNEL(8, 0f)
```

### Calling convention: APIC_IRQ_HANDLER

i386 passes arguments on the stack; AMD64 passes the first integer argument in
`%rdi`/`%edi`.

```diff
-#define APIC_IRQ_HANDLER(irq)    \
-    push    $irq                ;\
-    call    irq_handle          ;\
-    add     $4, %esp            ;
+#define APIC_IRQ_HANDLER(irq)    \
+    movl    $irq, %edi          ;\
+    call    irq_handle          ;
```

### context_stop call

i386 passed `proc_ptr` (held in `%ebp`) by pushing it onto the stack.  AMD64
passes it as the first argument in `%rdi`, matching the SysV AMD64 ABI.  The
pattern mirrors what `mpx.S` already does for the PIC handlers:

```diff
-    push    %ebp
-    call    context_stop
-    add     $4, %esp
-    movl    $0, %ebp          /* for stack trace */
+    movq    %rbp, %rdi        /* context_stop(proc_ptr) */
+    push    %rbp
+    movq    $0, %rbp          /* for stack trace */
+    call    context_stop
+    pop     %rbp
```

### LAPIC_INTR_HANDLER

The i386 version loaded the handler address into a 32-bit register and called
it indirectly; the EOI address was loaded with a 32-bit `mov`:

```diff
-#define LAPIC_INTR_HANDLER(func)                   \
-    movl    $func, %eax                            ;\
-    call    *%eax                                  ;\
-    mov     lapic_eoi_addr, %eax                   ;\
-    movl    $0, (%eax)                             ;
+#define LAPIC_INTR_HANDLER(func)                   \
+    call    func                                   ;\
+    movq    lapic_eoi_addr(%rip), %rax             ;\
+    movl    $0, (%rax)                             ;
```

`lapic_eoi_addr` is `vir_bytes` (64-bit); the old 32-bit load would truncate
on systems where the LAPIC is mapped above 4 GB.  The store remains 32-bit
(`movl`) because the LAPIC EOI register is a 32-bit MMIO register.

### In-kernel interrupt path

AMD64 has no `pusha`/`popa`.  The macros `pushaq`/`popaq` (15 × `push`/`pop`
covering all GP regs except `%rsp`) are defined in `sconst.h` and shared with
`mpx.S`.

```diff
-    pusha
-    call    context_stop_idle
-    APIC_IRQ_HANDLER(irq)
-    CLEAR_IF(10*4(%esp))
-    popa
-    iret
+    pushaq
+    call    context_stop_idle
+    APIC_IRQ_HANDLER(irq)
+    CLEAR_IF(KERNEL_IRQ_RFLAGS_OFF(%rsp))
+    popaq
+    iretq
```

`KERNEL_IRQ_RFLAGS_OFF` is 136: after `pushaq` (15 × 8 = 120 bytes), the
CPU-pushed frame sits at `120(%rsp)`; RFLAGS is 16 bytes into that frame
(after RIP and CS), giving `120 + 16 = 136`.

### APIC_DEBUG dummy handlers

The i386 version pushed arguments for `panic()` on the stack.  AMD64 ABI:

```diff
-#define lapic_intr_dummy_handler(vect)     \
-    pushl   $vect                         ;\
-    push    $lapic_intr_dummy_handler_msg ;\
-    call    panic                         ;\
+#define lapic_intr_dummy_handler(vect)                      \
+    movl    $vect, %esi                                    ;\
+    leaq    lapic_intr_dummy_handler_msg(%rip), %rdi       ;\
+    call    panic                                          ;\
```

Each handler is ≤ 19 bytes; `.balign LAPIC_INTR_DUMMY_HANDLER_SIZE` (32) pads
to the stride used by `lapic_set_dummy_handlers()` when programming the IDT.

---

## sconst.h / mpx.S — Shared Macros

`pushaq`, `popaq`, and `KERNEL_IRQ_RFLAGS_OFF` were previously defined only
inside `mpx.S`.  They are now in `sconst.h` (included by both `mpx.S` and
`apic_asm.S`) and removed from `mpx.S` to avoid redefinition.

---

## hw_intr.h — Include Path Fix

```diff
-#include "kernel/arch/i386/apic.h"
+#include "kernel/arch/x86_64/apic.h"
```

---

## apic.c — MMIO Base Address Types

`ioapic_read` and `ioapic_write` accepted `u32_t ioa_base`.  On x86_64
`vir_bytes` is 64-bit, so passing an I/O APIC virtual address truncated
silently to 32 bits.

```diff
-static u32_t ioapic_read(u32_t ioa_base, u32_t reg)
+static u32_t ioapic_read(vir_bytes ioa_base, u32_t reg)

-static void ioapic_write(u32_t ioa_base, u8_t reg, u32_t val)
+static void ioapic_write(vir_bytes ioa_base, u8_t reg, u32_t val)
```

`ioapic_redirt_entry_write` was updated from `void *` to `vir_bytes`
consistently, and the `(u32_t)` casts at all call sites were removed.

A `%llu` format string for `u64_t` was fixed: on x86_64 `u64_t` is
`unsigned long`, not `unsigned long long`.  A cast to `(unsigned long long)`
makes the format portable across both i386 and x86_64.

---

## proto.h — sigframe_sigcontext Forward Declaration

`struct sigframe_sigcontext` is defined in `sys/arch/i386/include/frame.h` but
not in the amd64 equivalent.  Using an undeclared struct tag in a function
prototype creates a declaration scoped to the parameter list, which Clang
rejects with `-Werror,-Wvisibility`.  A file-scope forward declaration was
added immediately before the `fpu_sigcontext` prototype.

---

## acpi.c — Full 64-bit XSDT Support

The APIC/SMP code discovers LAPICs and I/O APICs by walking the ACPI MADT,
which it locates through `acpi.c`.  On UEFI x86_64, ACPI is revision 2: the
RSDP points at an **XSDT** whose entries are 64-bit physical addresses, unlike
the legacy **RSDT** with 32-bit entries.

`acpi.c` already read the XSDT for `revision == 2`, but immediately narrowed
every entry back to 32 bits before storing it:

```diff
-	sdt_count = (s - sizeof(struct acpi_sdt_header)) / sizeof(u64_t);
-	for (i = 0; i < sdt_count; i++) {
-		rsdt.data[i] = (u32_t)xsdt.data[i];	/* truncates >4 GB tables */
-	}
+	sdt_count = (s - sizeof(struct acpi_sdt_header)) / sizeof(u64_t);
```

The whole lookup path was 32-bit too (`rsdt.data[]` is `u32_t[]`,
`acpi_phys2vir` took/returned `u32_t`), so any table the firmware placed above
4 GB was silently corrupted.  Since `phys_bytes` is `unsigned long` (64-bit) on
amd64, the truncation was the only barrier.

The fix makes the resolved table address the single source of truth and keeps
it full-width:

- Added `phys_bytes base` to the per-table `sdt_trans[]` record.
- A unified post-read loop selects each entry as a full `phys_bytes`
  (`rsdt.data[i]` for RSDT, `xsdt.data[i]` for XSDT — no down-cast) and stores
  it in `sdt_trans[i].base`.
- `acpi_get_table_base()` returns `sdt_trans[i].base` instead of
  `(phys_bytes)rsdt.data[i]`.
- `acpi_phys2vir(u32_t)` widened to `acpi_phys2vir(phys_bytes)`; the
  header-read error format changed `0x%x` → `0x%lx`.

`sdt_count` stays bounded by `MAX_RSDT` (35): `acpi_read_sdt_at()` rejects an
XSDT whose length overflows the `xsdt.data[MAX_RSDT]` read buffer.

The unused, debug-laden experimental parser `mb2_acpi2.c` (a standalone
`acpi2_init()` that was never compiled or called) was deleted.

### XSDT struct alignment padding (#GP at first boot)

When the EFI64 path first reached `acpi_init()`, the table loop took a #GP
(non-canonical address) in `memcpy` reading `xsdt.data[i]`.  `struct
acpi_sdt_header` is exactly 36 bytes (4-aligned), but `struct acpi_xsdt`'s
`u64_t data[]` forces 8-byte alignment, so the compiler inserted **4 bytes of
padding** after the header — `data[]` started at struct offset 40.  The
on-disk XSDT packs its 64-bit table pointers immediately after the header
(offset 36).  `memcpy`'ing the raw table into the struct therefore misaligned
every entry by 4 bytes, yielding mangled non-canonical addresses → #GP when
dereferenced.  Fix: mark the struct `__packed` so `data[]` sits at offset 36.
Unaligned 64-bit reads are fine on x86.  (The RSDT's `u32_t data[]` is
naturally 4-aligned and needs no packing.)

**Limitation:** before VM is running, `acpi_phys_copy` reads a physical address
*as* a virtual one via the boot identity/direct map.  The address path is now
correct for >4 GB tables, but actually reaching such a table also requires that
high range to be mapped at access time.  On QEMU/OVMF, firmware keeps ACPI
tables in low reclaim memory (<4 GB), so this works today; truly-high tables
would need a temporary mapping in `acpi_phys_copy`.

---

## Architecture Notes

- The LAPIC and I/O APIC MMIO regions (0xFEE00000 and 0xFEC00000) are
  physically below 4 GB and identity-mapped by the x86_64 kernel, so the
  addresses currently fit in 32 bits.  Nonetheless, using `vir_bytes` is
  correct and avoids latent bugs if the mapping ever moves.

- **`pg_identity()` must cover the MMIO hole.**  At first boot, APIC init took a
  page fault at `0xFEE00080`: `prot_init()` replaces the head.S boot map with
  `pg_identity()`, which only mapped RAM up to `mem_high_phys` (~2 GB on the
  test VM) — leaving the LAPIC at `0xFEE00000` (~4 GB) unmapped.  Fix: in
  `pg_utils.c`, `pg_identity()` always maps at least the low **4 GB**
  (`if (num_pds < 4) num_pds = 4;`), so the LAPIC/IOAPIC/video MMIO is
  reachable.  Pages above `mem_high_phys` are already mapped uncacheable
  (`PG_PWT | PG_PCD`).

- `iret` is illegal in 64-bit mode; `iretq` pops a 64-bit RIP, CS, RFLAGS,
  RSP, SS frame.

- The APIC C code (`apic.c`) is otherwise architecture-neutral: LAPIC/IOAPIC
  register access is via 32-bit MMIO reads/writes regardless of CPU bitness.
