#	$NetBSD: Makefile,v 1.30 2018/12/27 16:26:52 christos Exp $

.include <bsd.hostinit.mk>

MODULE=		grub

GNUHOSTDIST=	${.CURDIR}/../../external/gpl3/grub/dist

CONFIGURE_ARGS=	--with-platform=efi --target=i386

.if defined(__MINIX) && ! exists(${GNUHOSTDIST}/README)
# MINIX: LSC: Make sure we trigger the fetch rule
.include "${.CURDIR}/../../minix/Makefile.fetchgnu"

${GNUHOSTDIST}/README: ${fetch_done}
	@true
.endif # defined(__MINIX)

build/README: ${GNUHOSTDIST}/README
	@mkdir build 2>/dev/null || true
	cat ${GNUHOSTDIST}/README > ${.TARGET}

.configure_done: build/README

	
.include "${.CURDIR}/../Makefile.gmakehost"
BUILD_MAKE=${TOOL_GMAKE}

