# MINIX kernel/arch/x86_64 — x86_64 port notes

Previously a byte-for-byte copy of `kernel/arch/i386`. This document describes
the changes made to produce a native 64-bit (AMD64 SysV) kernel for the
`dev-efi3` branch.

## Boot flow

### Entry points

Two entry paths are defined in `head.S`:

| Path | How invoked | Starting mode |
|---|---|---|
| `multiboot_entry32` | BIOS/legacy multiboot2 bootloader | 32-bit protected mode |
| `multiboot_entry64_efi` | UEFI bootloader with EFI64 entry tag (type 9) | 64-bit long mode |

Both paths converge at `.Lentry64_common` in 64-bit mode with boot page
tables active.

### 32-bit → 64-bit transition (multiboot_entry32)

1. Save `EAX`/`EBX` (magic / info pointer) to `.data`.
2. Build boot page tables (PML4 + PDPT + PD) in `.bss`:
   - `PML4[0] → pdpt_low[0] → pd_low[0..7]`: identity-maps 0–16 MB (2 MB pages).
   - `PML4[511] → pdpt_high[510] → pd_kern[2..9]`: maps kernel at −2 GB.
3. Load `boot_gdtptr32` (6-byte GDTR for 32-bit mode).
4. Set `CR4.PAE`, load `CR3`, set `EFER.LME`, set `CR0.PG` → IA-32e compatibility mode.
5. Far jump `ljmpl $KERN_CS_SELECTOR, $.Lentry64_from32` — the target is the
   physical address of the label (unpaged section links at `_kern_phys_base`),
   which is identity-mapped.  Zero-extending the 32-bit offset gives the correct
   64-bit RIP.

### EFI-64 path (multiboot_entry64_efi)

1. Save magic/info via RIP-relative stores.
2. Load `boot_gdtptr64` (10-byte GDTR) via RIP-relative `lgdtq`.
3. Reload CS to our 64-bit segment via the far-return trick
   (`pushq $KERN_CS_SELECTOR; pushq %rip_target; lretq`).
4. Rebuild boot page tables using 64-bit RIP-relative writes (same layout as above).
5. Switch `CR3` to `boot_pml4`.
6. Fall through to `.Lentry64_common`.

### Common 64-bit entry (.Lentry64_common)

- Zero DS/ES/SS.
- Switch to 64-bit boot stack (`boot_stack64`).
- Reload magic/info from memory (handles both paths uniformly).
- Call `pre_init(u32_t magic, u32_t info_ptr)` — AMD64 ABI: args in rdi, rsi.
- On return, `rax` = `kinfo_t *`.
- Switch RSP to `k_initial_stktop` (high-virtual kernel stack, now mapped).
- Indirect call to `kmain` via `movabsq` (avoids 32-bit displacement overflow
  from physical to high-virtual address).

## Memory layout

```
_kern_phys_base = 0x00400000   (physical load address)
_kern_vir_base  = 0xFFFFFFFF80400000   (kernel at −2 GB)
_kern_offset    = _kern_vir_base − _kern_phys_base = 0xFFFFFFFF80000000
```

Linker script (`kernel.lds`) places the unpaged section first at the physical
base, then jumps the VMA by `_kern_offset` for all normal sections:

```
. = 0x00400000;
.unpaged_text / .unpaged_data / .unpaged_bss   ← physical addresses

. += _kern_offset;   → VMA now 0xFFFFFFFF80400000 + phys_offset

.usermapped_glo / .usermapped / .text / .data / .bss   ← high virtual
```

## Page tables

`pg_utils.c` implements 4-level paging (PML4 → PDPT → PD → PT).

| Structure | Covers |
|---|---|
| `pg_pml4[512]` | root |
| `pg_pdpt_low[512]` | PML4[0] — low canonical half |
| `pg_pdpt_high[512]` | PML4[511] — high canonical half |
| `pg_pd_ident[64][512]` | identity 2 MB pages, up to 64 GB |
| `pg_pd_kern[512]` | kernel 2 MB pages at −2 GB |
| `pagetables[32][512]` | 4 KB page tables for `pg_map()` |

