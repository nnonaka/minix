# x86_64 Driver Porting

## Overview

The MINIX driver build system originally gated the essential x86 driver set behind
`${MACHINE_ARCH} == "i386"`, leaving x86_64 with only the architecture-agnostic
drivers (memory, ramdisk, vnd, log, random). This document describes the changes
made to enable the essential driver subset for x86_64.

## Scope

Drivers enabled for x86_64 (essential x86 subset):

| Category | Drivers |
|----------|---------|
| Bus | `pci`, `ti1225` |
| Storage | `ahci`, `at_wini`, `floppy`, `fbd`, `filter`, `virtio_blk` |
| Network | `3c90x`, `atl2`, `dec21140A`, `dp8390`, `dpeth`, `e1000`, `fxp`, `ip1000`, `lance`, `rtl8139`, `rtl8169`, `virtio_net`, `vt6105` |
| HID | `pckbd` |
| Power | `acpi` |
| TTY | already built unconditionally; `bios_console` removed from x86_64 |

Not included: `audio`, `printer`, `iommu/amddev`, `vmm_guest/vbox`.

## Files Changed

### Makefile MACHINE_ARCH guards

Each `.if ${MACHINE_ARCH} == "i386"` block was extended to
`.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"`:

| File | Notes |
|------|-------|
| `minix/drivers/bus/Makefile` | `pci`, `ti1225` blocks |
| `minix/drivers/storage/Makefile` | two blocks: MKIMAGEONLY-gated + unconditional |
| `minix/drivers/net/Makefile` | all net drivers block |
| `minix/drivers/hid/Makefile` | `pckbd` block |
| `minix/drivers/power/Makefile` | `acpi` block |
| `minix/drivers/storage/ramdisk/Makefile` | PROGRAMS block; `rs.single` kept i386-only (real-mode BIOS) |

### Inline `#ifdef` guards

`#if defined(__i386__)` extended to `#if defined(__i386__) || defined(__x86_64__)`:

| File | What was re-enabled |
|------|---------------------|
| `minix/drivers/tty/tty/arch/x86_64/rs232.c` | `port_t` struct fields, `addr_8250[]` table, B50/B1800/B38400–B115200 baud rates |
| `minix/drivers/storage/memory/memory.c` | `sys_enable_iop()` for `/dev/mem` |
| `minix/drivers/storage/ramdisk/proto` | storage/HID/isofs/acpi binaries in ramdisk image |

### Build system MK* variables (`share/mk/bsd.own.mk`)

`MKPCI`, `MKACPI`, `MKAPIC`, and `MKDEBUGREG` were defaulted `yes` only for
`i386`, so on x86_64 `libsys` skipped all 18 `pci_*.c` client stubs and any
driver calling `pci_init()` / `pci_get_bar()` etc. failed to link. The fix
extends the `_MKVARS.yes` block to `i386 || x86_64` for those four variables.
`MKWATCHDOG` and `MKINSTALLBOOT` remain i386-only.

Side effect: `MKACPI=yes` on x86_64 now causes the ramdisk Makefile's
`${MKACPI} != "no"` guard (already inside the `i386 || x86_64` block) to
include the `acpi` service in the ramdisk image.

### TTY x86_64 arch

`minix/drivers/tty/tty/arch/x86_64/Makefile.inc`: removed `bios_console.c`
(BIOS video services are not available under UEFI/64-bit boot).

### Type fixes in driver sources

| File | Fix |
|------|-----|
| `minix/drivers/bus/pci/pci.c` | `(u32_t)` cast on `kinfo.mem_high_phys` (`phys_bytes` = `unsigned long`, 64-bit on x86_64) in `complete_bars()` — silenced implicit truncation warning |
| `minix/drivers/net/3c90x/3c90x.c` | Pointer cast via `uintptr_t`; format string fixes |
| `minix/drivers/net/atl2/atl2.c` | Pointer cast via `uintptr_t` |
| `minix/drivers/net/dp8390/dp8390.c` | Pointer cast via `uintptr_t` |
| `minix/drivers/net/e1000/e1000.c` | Pointer cast via `uintptr_t` |
| `minix/drivers/net/fxp/fxp.c` | Format string fixes |
| `minix/drivers/net/lance/lance.c` | Pointer cast via `uintptr_t`; format string fixes |

## Format string mismatches in storage drivers *(fixed)*

On x86_64, `u64_t` is `unsigned long`, not `unsigned long long`.  `%llu` /
`%llx` with a `u64_t` argument causes a `-Wformat` error.  All five cases were
fixed by replacing with `PRIu64` / `PRIx64` macros (and `#include <inttypes.h>`
where missing):

