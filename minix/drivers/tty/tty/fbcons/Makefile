# Makefile for framebuffer console driver

SRCS +=	fbcons.c wscons_rop.c wsemu_vt100.c

DPADD+=	${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lchardriver -lsys -ltimers

# Needs kernel/const.h, etc
CPPFLAGS+=	-I${.CURDIR}	-I${NETBSDSRCDIR}

.include <minix.service.mk>