`pg_identity()` fills PML4[0]→pdpt_low→pd_ident[i] with 2 MB PS pages.
`pg_mapkernel()` fills PML4[511]→pdpt_high[510]→pd_kern starting at PD index 2
(which corresponds to virtual address `0xFFFFFFFF80400000`).

`pg_map()` is used by `arch_boot_proc()` to map VM into the bootstrap page
table using 4 KB pages.  It only handles user-space addresses (PML4 index 0).

## GDT / IDT / TSS

### Code segments

64-bit code segments require `L=1` and `D=0` (Intel SDM §3.4.5).
`init_codeseg()` calls `sdesc()` then clears `DEFAULT` and sets `LONG_MODE`.

### IDT gates

64-bit interrupt gates are 16 bytes.  `struct gatedesc_s` in `archtypes.h`:

```c
u16_t offset_low;
u16_t selector;
u8_t  ist;
u8_t  p_dpl_type;
u16_t offset_mid;
u32_t offset_high;
u32_t reserved;
```

### TSS

The 64-bit TSS descriptor occupies **two consecutive GDT slots** (16 bytes total).
`TSS_INDEX(cpu) = TSS_INDEX_FIRST + cpu * 2`.

`tss_init()` writes:
- Slot N: standard 8-byte descriptor (base[31:0], limit, type=`TSS_TYPE`).
- Slot N+1: low 32 bits = base[63:32], rest zero.

`tss_s` (in `arch_proto.h`) follows the Intel SDM Vol. 3A §7.7 64-bit layout:
`reserved0`, `rsp0`–`rsp2`, `reserved1`, `ist[7]`, `reserved2`, `reserved3`,
`iobase`.  No legacy segment-register fields (`ds`, `es`, `ss0`, `sp0`, etc.).

## SYSCALL / SYSENTER MSR setup (setup_sysenter_syscall)

On x86_64 the `SYSCALL` instruction reads RIP from **LSTAR** (MSR `0xC0000082`),
not from STAR's low 32 bits.

- `AMD_MSR_STAR` bits[63:32] = `(USER_CS_SELECTOR << 16) | KERN_CS_SELECTOR`;
  bits[31:0] = 0 (unused for 64-bit SYSCALL).
- `AMD_MSR_LSTAR` = full 64-bit virtual address of `ipc_entry_syscall_cpuN`,
  written as hi/lo pair through `ia32_msr_write`.
- `INTEL_MSR_SYSENTER_ESP` / `EIP` are also written as full 64-bit values
  (hi/lo), not truncated to 32 bits.

## klib.S — AMD64 ABI register conventions

All functions follow the AMD64 SysV ABI:

| Purpose | Registers |
|---|---|
| Integer arguments | rdi, rsi, rdx, rcx, r8, r9 |
| Return value | rax |
| Callee-saved | rbx, rbp, r12–r15 |
| Clobbered by CPUID | rbx, rcx, rdx — saved/restored in smp_get_* |

Notable implementations:

- **phys_insw/insb**: `insw`/`insb` use dx=port, rdi=dest, rcx=count.  On entry
  rdi=port, rsi=buf, rdx=count, so the function swaps them before the rep.
- **phys_copy**: `rep movsb` uses rsi=src, rdi=dst; the function swaps the first
  two arguments.
- **x86_load_kerncs**: uses the far-return trick (`pushq $CS; pushq $ret_addr; lretq`).
- **ia32_msr_read**: saves rdx (third arg = lo ptr) to r8 before `rdmsr` clobbers rdx.
- **ia32_msr_write**: rdi=reg, rsi=hi, rdx=lo → ecx=reg, edx=hi, eax=lo before `wrmsr`.

## Unpaged section and objcopy prefix mechanism

