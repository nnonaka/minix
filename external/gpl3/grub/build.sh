#!/bin/sh

./bootstrap
./configure --with-platform=efi --target=i386
make
cd grub-core
../grub-mkimage -v -d . -o booti386.efi -O i386-efi -p /boot/efi normal part_msdos fat chain boot configfile multiboot minix3 gzio efi_uga
ls -l booti386.efi

