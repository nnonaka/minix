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
- `AMD_MSR_FMASK` (`0xC0000084`) **must** be programmed. `SYSCALL` clears
  exactly the RFLAGS bits set in FMASK; left at its power-on value of 0 it
  clears nothing, so the user-space `IF` (interrupts enabled) carries straight
  into the kernel and the **entire syscall/IPC path runs with interrupts on**.
  A device IRQ can then `mini_notify()` → `enqueue()` a process while the kernel
  is walking/editing the run queues, corrupting them. i386 is immune because it
  enters through an INT/interrupt gate, which clears `IF` in hardware.

```c
/* before: FMASK never written -> IF stays set through SYSCALL */

/* after (setup_sysenter_syscall): mask NT|AC|DF|IF|TF like NetBSD/amd64 */
ia32_msr_write(AMD_MSR_FMASK, 0, 0x44700);
/*   NT 0x4000 | AC 0x40000 | DF 0x400 | IF 0x200 | TF 0x100 */
```

  `SYSRET` restores RFLAGS from `r11` (which holds the user value with `IF`
  set), so interrupts resume on return to user. Symptom before the fix: the
  `runqueues_ok` "double sched" assert (`proc.c` `dequeue`/`enqueue`) firing
  intermittently once `DEBUG_SANITYCHECKS` was enabled — a false-looking
  double (proc linked once but `p_found` already set) because an IRQ moved a
  process between queues mid-check. With the fix the system boots to multiuser.

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

## EFI64 bring-up — reaching userland exec (2026-06)

Continuing from APIC init, the boot was carried through VM adopting the
per-process page tables and into userland: `exec` works and `init` runs
`/etc/rc`, starting drivers (acpi, pci, pckbd, floppy, at_wini) and services.
Two themes dominated.

### Theme 1: 64-bit addresses truncated through 32-bit IPC message fields

A user virtual address with bit 31 set (e.g. a stack address `0xefffXXXX`,
since the MINIX user stack top is `0xF0000000`) stored into a 32-bit `int`
message field and read back into a `vir_bytes` is **sign-extended into the
kernel half** (`0xffffffffefffXXXX`).  The receiver then rejects it as a
kernel address.  Three instances, all in `minix/include/minix/com.h`
(+`ipc.h`):

1. **`VPF_ADDR`** (`VM_PAGEFAULT` fault address, kernel→VM): `m1_i1` →
   `m1_ull1`.  Symptom: a normal demand-paged stack-growth fault (`cr2` was
   correct) became a kernel-half address in VM, which SIGSEGV'd the faulting
   server.
2. **`SVMCTL_MRG_ADDR`** (kernel→VM memory-request address, the `vm_suspend`
   path): `m2_i2` → `m2_ll1`.  Tell-tale: the *source* address
   `SVMCTL_MRG_ADDR2` was already a 64-bit `m2_l2`; only the primary address
   was left 32-bit.
3. **`VFS_PM_PS_STR` / `VFS_PM_NEWPS_STR`** (`ps_strings` pointer, VFS↔PM exec
   reply): `m7_i5` → a new pointer field **`m7_p3`** added to `mess_7`
   (`ipc.h`, consuming padding; i386 size stays 56 bytes via alignment).  Note
   `VFS_PM_PC`/`VFS_PM_NEWSP` already used pointer fields (`m7_p1`/`m7_p2`), so
   `ip`/`sp` survived while `ps_str` was truncated.

This pattern is **systemic**: audit any `m*_i*` field carrying an address.
Downstream typed structs (`mess_rs_pm_exec_restart`, `mess_lsys_krn_sys_exec`)
already use `vir_bytes ps_str`.

### Theme 2: the demand-paged kernel copy (the exec stack frame)

The single biggest blocker.  `lin_lin_copy()` (`arch/x86_64/memory.c`) copies
between address spaces using `createpde()`, which returns **0** for a
not-present process page (e.g. a child's demand-zero stack during the exec
frame copy).  The old code relied on `PHYS_COPY_CATCH` to detect a bad page,
but with `dstptr == 0` the copy touches **virtual address 0**, whose caught
fault address is `0` — *indistinguishable from the `if(addr)` "no fault"
sentinel*.  So the copy silently "succeeded" writing nowhere, VM was never
asked to fault the page in, and every later access got a fresh zero page.
Text/data escaped this only because the ELF loader pre-allocates them (present
→ real phys).  The user-visible effect: the first user process (`sh`) found
`ps_strings->ps_argvstr == 0` and `#PF`'d in crt0.

Fix — detect not-present explicitly so `virtual_copy_f()` routes through
`vm_suspend()`:

