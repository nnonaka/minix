# VM Server x86_64 Port

Port of `minix/servers/vm` and supporting subsystems from i386 to x86_64 (AMD64).

## Design Overview

### Paging model

x86_64 uses 4-level paging: PML4 → PDPT → PD → PT.  Each level is a 4 KB page
of 512 × 8-byte entries (9-bit index fields).  CR3 holds the physical address of
the PML4.

The MINIX x86_64 kernel identity-maps all physical RAM at boot (`pg_identity`),
so for every physical address in low RAM: `phys == virt`.  This is the central
simplification that shapes the VM port.

### `pt_t` — 4-level page table struct (`minix/servers/vm/pt.h`)

```c
typedef struct {
    u64_t      *pt_pml4;               /* PML4 virtual address */
    phys_bytes  pt_pml4_phys;          /* PML4 physical address (→ CR3) */
    u64_t      *pt_pdpt;               /* single user PDPT (PML4[0]) */
    u64_t      *pt_pd[ARCH_PDPT_ENTRIES];   /* PD pages [pdpte] */
    u64_t     **pt_pt[ARCH_PDPT_ENTRIES];  /* PT pointer arrays [pdpte][pde] */
    vir_bytes   pt_virtop;
} pt_t;
```

`pt_pt[pdpte]` is a 4 KB page holding 512 `u64_t *` pointers — one per PD entry.
Each non-NULL pointer points to the virtual address of a PT page.  This is
allocated on demand per PDPT entry when the first PDE in that PDPT slot is mapped.

User space is limited to 512 GB (single PDPT under PML4[0]).  The kernel occupies
the upper half; PML4[511] is shared from `kern_pml4_hi` (see below).

### Identity-map approach (`minix/kernel/arch/x86_64/memory.c`)

On i386, `createpde()` transplants a PDE from the target process's page table into
the current page table's scratch window so the kernel can access it.  On x86_64
this is unnecessary:

- Physical addresses equal virtual addresses for all RAM.
- The kernel can dereference any page-table entry directly by casting its physical
  address to a pointer.

`createpde()` therefore walks PML4 → PDPT → PD → PT via identity-mapped pointers
and returns the resolved physical address, which is also valid as a virtual
address.  `mem_clear_mapcache()` is a no-op.

`vm_lookup()` uses the same direct-walk approach instead of `phys_get32`.

### `kern_pml4_hi` — kernel address space sharing

Every process must map the kernel.  On i386 this is done by writing big-page PDEs
into the process's page directory.  On x86_64 the kernel lives in the upper half
(above `0xFFFF800000000000`), accessible via PML4[511].

`pt_init()` saves `kern_pml4_hi = mypml4[511]` (the kernel's own PML4[511] entry,
pointing to the shared kernel PDPT).  `pt_mapkernel()` then writes this single
entry into every new process's PML4[511], giving it access to the kernel with no
other work.

The PDM (page-directory mapping) mechanism used on i386 to let the kernel inspect
process page directories is not needed on x86_64; `pt_allocate_kernel_mapped_pagetables()`
is a no-op.

### CR3 width fix (`minix/include/minix/com.h`)

`SVMCTL_PTROOT` was `m1_i3` (32-bit `int` in `mess_1`) — too narrow for a 64-bit
physical address.  Changed to `m1_ull1` (`uint64_t`), which is already present in
the `mess_1` union.  All three arch GET_PDBR handlers now write `SVMCTL_PTROOT`
instead of `SVMCTL_VALUE`.  `sys_vmctl_get_pdbr` was widened from `u32_t *` to
`phys_bytes *`.

## Files Modified

| File | Notes |
|------|-------|
| `minix/include/arch/x86_64/include/vm.h` | AMD64 paging constants, flag bits, index macros |
| `minix/include/arch/x86_64/include/multiboot.h` | New — `MULTIBOOT_MAX_MODS` used by VM server |
| `minix/include/arch/x86_64/include/Makefile` | Added `multiboot.h` to `INCS` |
| `minix/servers/vm/arch/x86_64/pagetable.h` | Rewritten — AMD64 constants, ARCH_PDPTE/PDE/PTE macros, ARCH_BIG_PAGE_SIZE=2 MB, ARCH_VM_DIR_ENTRIES=512 |
| `minix/servers/vm/pt.h` | Added `#ifdef __x86_64__` branch with 4-level `pt_t` |
| `minix/include/minix/com.h` | `SVMCTL_PTROOT` widened to `m1_ull1` |
| `minix/include/minix/syslib.h` | `sys_vmctl_get_pdbr` parameter widened to `phys_bytes *` |
| `minix/kernel/arch/x86_64/arch_do_vmctl.c` | `setcr3` widened; GET_PDBR writes `SVMCTL_PTROOT` |
| `minix/kernel/arch/i386/arch_do_vmctl.c` | GET_PDBR writes `SVMCTL_PTROOT` |
| `minix/kernel/arch/earm/arch_do_vmctl.c` | Same |
| `minix/lib/libsys/sys_vmctl.c` | Reads `SVMCTL_PTROOT` as `phys_bytes` |
| `minix/kernel/arch/x86_64/memory.c` | Identity-map `createpde`, 4-level `vm_lookup`, `vm_lookup_range` |
| `minix/servers/vm/pagetable.c` | All paging functions with `#ifdef __x86_64__` branches; `vm_allocpagedir()` defined |

