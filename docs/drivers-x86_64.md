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

## Deferred Work

**PCI BAR address widening**: `pb_base` in `bus/pci/pci.c` is `u32_t`, the IPC
reply field `mess_pci_lsys_busc_get_bar.base` in `ipc.h` is `int`, and
`pci_get_bar()` in `libsys/pci_get_bar.c` takes `u32_t *base`. On x86_64, MMIO
BARs above 4 GB would be silently truncated. All PCI MMIO drivers (`ahci`,
`at_wini`, `e1000`, etc.) store the BAR result in `u32_t` locals. Widening
requires changing the IPC message struct, `pci.c`, `pci_get_bar.c`, and every
caller — tracked as a follow-up.