| File | Line | Was | Fix |
|------|------|-----|-----|
| `minix/drivers/storage/ahci/ahci.c` | 392, 1426 | `%llu` | `%"PRIu64"` |
| `minix/drivers/storage/ahci/ahci.c` | 1154 | `%llx` | `%"PRIx64"` |
| `minix/drivers/storage/virtio_blk/virtio_blk.c` | 279 | `%016llx` | `%016"PRIx64"` |
| `minix/drivers/storage/virtio_blk/virtio_blk.c` | 365 | `%llu` | `%"PRIu64"` |
| `minix/drivers/storage/filter/driver.c` | 776 | `%llx` | `%"PRIx64"` (inside `#if DEBUG2`) |

### MMIO virtual address truncation in ip1000 and vt6105 *(fixed)*

Both drivers called `vm_map_phys()` to map device MMIO and stored the 64-bit
virtual address via an explicit `(u32_t)` cast, silently discarding the upper
32 bits.  The truncated address was then used for all register accesses.

**Fix applied**: widened the entire MMIO address chain from `u32_t` to
`vir_bytes` (`unsigned long`, 64-bit on x86_64) throughout both drivers:

- `NDR_driver.base[6]` (`ip1000.h`, `vt6105.h`): `u32_t` → `vir_bytes`
- `base0` locals and `*base` parameters in all functions (`ip1000.c`, `vt6105.c`)
- `read_eeprom`, `read_phy_reg`, `write_phy_reg` base parameters (`ip1000.c`)
- Assignment: `pdev->base[i] = (vir_bytes)reg` (removing the truncating cast)
- `my_inb/inw/inl/outb/outw/outl` `port` parameters (`ip1000/io.h`, `vt6105/io.h`)

The `(volatile u8_t *)(port)` casts inside the io.h functions continue to work
correctly once `port` carries the full 64-bit VA.

### Already-correct drivers

| Driver | Notes |
|--------|-------|
| `rtl8169` | DMA descriptor uses `addr_low`/`addr_high` split — correct for 64-bit DMA |
| `rtl8139` | 32-bit DMA-only hardware; `u32_t` bus addresses are inherently limited to 4 GB |
| `dec21140A` | Uses I/O ports, not MMIO; all bus addresses are `u32_t` matching the hardware |
| `at_wini`, `floppy`, `virtio_net`, `dpeth` | No format or cast issues found |

## PCI BAR address widening *(fixed)*

`pb_base` in `bus/pci/pci.c`, the IPC reply field
`mess_pci_lsys_busc_get_bar.base`, and `pci_get_bar()` were all 32-bit,
truncating MMIO BARs above 4 GB on x86_64.

**Fix applied**:

- `mess_pci_lsys_busc_get_bar.base`: `int` → `uint64_t`; `size_t size` →
  `uint32_t size`; `padding[44]` → `padding[40]` (maintains 56-byte size on
  i386; fits within 88-byte x86_64 payload)
- `pb_base` in `pci.c` struct: `u32_t` → `u64_t`
- `record_bar()`: `bar_high` saves the upper 32-bit DWORD for 64-bit BARs;
  `pb_base` is set to `bar | ((u64_t)bar_high << 32)`.  On i386 the original
  "ignore BAR if high bits set" guard is kept via `#if !defined(__x86_64__)`.
- `complete_bars()` 32-bit gap loops: skip BARs with `pb_base > 0xFFFFFFFFULL`.
- **`PBF_INCOMPLETE` fix** (`pci.c:1108`): the incomplete check was
  `if (bar == 0)`, testing only the low 32 bits.  A 64-bit BAR whose firmware
  address has its low DWORD equal to zero (e.g. a BAR at exactly `0x100000000`)
  has `bar == 0` but `bar_high == 1`, so `pb_base = 0x100000000`.  The old
  check wrongly set `PBF_INCOMPLETE`, and `complete_bars()` then skipped it
  (because `pb_base > 0xFFFFFFFF`), leaving `PBF_INCOMPLETE` set forever;
  `_pci_get_bar()` returned `EINVAL` and the device failed to initialise.
  Fixed by testing the full 64-bit combined value:
  ```c
  if ((bar | ((u64_t)bar_high << 32)) == 0)
  ```
- `_pci_get_bar()` / `pci_get_bar()` / `syslib.h` declaration: `u32_t *base`
  → `u64_t *base`
- All callers widened: `ahci.c`, `atl2.c`, `3c90x.c`, `e1000.c`, `ip1000.c`,
  `vt6105.c`, `als4000.c`, `cmi8738.c`, `cs4281.c`, `trident.c`
