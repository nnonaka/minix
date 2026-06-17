# $NetBSD: Makefile,v 1.8 2010/05/27 06:58:13 dholland Exp $

SUBDIR=		# biosboot not needed for UEFI boot

LIBOBJ= ${.OBJDIR}
.MAKEOVERRIDES+= LIBOBJ

.include <bsd.subdir.mk>
.include <bsd.obj.mk>

cleandir distclean: .WAIT cleanlibdir

cleanlibdir:
	-rm -rf lib
