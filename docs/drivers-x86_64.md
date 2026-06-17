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
- `_pci_get_bar()` / `pci_get_bar()` / `syslib.h` declaration: `u32_t *base`
  → `u64_t *base`
- All callers widened: `ahci.c`, `atl2.c`, `3c90x.c`, `e1000.c`, `ip1000.c`,
  `vt6105.c`, `als4000.c`, `cmi8738.c`, `cs4281.c`, `trident.c`
- Audio driver MMIO chain also fixed: `io.h` `port` params, `base[6]` struct
  field, `*base` / scalar `base` function params, and `(vir_bytes)reg` cast
  throughout `als4000`, `cmi8738`, `cs4281`, `trident`
