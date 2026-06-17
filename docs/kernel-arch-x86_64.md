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
| `pg_pd_ident[8][512]` | identity 2 MB pages, up to 8 GB |
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

## Limitations / future work

- **SMP trampoline**: `trampoline.S` is a verbatim i386 trampoline.  It
  transitions real mode → 32-bit protected mode and jumps to `startup_ap_32`
  (`mpx.S`).  There is no PAE enable, no 64-bit page table load, no
  `EFER.LME`, no far jump into long mode.  AP cores will not boot until this
  is rewritten to mirror the BSP's `multiboot_entry32` → long-mode path in
  `head.S`.
- **> 8 GB RAM**: identity map covers up to `PG_IDENT_PD_MAX` (8) GB; increase
  `PG_IDENT_PD_MAX` to support more RAM.
- **NX bit**: page table entries do not set the NX/XD bit on data pages.

*(APIC was ported in a prior session; see `docs/apic-x86_64.md`.)*
