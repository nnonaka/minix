#!/usr/bin/env bash
set -e

#
# This script creates a bootable image and should at some point in the future
# be replaced by the proper NetBSD infrastructure.
#

: ${ARCH=amd64}
: ${OBJ=../obj.${ARCH}}
: ${TOOLCHAIN_TRIPLET=x86-64-elf64-minix-}
: ${BUILDSH=build.sh}

: ${SETS="minix-base minix-comp minix-games minix-man minix-tests tests"}
: ${IMG=minix_x86_64.img}

if [ ! -f ${BUILDSH} ]
then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

# we create a disk image of about 2 gig's
# for alignment reasons, prefer sizes which are multiples of 4096 bytes
: ${BOOTXX_SECS=2048}
: ${GPT_SECS=2048}
: ${ROOT_SIZE=$((  128*1024*1024 ))}
: ${HOME_SIZE=$((  128*1024*1024 ))}
: ${USR_SIZE=$((  1500*1024*1024 ))}
: ${EFI_SIZE=$((  128*1024*1024 ))}

# set up disk creation environment
. releasetools/image.defaults
. releasetools/image.functions

echo "Building work directory..."
build_workdir "$SETS"

echo "Adding extra files..."
workdir_add_hdd_files

# add kernels
add_link_spec "boot/minix_latest" "minix_default" extra.kernel
workdir_add_kernel minix_default
workdir_add_kernel minix/$RELEASE_VERSION

# add boot.cfg
cat >${ROOT_DIR}/boot.cfg <<END_BOOT_CFG
menu=Start MINIX 3:load_mods /boot/minix_default/mod*; multiboot /boot/minix_default/kernel rootdevname=c0d0p0
menu=Start latest MINIX 3:load_mods /boot/minix_latest/mod*; multiboot /boot/minix_latest/kernel rootdevname=c0d0p0
menu=Start latest MINIX 3 in single user mode:load_mods /boot/minix_latest/mod*; multiboot /boot/minix_latest/kernel rootdevname=c0d0p0 bootopts=-s
menu=Start MINIX 3 ALIX:load_mods /boot/minix_default/mod*;multiboot /boot/minix_default/kernel rootdevname=c0d0p0 console=tty00 consdev=com0 ata_no_dma=1
menu=Edit menu option:edit
menu=Drop to boot prompt:prompt
clear=1
timeout=5
default=2
menu=Start MINIX 3 ($RELEASE_VERSION):load_mods /boot/minix/$RELEASE_VERSION/mod*; multiboot /boot/minix/$RELEASE_VERSION/kernel rootdevname=c0d0p0
END_BOOT_CFG
add_file_spec "boot.cfg" extra.boot

echo "Bundling packages..."
bundle_packages "$BUNDLE_PACKAGES"

echo "Creating specification files..."
create_input_spec
create_protos "usr home"

# Clean image
if [ -f ${IMG} ]	# IMG might be a block device
then
	rm -f ${IMG}
fi

#
# Generate /root, /usr and /home partition images.
#
echo "Writing disk image..."

# all sizes are written in 512 byte blocks
ROOTSIZEARG="-b $((${ROOT_SIZE} / 512 / 8))"
USRSIZEARG="-b $((${USR_SIZE} / 512 / 8))"
HOMESIZEARG="-b $((${HOME_SIZE} / 512 / 8))"

_BOOT_SIZE=$((${BOOTXX_SECS} + ${GPT_SECS}))
BOOT_SIZE=$((${_BOOT_SIZE} * 512))
dd if=/dev/zero bs=${BOOT_SIZE} count=1 > ${OBJ}/boot.img

