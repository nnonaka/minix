# Makefile for arch-dependent TTY code
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:  ${HERE}

SRCS +=	keyboard.c rs232.c

.if ${USE_BIOS} != "no"
SRCS+= console.c
CPPFLAGS+= -DUSE_BIOS
.else
SRCS+= fb_console.c
.endif