## Function-level changes in `pagetable.c`

### New or replaced for x86_64

**`vm_allocpagedir(phys_bytes *)`** — new definition (was declared in `proto.h`
but undefined).  Calls `vm_allocpages(..., ARCH_PAGEDIR_SIZE/VM_PAGE_SIZE)`;
on x86_64 this is 1 page (4 KB).

**`pt_ptalloc(pt, pdpte, pde, flags)`** — x86_64 signature adds `pdpte` parameter
and widens `flags` to `u64_t`.  Allocates a PD page for `pt_pd[pdpte]` if absent,
a PT-pointer array for `pt_pt[pdpte]` if absent, then a PT page, and writes the
PDE into `pt_pd[pdpte][pde]`.

**`pt_ptalloc_in_range()`** — x86_64 path iterates `(pdpte, pde)` pairs across the
address range instead of `pde` alone.

**`ptestr()`** — x86_64 version takes `u64_t`; uses AMD64_VM_* flag names.

**`pt_map_in_range()`** — x86_64 path uses `(pdpte, pde, pte)` indexing.

**`pt_ptmap()`** — x86_64 maps PML4 + PDPT + all PD pages + all PT pages into the
destination process; no `pt_dir` or PDM windows used.

**`pt_writable()`** — x86_64 checks `pt->pt_pt[pdpte][pde][pte] & AMD64_VM_WRITE`.

**`pt_writemap()`** inner loop — x86_64 uses `u64_t entry` and 3-index access.

**`pt_checkrange()`** — x86_64 walks 3 levels.

**`pt_new()`** — x86_64 allocates PML4 (once, never moved) and PDPT (once, never
moved); wires `PML4[0] → PDPT`; calls `pt_mapkernel()`.

**`pt_allocate_kernel_mapped_pagetables()`** — no-op on x86_64.

**`pt_copy()`** — x86_64 iterates `(pdpte, pde)` pairs.

**`pt_init()`** — x86_64 path: gets 64-bit PDBR, saves `kern_pml4_hi = mypml4[511]`,
walks PML4[0..510] → PDPT → PD to copy VM's own PT pages via `sys_abscopy`.

**`pt_bind()`** — x86_64 calls `sys_vmctl_set_addrspace(ep, pt_pml4_phys, pt_pml4)`;
no PDM update.

**`pt_free()`** — x86_64 frees PT pages, PT-pointer arrays, PD pages, and PDPT in
order; PML4 is never freed (same policy as i386 `pt_dir`).

**`pt_mapkernel()`** — x86_64 writes `pt->pt_pml4[511] = kern_pml4_hi` and maps
kernel device regions via `pt_writemap`.

**`pt_sanitycheck()` / `pt_assert()`** — x86_64 checks `pt_pml4` / `pt_pml4_phys`.

## Address space layout

```
PML4 index   Virtual range             Use
-----------  -------------------------  ---
0            0x0000000000000000–        User space (PDPT → PD[512] → PT[512])
             0x0000007FFFFFFFFF
1..510       (unused)
511          0xFFFFFF8000000000–        Kernel (shared kern_pml4_hi entry)
             0xFFFFFFFFFFFFFFFF
```

User virtual address decode (each field is 9 bits):

```
Bits 47:39  → PML4 index  (always 0 for user; always 511 for kernel)
Bits 38:30  → PDPT index  = ARCH_VM_PDPTE(v) = AMD64_VM_PDPT(v)
Bits 29:21  → PD index    = ARCH_VM_PDE(v)   = AMD64_VM_PD(v)
Bits 20:12  → PT index    = ARCH_VM_PTE(v)   = AMD64_VM_PT(v)
Bits 11:0   → page offset
```

## Known limitations / future work

- User space is capped at 512 GB (single PDPT).  Extending to 256 TB requires
  making `pt_pdpt` an array indexed by PML4 index and adjusting `pt_ptalloc`,
  `pt_writemap`, etc.
- NX (no-execute) bit (`AMD64_VM_NX = 1ULL << 63`) is not propagated through
  `pt_writemap` because `flags` is `u32_t`.  Widening `flags` to `u64_t` throughout
  would enable NX support.
- `freepde()` / `kern_start_pde` are i386 concepts used unconditionally in
  `pt_init()` for kernel device-region virtual address assignment.  On x86_64 these
  yield low virtual addresses (N × 2 MB).  Kernel device regions should be placed
  in a dedicated portion of the address space once the kernel address layout is
  finalised.
- The linker error `strcspn undefined in libminc` is a pre-existing x86_64
  `libminc` build issue unrelated to this port.