```c
srcptr = createpde(srcproc, srclinaddr, &chunk, 0, &changed);
dstptr = createpde(dstproc, dstlinaddr, &chunk, 1, &changed);
if (srcproc && !srcptr) return EFAULT_SRC;   /* not-present -> ask VM */
if (dstproc && !dstptr) return EFAULT_DST;
```

(`createpde` returns `phys_to_kacc(phys)` = `DM_BASE + phys` for present
pages, never 0, so `!ptr` cleanly means not-present.)

### Other fixes folded in

- **`pt_free()` freed `pt_pdpt` but left the pointer non-NULL**
  (`servers/vm/pagetable.c`).  `pt_new()` allocates the PML4+PDPT once and
  reuses them (recovering the PDPT phys from the kept `pt_pml4[0]`); the stale
  non-NULL `pt_pdpt` made the next `pt_new()` `memset` a freed/unmapped page →
  "pagefault in VM" on `VMPPARAM_CLEAR` slot reuse (exec).  Fix: keep
  `pt_pdpt` (never free, same policy as `pt_pml4`) and NULL the freed
  `pt_pd[]`/`pt_pt[]` slots.

- **`SYS_DEVIO`/`VDEVIO`/`SDEVIO`/`IOPENABLE`/`READBIOS` were i386-only**
  (`kernel/system.c` `map()` calls + `system/Makefile.inc` `do_devio.c`/
  `do_vdevio.c` sources).  amd64 has identical x86 port I/O; gate them
  `__i386__ || __x86_64__`.  Symptom: "Unused kernel call 21" and tty/driver
  `sys_inb`/`sys_outb` failures.

- **`arch_proc_init()` truncated `ip`/`sp`/`ps_str` to `u32_t`**
  (`do_exec.c` casts + `proto.h` + all three `arch/*/memory.c`): widen to
  `vir_bytes`.  Latent for sh (values < 4 GB) but a real LP64 bug.

- **VFS computes `ps_str` authoritatively** (`servers/vfs/exec.c` `pm_exec`):
  the `ps_strings` struct always sits at the top of the (possibly relocated)
  frame, so `*ps_str = vsp + frame_len - sizeof(struct ps_strings)` rather
  than echoing the caller's guess (robust against stale-libc callers and
  script/dynamic frame relocation).

- **libc stack frame** (`stack_utils.c`, `execve.c`): `vsp` widened `int` →
  `vir_bytes`; argc occupies a pointer-sized cell so `argv[]` starts
  `sizeof(char *)` in, and `ps_argvstr = vsp + sizeof(char *)`.

## Physical memory map (multiboot2) — usable-RAM fixes

`get_parameters_mb2()` builds `cbi->memmap` (the kernel's free-RAM list, later
fed to VM) from the multiboot2 mmap tag.  Two fixes were needed before VM could
be trusted with the memory it hands out — both caused VM to place structures on
**non-RAM physical pages**, whose writes vanish, producing pervasive corruption
(details in `vm-x86_64-port.md`):

- **`do_tag_mmap()` truncated 64-bit addresses.**  `base_addr`/`length` were
  `uint32_t`, truncating the firmware `mmap->addr`/`len` (folding a >4 GB region
  down to a bogus low address that overlaps real low RAM).  Use `u64_t`.

- **Legacy ROM hole left in usable RAM.**  Firmware reports `0xA0000–0x100000`
  (VGA framebuffer + option/BIOS-ROM shadow) as available, but under QEMU those
  pages are ROM/MMIO and silently drop writes.  `get_parameters_mb2()` already
  carves out the kernel and boot modules with `cut_memmap()`; it now also does
  `cut_memmap(cbi, 0xA0000, 0x100000)`.  Symptom before the fix: kernel
  `vm_memset` zeroing a page at `ph=0xff000` left it `0xffffffff` (the
  read-back-verify that found it).

With these (plus the VM `findbit()` LP64 fix), MINIX/amd64 boots to a multiuser
login prompt and working root shell.  (The earlier "no login prompt" stall was a
symptom of this corruption, not a separate console bug.)

## Read-only / copy-on-write protection on kernel-mediated copies (test74)

A POSIX `read(2)` into a read-only `mmap`'d buffer must fail with `EFAULT`; on
amd64 it silently succeeded.  Three independent defects let kernel-mediated
writes bypass a page's write protection — all three had to be fixed:

1. **`createpde()` ignored the PTE write bit.**  It returns the kernel
   direct-map alias (`phys_to_kacc(phys)`, always writable) for any *present*
   page, so a kernel-mediated write into a present read-only or COW page wrote
   straight through — no fault, no copy-on-write.  (i386 avoids this with
   `CR0.WP=1` plus its transplanted process-PDE copy window.)  Fix:
   `createpde()`'s 4th parameter is now a `writable` flag (formerly the unused
   `free_pde_idx`); `lin_lin_copy()` passes 0 for the source and 1 for the
   destination — see the Theme 2 snippet above.  For a write, `createpde()`
   returns the not-present sentinel `0` when the leaf entry lacks
   `AMD64_VM_WRITE`, so the access routes through `vm_suspend(..., writeflag=1)`
   and VM performs copy-on-write (writable region) or returns `EFAULT`
   (read-only region).