Objects in the unpaged list (head.o, pre_init.o, pg_utils.o, klib.o, mpx.o, …)
are processed with `objcopy --prefix-symbols=__k_unpaged_`, which renames **all**
symbols (both defined and external references).

Cross-object references from unpaged to non-unpaged code require explicit
`__k_unpaged_` aliases:

- `mpx.S` defines `LABEL(__k_unpaged_k_initial_stktop)` at the physical stack top.
- `kernel.lds` provides `PROVIDE(__k_unpaged_kmain = kmain)` so that the
  prefixed `call __k_unpaged_kmain` in `unpaged_head.o` resolves to the
  high-virtual address of `kmain` (which is accessible once `pre_init` has
  installed page tables).

## Multiboot2 header

```
magic    = 0xe85250d6
arch     = 0 (MULTIBOOT2_ARCHITECTURE_I386 — 32-bit PM entry convention)
checksum = -(magic + arch + header_size)
```

Tags (all 8-byte aligned):

| Tag | Type | Notes |
|---|---|---|
| Information request | 1 | cmdline, module, mmap, framebuffer, EFI mmap |
| Entry address | 3 | → `multiboot_entry32` |
| EFI64 entry address | 9 | → `multiboot_entry64_efi` |
| End | 0 | — |

The information-request tag `size` field is 28 (8-byte header + 5 × 4-byte
request fields), **not** including inter-tag alignment padding.

## SMP AP trampoline (`trampoline.S`)

APs start in 16-bit real mode and must reach the 64-bit `startup_ap_32` entry
point in `mpx.S` (the name is misleading — it is 64-bit code).  The transition
sequence:

1. In 16-bit real mode: compute the physical address of `__ap_startup_64`
   (the 64-bit stub within the trampoline page) as `CS × 16 + offset` and
   store it in the `__ap_jmpvec` far-pointer slot.
2. Load the BSP-prepared GDT (copied into `__ap_gdt_tab` by `copy_trampoline`)
   and IDT via `lgdtl` / `lidtl`.
3. Set `CR0.PE` — enter 32-bit protected mode.
4. Set `CR4.PAE` — required for long mode.
5. Load the BSP's PML4 physical address from `__ap_pt` into `CR3`.
6. Set `EFER.LME` via `wrmsr` on MSR `0xC0000080`.
7. Set `CR0.PG` — paging on; CPU enters IA-32e compatibility mode.
8. `ljmpl *__ap_jmpvec` — reads the 6-byte far pointer
   `{ phys_addr_of___ap_startup_64, KERN_CS_SELECTOR }`.  Loading the 64-bit
   CS descriptor transitions the CPU to 64-bit long mode; RIP = physical
   address of `__ap_startup_64` (identity-mapped, so VA = PA).
9. **`__ap_startup_64`** (64-bit stub in the trampoline page): `movabs
   $startup_ap_32, %rax; jmpq *%rax` — loads the full 64-bit kernel VA and
   jumps there.

The `ljmpl` must encode a 32-bit offset that fits in the low-memory trampoline
page; it cannot encode the high-kernel VA of `startup_ap_32` directly.  The
two-step approach (stub in trampoline → `movabs` to high VA) is the standard
x86_64 SMP solution.

### arch_smp.c — AP IDT table copy bug *(fixed)*

`copy_trampoline()` prepares the GDT and IDT for APs before copying the
trampoline to low memory.  The original code had a copy-paste error:

```c
memcpy(&__ap_gdt_tab, gdt, sizeof(gdt));
memcpy(&__ap_idt_tab, gdt, sizeof(idt));  /* BUG: gdt instead of idt */
```

The second `memcpy` used `gdt` as the source instead of `idt`, so every AP
received GDT descriptors in its IDT slot.  The first exception or external
interrupt on any AP (timer, NMI, page-fault) resolved the vector number
through a GDT entry, produced a garbage CS:EIP gate, and triggered an
immediate triple-fault.  SMP never became functional: all APs would die on
their first interrupt.

