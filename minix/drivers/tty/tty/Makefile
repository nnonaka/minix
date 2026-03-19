# Makefile for terminal driver (TTY)
PROG=	tty

.include "arch/${MACHINE_ARCH}/Makefile.inc"


SRCS +=	tty.c

.include "Makefile.fbcons.inc"

DPADD+=	${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lchardriver -lsys -ltimers

SUBDIR= keymaps fbcons

# Needs kernel/const.h, etc
CPPFLAGS+=	-I${.CURDIR}	-I${NETBSDSRCDIR}

.include <minix.service.mk>
.include <bsd.subdir.mk>