2. **`CR0.WP` was never set.**  Long mode is entered with paging already on by
   the EFI loader, so `pre_init.c` *skipped* `vm_enable_paging()` — but that
   routine also sets `CR0.WP` (and `CR4.PGE`).  Without WP, supervisor-mode
   writes ignore read-only PTEs entirely (the whole protection model assumes
   WP=1).  Fix: call `vm_enable_paging()` after `pg_load()` on amd64; it only
   ORs in WP/PGE and does **not** re-toggle paging.

3. **`vm_memset()` had the same not-present bug as `lin_lin_copy()`** — this is
   the `safememset` path, used e.g. by `/dev/zero` reads.  It relied on
   `phys_memset` faulting, but a not-present page makes it touch virtual address
   0, whose caught fault address `0` is indistinguishable from "no fault", so
   the memset silently succeeds writing nowhere.  Fix: pass `writable=1` to
   `createpde()` and, exactly like `lin_lin_copy()`, explicitly
   `vm_suspend(..., 1)` when it returns 0.

Why all three: (1) covers the cross-process page-walk copy (a server's
`safecopy` into a user buffer), (2) covers a write through the faulting
process's own mapping, (3) covers `safememset`.  This also makes copy-on-write
correct for kernel-mediated writes generally, not just the test.

## FPU / XMM context-switch state (test2)

A POSIX `test2` pipe test failed with "wrong data": a parent filled a buffer with
an SSE-vectorized loop (`buf[i]=i&0xff` → clang emits `movdqa`/`paddb`, holding
the increment constant `{0,1,…,15}` in `xmm0` across the whole loop), wrote it
through a pipe, and the child read back corruption — every other 16-byte group
zeroed, as if `xmm0` had been clobbered to `0` (a `pxor %xmm0,%xmm0` signature)
mid-fill.  The root cause was **XMM not surviving a context switch**, from two
independent amd64 defects:

1. **`fpu_init()` mis-probed the FPU → no `fxsave`.**  The x87 probe
   (`fninit`/`fnstcw`) ran with `CR0.EM=1` (FPU emulation), so the control-word
   check failed and the code took the "no FPU" branch, leaving
   `osfxsr_feature=0`.  With that flag clear, `save_local_fpu()` used `fnsave`
   (x87/MMX only, 108 bytes) instead of `fxsave` — **the XMM registers were
   never saved/restored at all**.  SSE still ran in userland (`CR4.OSFXSR` was
   set elsewhere), so XMM silently leaked across every context switch.  Fix
   (`arch_system.c fpu_init`): clear `CR0.EM`/`CR0.TS` and set `CR0.MP` before
   the probe so x87 initializes and the probe succeeds → `osfxsr_feature=1` →
   `fxsave`/`fxrstor` save the full 512-byte area including all 16 XMM.

2. **`do_fork()` gave the child the parent's FPU save buffer.**  Each process's
   `p_seg.fpu_state` points at its own slot in `static fpu_state[NR_PROCS][512]`
   (assigned by `arch_proc_reset` from `p_nr`).  `do_fork` copies the parent
   `proc` struct wholesale (`*rpc = *rpp`), which **overwrites the child's
   `fpu_state` pointer with the parent's**; the code that saves the child's own
   pointer beforehand and restores it afterward (then `memcpy`s the parent's FPU
   contents in) was guarded by `#if defined(__i386__)` and so was **compiled out
   on amd64**.  Result: parent and child shared one FPU buffer.  The lazy-FPU
   `#NM` path then corrupted it: the child's first SSE use trapped, saved *its*
   `xmm0=0` into the shared buffer, and when the parent resumed `fxrstor` loaded
   that zero — exactly the observed `xmm0={0,1,2,3}` → `00000000` transition.
   Fix: change both guards to `#if defined(__i386__) || defined(__x86_64__)` so
   the child keeps its own buffer (declaration of `old_fpu_save_area_p` too).

Diagnosing this took a buffer-aliasing detector in `save_local_fpu`/`restore_fpu`
that printed each watched process's `p_nr` and `fpu_state` address: the parent
(`nr=205`) and child (`nr=210`) showing the *same* `buf=0x…` pinned it
immediately.  General lesson: any per-process resource the i386 port carries
across `*rpc = *rpp` in `do_fork` (here the FPU buffer pointer) must have its
`#if defined(__i386__)` guard extended to amd64.