- Audio driver MMIO chain also fixed: `io.h` `port` params, `base[6]` struct
  field, `*base` / scalar `base` function params, and `(vir_bytes)reg` cast
  throughout `als4000`, `cmi8738`, `cs4281`, `trident`

## `paddr_t` in wsdisplayvar.h *(fixed for i386 and amd64)*

`minix/drivers/tty/tty/wscons/wsdisplayvar.h` uses `paddr_t` in a function
pointer declaration (the `mmap` member of `struct wsdisplay_accessops`).
`paddr_t` is a kernel/privileged type whose availability differs by arch:

| Arch | Condition for `paddr_t` in `machine/types.h` |
|------|----------------------------------------------|
| i386 | `_NETBSD_SOURCE && !_KERNEL` → `__uint64_t` (always in userland) |
| amd64 | `_NETBSD_SOURCE && (_KERNEL \|\| _KMEMUSER \|\| _KERNTYPES \|\| _STANDALONE)` only |

**i386 problem**: the old fallback `typedef unsigned long paddr_t` conflicted
with the `__uint64_t` definition already provided by `machine/types.h` (pulled
in via `sys/device.h` → `sys/mutex.h` → `sys/types.h` → `machine/types.h`).

**amd64 problem**: in MINIX userspace drivers none of `_KERNEL`, `_KMEMUSER`,
`_KERNTYPES`, or `_STANDALONE` is defined, so `machine/types.h` does not
define `paddr_t` at all.  The old fallback was also gated on `!defined(_KERNEL)
&& !defined(_KERNTYPES)`, so it would have fired — but `unsigned long` is the
correct 64-bit type on amd64 anyway.  The conflict arose only on i386.

**Fix** (`wsdisplayvar.h` lines 38–47):

```c
/* Ensure paddr_t is defined. i386 machine/types.h provides it in userspace;
 * amd64 gates it on _KERNEL/_KERNTYPES, so provide a fallback for MINIX
 * userspace drivers. */
#if !defined(_I386_MACHTYPES_H_) && !defined(_X86_64_TYPES_H_)
#include <machine/types.h>
#endif
#if defined(__x86_64__) && \
    !defined(_KERNEL) && !defined(_KMEMUSER) && !defined(_KERNTYPES) && \
    !defined(_STANDALONE)
typedef unsigned long paddr_t;
#endif
```

- The explicit `#include <machine/types.h>` (guarded by each arch's include
  guard) ensures i386 userspace gets its `__uint64_t` definition without
  conflict.
- The `__x86_64__` + no-kernel-flags fallback provides `unsigned long`
  (64-bit) for amd64 MINIX userspace drivers where `machine/types.h` would
  otherwise leave `paddr_t` undefined.
- Kernel builds on both arches already have `paddr_t` from their own include
  chain; neither branch interferes.

## AHCI / SATA bring-up on q35/UEFI *(fixed)*

q35 (the default UEFI machine on QEMU/KVM) has **no legacy IDE**: its disk is
AHCI/SATA, so `at_wini` (legacy PIO, ports 0x1F0/0x170) reads 0x00 status and
fails IDENTIFY. Booting from disk under pure UEFI therefore requires the `ahci`
driver, which had several latent LP64 bugs that only surfaced on amd64. The
driver is started in place of `at_wini` by `ahci=yes` (ramdisk `rc`); the disk
is the built-in q35 SATA controller at PCI 00:1f.2, port 0.

Full chain of fixes (all in `minix/drivers/storage/ahci/ahci.c` unless noted).
Note PCI interrupt routing (ACPI `_PRT` + IOAPIC GSI) is a prerequisite and is
documented in `apic-x86_64.md`.

### `iovec_s_t` type pun — the root-mount blocker

`bdr_transfer` (libblockdriver) passes an `iovec_t`, whose `iov_addr`
(`vir_bytes`, 64-bit) holds **either** a local virtual address (when
`endpt == SELF`) **or** a grant ID. `at_wini` reads `iov_addr` directly. AHCI
instead cast the vector to `iovec_s_t *` and read `iov_grant`:

```c
struct { vir_bytes iov_addr;  vir_bytes iov_size; } iovec_t;     /* offset 0: 8 bytes */
struct { cp_grant_id_t iov_grant; vir_bytes iov_size; } iovec_s_t; /* offset 0: 4 bytes */
```

On **i386** `iov_addr` and `iov_grant` are both 4 bytes at offset 0, so the pun
was harmless. On **amd64** `iov_grant` is the *low 32 bits* of the 64-bit
`iov_addr`, and the SELF path then sign-extended it:

