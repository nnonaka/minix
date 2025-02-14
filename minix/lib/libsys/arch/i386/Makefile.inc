# Makefile for arch-dependent libsys code
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:  ${HERE}

SRCS+=  \
        get_randomness.c \
        getidle.c \
        ser_putc.c \
        spin.c \
        sys_eniop.c \
        sys_in.c \
        sys_out.c \
        sys_sdevio.c \
        sys_umap_remote.c \
        sys_vinb.c \
        sys_vinl.c \
        sys_vinw.c \
        sys_voutb.c \
        sys_voutl.c \
        sys_voutw.c \
        tsc_util.c \
        vbox.c

.if ${USE_BIOS} != "no"
SRCS+= sys_readbios.c
CPPFLAGS+= -DUSE_BIOS
.endif

CPPFLAGS+=        -I${HERE}/../../


