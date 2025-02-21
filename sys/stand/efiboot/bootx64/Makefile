#	$NetBSD: Makefile,v 1.1.26.2 2020/08/17 10:30:22 martin Exp $

PROG=		bootx64.efi
NEWVERSWHAT=	"EFI Boot (x64)"
CPUFLAGS=	-march=x86-64
GNUEFIARCH=	x86_64
LIBKERN_ARCH=	x86-64
LIBGNUEFI_ARCH=	x86_64
KLINK_MACHINE=	x86-64
OBJCOPY_FORMAT=	--subsystem efi-app	--target=pei-x86-64

EFIBOOT_ACPI=	yes
EFIBOOT_FDT=	yes
EFIBOOT_MULTIBOOT2=	yes

EXTRA_SOURCES=	efibootx64.c
EXTRA_SOURCES+=	startprog64.S
EXTRA_SOURCES+=	multiboot64.S

COPTS+=		-mno-red-zone
CPPFLAGS+=	-DEFI_FUNCTION_WRAPPER

CPPFLAGS+=	-DEFIBOOT_ALLOCATE_MAX_ADDRESS=0x100000000ULL
CFLAGS+=	-DACPI_64BIT_PHYSICAL_ADDRESS
CFLAGS+=	-DEFIBOOT_ALIGN=0x200000
CFLAGS+=	-DEFIBOOT_MODULE_MACHINE=\"x86_64\"

# Follow the suit of Makefile.kern.inc; needed for the lfs64 union
# accessors -- they don't actually dereference the resulting pointer,
# just use it for type-checking.
CWARNFLAGS.clang+=	-Wno-error=address-of-packed-member

.include "${.CURDIR}/../Makefile.efiboot"

release: check_RELEASEDIR
	${HOST_INSTALL_FILE} -m ${BINMODE} ${PROG} \
		${RELEASEDIR}/${RELEASEMACHINEDIR}/installation/misc
		