```diff
-static int setup_prdt(... iovec_s_t *iovec ...)
+static int setup_prdt(... iovec_t *iovec ...)
 ...
-	if (endpt == SELF)
-		vvec[i].vv_addr = (vir_bytes) iovec[i].iov_grant;
-	else
-		vvec[i].vv_grant = iovec[i].iov_grant;
+	if (endpt == SELF)
+		vvec[i].vv_addr = iovec[i].iov_addr;
+	else
+		vvec[i].vv_grant = (cp_grant_id_t) iovec[i].iov_addr;
```

A SELF buffer at a high user address (`0xefbae000`, bit 31 set) became
`0xffffffffefbae000`, so `sys_vumap` → VM `handle_memory` found no region and
returned `EFAULT`. This hit the **partition-table read at open** (drvlib
`partition()` issues a `SELF` transfer into a high `mmap`'d buffer), so the
device never opened and root never mounted. Grants kept working throughout
because grant IDs are small positive ints (no high bits, no sign extension).

The fix changes `sum_iovec`, `setup_prdt`, and `port_transfer` parameters from
`iovec_s_t *` to `iovec_t *` and drops the `(iovec_s_t *)` cast in
`ahci_transfer`. `iov_size` is at the same offset in both structs, so the
size-summing path was unaffected.

### DMA descriptor 64-bit address fields

The AHCI HBA structures carry 64-bit physical addresses as low/high DWORD
pairs, but the driver wrote only the low half and hardcoded the upper half to
0. On amd64 `phys_bytes` is 64-bit, so any buffer above 4 GB (or, with
`alloc_contig`, anywhere) would DMA to the wrong address.

| Field | Location | Fix |
|-------|----------|-----|
| PRDT `DBAU` (data base, 63:32) | `ct_set_prdt` | was `*p++ = 0` → `*p++ = (u32_t)((u64_t)prdt->vp_addr >> 32)` |
| Cmd-list header `CTBAU` (63:32) | `port_set_cmd` | added `cl[3] = (u32_t)((u64_t)ps->ct_phys[cmd] >> 32)` |
| Port `FBU` (FIS base, 63:32) | `port_alloc` | `port_write(ps, AHCI_PORT_FBU, (u32_t)((u64_t)ps->fis_phys >> 32))` |
| Port `CLBU` (cmd-list base, 63:32) | `port_alloc` | `port_write(ps, AHCI_PORT_CLBU, (u32_t)((u64_t)ps->cl_phys >> 32))` |

The `(u64_t)` cast before `>> 32` keeps the shift well-defined on i386 (where
`phys_bytes` is 32-bit, the cast yields 0). Below 4 GB the upper halves are 0,
identical to the old behaviour, so there is no i386 regression. Above 4 GB they
program correctly on S64A-capable controllers (QEMU's ICH9 AHCI supports it).

### Device detection without a connect-change interrupt

Detection keyed solely on the **PxIS.PCS** bit (Port Connect *change* Status).
On QEMU's ich9 the disk is present and stable from power-on, so no
connect-change is ever latched (`PxIS.PCS == 0`) even though `PxSSTS.DET == 3`
plainly shows an established device. The spin-up-timeout path then wrongly
concluded `STATE_NO_DEV`.

```diff
-		if (port_read(ps, AHCI_PORT_IS) & AHCI_PORT_IS_PCS) {
+		if ((port_read(ps, AHCI_PORT_IS) & AHCI_PORT_IS_PCS) ||
+			(port_read(ps, AHCI_PORT_SSTS) & AHCI_PORT_SSTS_DET_MASK)
+				== AHCI_PORT_SSTS_DET_PHY) {
 			/* device is present; poll it instead of giving up */
 			ps->state = STATE_WAIT_DEV;
 			...
```

This generalises the existing "bad controller, no interrupt" (VirtualBox)
fallback to any controller that presents `SSTS.DET == DET_PHY` without raising
the connect interrupt.

### PCI command register

`pci_reserve()` adds the device's I/O/memory/IRQ to the driver's privileges but
does **not** touch the PCI command register. `ahci_init` now explicitly enables
memory space + bus mastering and clears the Interrupt-Disable bit (bit 10),
rather than relying on firmware state:

```c
u16_t cr = pci_attr_r16(devind, PCI_CR);
cr |= PCI_CR_MEM_EN | PCI_CR_MAST_EN;	/* mem space + busmaster (DMA) */
cr &= ~PCI_CR_INT_DIS;			/* enable INTx */
pci_attr_w16(devind, PCI_CR, cr);
```

`PCI_CR_INT_DIS` (0x0400) is defined locally in `ahci.c` (it is not in
`machine/pci.h`). On OVMF this is a no-op (CR is already `0x0007`), but it is
correct not to depend on firmware leaving INTx/bus-master enabled.
