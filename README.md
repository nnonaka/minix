# minix
My personal MINIX branch to support uefi boot.

Currently done

- build gpt partitoned live image
  - run `build -U -u -mi386 -O ../build release`
  - run `build -U -u -mi386 -O ../build live-image`
  - live disk image at ../build/distrib/i386/liveimage/emuimage/Minix-3.4.0-i386-live.img
- boot to efi boot loader (bootia32.efi)
- boot to minix kernel (multiboot2)

Not yet

- **show login prompt (sigh)**

