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

## Remaining Issues

### Format string mismatches in storage drivers

On x86_64, `u64_t` is `unsigned long`, not `unsigned long long`.  Any `%llu` /
`%llx` used with a `u64_t` argument causes a `-Wformat` warning (treated as
error under `-Werror`) and potentially wrong output.

| File | Line | Format | Argument type | Fix |
|------|------|--------|---------------|-----|
| `minix/drivers/storage/ahci/ahci.c` | 392, 1426 | `%llu` | `u64_t` (lba_count × sector_size) | `PRIu64` or `(unsigned long long)` cast |
| `minix/drivers/storage/ahci/ahci.c` | 1154 | `%llx` | `u64_t pos` | `PRIx64` or cast |
| `minix/drivers/storage/virtio_blk/virtio_blk.c` | 279 | `%016llx` | `u64_t position` | `PRIx64` |
| `minix/drivers/storage/virtio_blk/virtio_blk.c` | 365 | `%llu` | `u64_t sector` | `PRIu64` |
| `minix/drivers/storage/filter/driver.c` | 776 | `%llx` | `u64_t pos` | `PRIx64` (inside `#if DEBUG2`) |

### MMIO virtual address truncation in ip1000 and vt6105

Both drivers have identical structure.  `pci_get_bar()` returns the device's
physical BAR address; the driver then calls `vm_map_phys()` to obtain a virtual
address and stores the result as:

```c
pdev->base[i] = (u32_t)reg;   /* reg = vm_map_phys(...) */
```

`pdev->base` is declared `u32_t base[6]`.  On x86_64, `vm_map_phys()` returns
a 64-bit virtual address; the explicit `(u32_t)` cast silently discards the
upper 32 bits.  Subsequent MMIO accesses via `ndr_in8(base[0], ...)` / `ndr_out8`
then go to the truncated address.

Fix: change `base[6]` to `vir_bytes base[6]` (or `uintptr_t`) in the per-device
struct and remove the `(u32_t)` cast.

Affected files:
- `minix/drivers/net/ip1000/ip1000.c:690`
- `minix/drivers/net/vt6105/vt6105.c:494`

### Already-correct drivers

| Driver | Notes |
|--------|-------|
| `rtl8169` | DMA descriptor uses `addr_low`/`addr_high` split — correct for 64-bit DMA |
| `rtl8139` | 32-bit DMA-only hardware; `u32_t` bus addresses are inherently limited to 4 GB |
| `dec21140A` | Uses I/O ports, not MMIO; all bus addresses are `u32_t` matching the hardware |
| `at_wini`, `floppy`, `virtio_net`, `dpeth` | No format or cast issues found |

## Deferred Work

**PCI BAR address widening**: `pb_base` in `bus/pci/pci.c` is `u32_t`, the IPC
reply field `mess_pci_lsys_busc_get_bar.base` in `ipc.h` is `int`, and
`pci_get_bar()` in `libsys/pci_get_bar.c` takes `u32_t *base`. On x86_64, MMIO
BARs above 4 GB would be silently truncated. All PCI MMIO drivers (`ahci`,
`at_wini`, `e1000`, etc.) store the BAR result in `u32_t` locals. Widening
requires changing the IPC message struct, `pci.c`, `pci_get_bar.c`, and every
caller — tracked as a follow-up.