## Limitations / future work

- **> 64 GB RAM**: identity map covers up to `PG_IDENT_PD_MAX` (64) GB; each
  entry is a 512-entry PD covering 1 GB via 2 MB pages.  Increase
  `PG_IDENT_PD_MAX` (and add more `pg_pdpt_low` entries) to support more RAM.

## Companion POSIX-suite fixes (non-arch)

Other `minix-posix` failures fixed alongside the above, recorded here for the
trail (the code lives outside `kernel/arch`):

- **test53** — `minix/include/minix/u64.h`: `ex64lo()`/`ex64hi()` returned
  `unsigned long`, which is 64-bit on amd64, so they no longer truncated to the
  low/high 32 bits.  Fixed by masking through `(u32_t)` while keeping the
  `unsigned long` return type (preserves i386 semantics and `%lx` callers).
- **test66** — `minix/tests/test66.c` used `unsigned long`/`signed long` for
  values meant to be 32-bit; on LP64 that changes `(unsigned long)(-10) % 7`.
  Switched those locals to `int32_t`/`uint32_t`.
- **test64 panic / test74** — see "Read-only / COW protection" above and the
  low-memory reserve in `docs/vm-x86_64-port.md`.
- **test76** — silenced an upstream debug `printf` in `servers/vfs/worker.c`.
- **test77** (PTY) — the earlier subtest-5 `sigsuspend` heisenbug-hang cleared
  with the WP/COW and FPU fixes above (the suite now runs through to the end),
  exposing a real subtest-7 failure: `test_getdents` over `/dev/pts` checks that
  each Unix98 slave node belongs to the `tty` group, but they came up group
  `wheel` (gid 0).  Root cause was in `etc/MAKEDEV.tmpl`: the `ptmx)` recipe
  created `/dev/ptmx` with no group argument, so `mkdev` defaulted it to
  `$g_wheel`.  MINIX `etc/usr/rc` then starts the pty driver with
  `-args "gid=$(stat -f '%g' /dev/ptmx)"`, so the driver stamped every slave
  node (via ptyfs) with gid 0 instead of `tty` (4).  Fix: add `$g_tty` to the
  `ptmx` line — `mkdev ptmx c %ptmx_chr% 0 666 $g_tty`.  NetBSD's upstream
  template omits the group because NetBSD does not derive the tty gid from
  `/dev/ptmx`; that mechanism is MINIX-specific, so the group is required here.
  Machine-independent (would affect i386 too).  **Resolved:** after the
  live-image rebuild recreated `/dev` with the corrected `ptmx` group, test77
  passes end-to-end.

## Known limitation — test85 (vnd block-device EOF) deadlocks on a single-partition image

`test85` sets up a `vnd` whose backing file (`image`) is created in the test's
working directory (`/usr/tests/minix-posix`), then does block I/O on
`/dev/vnd0`.  On the dev-efi3 **emuimage**, everything lives on one MFS
partition (`distrib/amd64/liveimage/emuimage/fstab.in`: only `/dev/c0d0p1 /
mfs`), which makes the test deadlock on its first device read:

1. `read(/dev/vnd0)` — for an *unmounted* block device, VFS routes the block I/O
   to `ROOT_FS_E` (`servers/vfs/open.c` `v_bfs_e = ROOT_FS_E`).  The **root MFS**
   runs `lmfs_bio` → `bdev_gather`, a **synchronous** sendrec to vnd
   (`lib/libbdev/bdev.c` `bdev_vrdwt`), and blocks.
2. vnd services that request by `pread()`-ing its backing file `image` — which is
   on the **same root MFS**.
3. That `pread` queues behind the blocked root MFS → deadlock (single-threaded
   FS can't re-enter itself).

This is **not arch-specific** and not an LP64 bug: the vnd / libbdev /
libblockdriver / `lmfs_bio` data path all compute correctly on amd64 (iovec math
verified; `iovec_t` and `iovec_s_t` are both 16 bytes with matching field
offsets).  A stock MINIX install avoids the deadlock only because `/usr` is a
**separate** MFS process, so vnd's backing-file reads never touch the root MFS
that is doing the block caching.  i386 with the same single-partition emuimage
hangs identically.

Resolutions, if ever needed: give the emuimage a separate `/usr` partition (matches
stock MINIX, fixes the whole suite's `/usr != /` assumption), or host the vnd
backing file on a different FS instance (e.g. a ramdisk MFS).  Left as a known
limitation for now; the earlier `test85` *crash* (vnd `iovec_s_t`/`iov_grant`
sign-extension at config time) is fixed — see commit `25297885a`.

*(APIC was ported in a prior session; see `docs/apic-x86_64.md`.)*
