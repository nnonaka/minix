# minix
My personal MINIX branch to support uefi boot.

Develop enviromment
- cross-compile on Ubuntu 24.04
- runs on qemu-system-i386/ovmf-ia32

Currently done.
- build gpt partitoned live image
  - fetch texinfo and binutils sources
  - run `./build.sh -U -u -mi386 -O ../build release`
  - run `./build.sh -U -u -mi386 -O ../build live-image`
  - live disk image at ../build/distrib/i386/liveimage/emuimage/Minix-3.4.0-i386-live.img
- boot to efi boot loader (bootia32.efi)
- boot to minix kernel (multiboot2)
- show login prompt

Not yet.
- self compile
- MINIX installer for uefi/gpt