**Fix** (`arch_smp.c:90`): source changed to `idt`.

```c
memcpy(&__ap_idt_tab, idt, sizeof(idt));
```

### LP64 porting bugs in the SMP path *(fixed)*

The generic SMP core and the AP trampoline data layout were copied verbatim
from i386, where pointers and the gate/pointer descriptors happen to be
32-bit / 8-byte / 6-byte.  On x86_64 (LP64, long-mode descriptors) four of
those assumptions break.  All four are latent until `CONFIG_SMP` is enabled —
a UP kernel uses `#define cpuid 0` from `kernel.h` and never sends scheduling
IPIs — so they did not surface during single-CPU bring-up.

#### 1. `cpuid` macro truncates the kernel stack VA (`arch_smp.h`)

```c
/* before — correct on i386 (pointer == u32_t), broken on x86_64 */
#define cpuid (((u32_t *)(((u32_t)get_stack_frame() + (K_STACK_SIZE - 1)) \
                          & ~(K_STACK_SIZE - 1)))[-1])
```

`get_stack_frame()` returns a `reg_t` (64-bit) pointing into the kernel stack
at `0xFFFFFFFF80xxxxxx`.  The `(u32_t)` cast truncated it to `0x80xxxxxx`,
which dereferences to an invalid low VA.  Separately, `tss_init()` stores the
cpu id as a full `reg_t` at `kernel_stack_top - sizeof(reg_t)`
(`protect.c`: `*((reg_t *)(rsp0 + 1*sizeof(reg_t))) = cpu`), but the macro
read a `u32_t` at `stack_top - 4` — the high (always-zero) half of that slot.
So every CPU evaluated `cpuid == 0`, folding all per-CPU TSS, scheduling, and
cycle-accounting state onto CPU 0.

```c
/* after — vir_bytes arithmetic (no truncation), reg_t read (full 8-byte slot) */
#define cpuid (((reg_t *)(((vir_bytes)get_stack_frame() + (K_STACK_SIZE - 1)) \
                          & ~(K_STACK_SIZE - 1)))[-1])
```

`reg_t *` with `[-1]` reads `stack_top - 8`, matching where `tss_init` writes.

#### 2. `sched_ipi_data.data` truncates a kernel pointer (`smp.c`)

The scheduling-IPI mailbox passed a `struct proc *` between CPUs through a
`u32_t` field:

```c
struct sched_ipi_data {
        volatile u32_t flags;
        volatile u32_t data;          /* before: truncates a 64-bit proc ptr */
};
...
sched_ipi_data[cpu].data = (u32_t) p;             /* loses high 32 bits */
p = (struct proc *)sched_ipi_data[cpu].data;      /* zero-extends → bad addr */
```

`struct proc` objects live in the kernel image at `0xFFFFFFFF80xxxxxx`.  The
receiver reconstructed `0x0000000080xxxxxx` and dereferenced it in
`smp_sched_handler` on the first stop/vm-inhibit/migration IPI.  Fix: widen
the field to `vir_bytes` and store `(vir_bytes) p`; the readback already casts
back to `struct proc *`.

#### 3. AP IDT trampoline buffer was half-size (`trampoline.S`)

```asm
LABEL(__ap_idt_tab)
.space IDT_SIZE*DESC_SIZE        /* before: 256 * 8 = 2048 bytes */
```

`DESC_SIZE` (8) is the *segment* descriptor size, correct for the GDT
(`struct segdesc_s`).  But long-mode *gate* descriptors (`struct gatedesc_s`,
the IDT element type) are 16 bytes, so `sizeof(idt) = 256 * 16 = 4096`.
`copy_trampoline()`'s `memcpy(&__ap_idt_tab, idt, sizeof(idt))` therefore
overflowed the 2048-byte buffer by 2048 bytes into adjacent trampoline memory
on every AP boot.  Fix: add a `GATE_DESC_SIZE` (16) macro in `archconst.h`
and reserve `IDT_SIZE*GATE_DESC_SIZE`.