if [ ${EFI_SIZE} -ge 512 ]
then
    fetch_and_build_grub

    : ${EFI_DIR=$OBJ/efi}
    rm -rf ${EFI_DIR} && mkdir -p ${EFI_DIR}/EFI/boot ${EFI_DIR}/EFI/boot/minix_default 
    create_grub_cfg
    cp ${MODDIR}/* ${EFI_DIR}/EFI/boot/minix_default/
    cp ${RELEASETOOLSDIR}/grub/grub-core/booti386.efi ${EFI_DIR}/EFI/boot/bootia32.efi
    cp ${RELEASETOOLSDIR}/grub/grub-core/*.mod ${EFI_DIR}/EFI/boot
    
    dd if=/dev/zero bs=${EFI_SIZE} count=1 > ${OBJ}/efi.img
    
    echo " * EFI"
    ${CROSS_TOOLS}/nbmakefs -t msdos -s ${EFI_SIZE} -o "F=32,c=1" ${OBJ}/efi.img ${EFI_DIR}
    #dd if=${OBJ}/efi.img >> ${IMG}
    cat ${OBJ}/boot.img ${OBJ}/efi.img > ${IMG}

    EFI_START=${_BOOT_SIZE}
    _EFI_SIZE=$(($EFI_SIZE / 512))
    ROOT_START=$((${EFI_START} + ${_EFI_SIZE}))
else
    ROOT_START=${_BOOT_SIZE}
fi

ROOT_START=${BOOTXX_SECS}
echo " * ROOT"
_ROOT_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs -d ${ROOTSIZEARG} -I $((${ROOT_START}*512)) ${IMG} ${WORK_DIR}/proto.root)
_ROOT_SIZE=$(($_ROOT_SIZE / 512))
USR_START=$((${ROOT_START} + ${_ROOT_SIZE}))
echo " * USR"
_USR_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs  -d ${USRSIZEARG}  -I $((${USR_START}*512))  ${IMG} ${WORK_DIR}/proto.usr)
_USR_SIZE=$(($_USR_SIZE / 512))
HOME_START=$((${USR_START} + ${_USR_SIZE}))
echo " * HOME"
_HOME_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs -d ${HOMESIZEARG} -I $((${HOME_START}*512)) ${IMG} ${WORK_DIR}/proto.home)
_HOME_SIZE=$(($_HOME_SIZE / 512))

#
# Write the partition table using the natively compiled
# minix partition utility
#
if [ ${EFI_SIZE} -ge 512 ]
then
    mv ${IMG} ${IMG}.work
    cat ${IMG}.work ${OBJ}/boot.img > ${IMG}
    rm ${IMG}.work
	${CROSS_TOOLS}/nbgpt ${IMG} create -f
	${CROSS_TOOLS}/nbgpt ${IMG} add -b ${EFI_START} -s ${_EFI_SIZE} -t efi -l "EFI system"
	${CROSS_TOOLS}/nbgpt ${IMG} add -s ${_ROOT_SIZE} -t ffs
	${CROSS_TOOLS}/nbgpt ${IMG} add -s ${_USR_SIZE} -t ffs
	${CROSS_TOOLS}/nbgpt ${IMG} add -s ${_HOME_SIZE} -t ffs
else
    ${CROSS_TOOLS}/nbpartition -m ${IMG} ${BOOTXX_SECS} 81:${_ROOT_SIZE}* 81:${_USR_SIZE} 81:${_HOME_SIZE}
    ${CROSS_TOOLS}/nbinstallboot -f -m ${ARCH} ${IMG} ${DESTDIR}/usr/mdec/bootxx_minixfs3
fi

echo "Convert to vmdk"
qemu-img convert -f raw -O vmdk ${IMG} minix_x86.vmdk

echo ""
echo "Disk image at `pwd`/${IMG}"
echo ""
echo "To boot this image on kvm using the bootloader:"
echo "qemu-system-i386 --enable-kvm -m 256 -hda `pwd`/${IMG}"
echo ""
echo "To boot this image on kvm:"
echo "cd ${MODDIR} && qemu-system-i386 --enable-kvm -m 256M -kernel kernel -append \"rootdevname=c0d0p0\" -initrd \"${mods}\" -hda `pwd`/${IMG}"
echo "To boot this image on kvm with EFI (tianocore OVMF):"
echo "qemu-system-i386 -L . -bios OVMF-i32.fd -m 256M -drive file=minix_x86.img,if=ide,format=raw"