#### 4. Descriptor-table pointer slots were 8 bytes, not 10 (`trampoline.S`)

`__ap_gdt` / `__ap_idt` hold `struct desctableptr_s` = `{ u16 limit; u64 base; }`
= 10 bytes packed, but were reserved as `.space 8`.  Writing `.base` overflowed
2 bytes into the next field.  This was *benign in practice* — the overflow
bytes are the zero high half of a sub-1 MB physical address, and the clobbered
fields (`__ap_idt.limit`, `__ap_jmpvec`) are rewritten afterward — but fragile.
Reserved `.space 10` for each to match the struct.

## EFI64 boot bring-up — first successful boot (2026-06)

The UEFI/multiboot2 path had never run to completion; fixing it surfaced one
fault at a time from the loader hand-off through `arch_init`.  In order:

1. **Multiboot2 header out of search window** — `ld` defaulted to a 2 MB
   max-page-size, pushing the first `PT_LOAD` (and the header in
   `.unpaged_text`) to file offset `0x200000`, past the loader's 32 KB
   `MULTIBOOT_SEARCH`.  Fix: link the kernel with `-Wl,-z,max-page-size=0x1000`
   (`minix/kernel/Makefile`).  See `docs/kernel-build-x86_64-fixes.md`.

2. **NULL trampoline in the loader** — `efi_md_init()` (copies the
   `multiboot64`/`startprog64` trampolines into allocated memory and sets the
   function pointers) was never called from `efi_main()` in
   `sys/stand/efiboot/efiboot.c`.  `multiboot64` stayed at its `.quad 0`
   initializer, so `(*multiboot64)()` jumped through NULL to address 0.  Fix:
   call `efi_md_init()` before `boot()`.  (Latent because the multiboot2 path
   never reached the jump before.)

3. **EFI64 entry emitted into `.unpaged_data`** — in `head.S` the `.code64`
   directive does *not* switch sections back after the preceding `.data`
   block, so `multiboot_entry64_efi` landed in `.unpaged_data`.  It happened to
   work (boot map is RWX) but is wrong; add an explicit `.text` before the
   entry.

4. **Boot identity map too small (16 MB)** — `multiboot_entry64_efi`'s boot
   page tables identity-mapped only 0–16 MB, but the EFI loader places the
   multiboot info struct (via `AllocateAnyPages`) well above that (~2 GB on
   OVMF).  `pre_init()` dereferences it in `get_parameters_mb2()` before
   building the full map → triple fault.  Fix: identity-map the low **4 GB** in
   the boot tables (4 PD pages, `boot_pd_low` enlarged to 4 pages).

5. **`lldt` #GP in `prot_init`** — on x86_64 an LDT descriptor is a 16-byte
   system descriptor, but the code built an 8-byte one at `gdt[LDT_INDEX]`, so
   `lldt LDT_SELECTOR` faulted (#GP, error = the LDT selector `0x28`).  x86_64
   uses a flat GDT with no per-process LDT, so the fix is `x86_lldt(0)` (null
   LDTR) and dropping the bogus descriptor (`protect.c`).  The TSS descriptor
   is already correctly built as 16 bytes (`tss_init`).

6. **ACPI XSDT alignment padding** — see `docs/apic-x86_64.md`.

7. **LAPIC MMIO unmapped after `prot_init`** — see `docs/apic-x86_64.md`.

The boot now reaches APIC initialization.

## Limitations / future work

- **> 64 GB RAM**: identity map covers up to `PG_IDENT_PD_MAX` (64) GB; each
  entry is a 512-entry PD covering 1 GB via 2 MB pages.  Increase
  `PG_IDENT_PD_MAX` (and add more `pg_pdpt_low` entries) to support more RAM.

*(APIC was ported in a prior session; see `docs/apic-x86_64.md`.)*
