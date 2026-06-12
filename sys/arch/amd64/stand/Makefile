#	$NetBSD: Makefile,v 1.27 2012/01/27 08:41:35 jnemeth Exp $

.if defined(__MINIX)
SUBDIR=		boot
.else
SUBDIR=		mbr boot
.endif
#SUBDIR+=	cdboot # fatboot
#SUBDIR+=	dosboot misc
#SUBDIR+=	pxeboot
#SUBDIR+=	efiboot
SUBDIR+= ../../../stand/efiboot/bootx64

.include <bsd.subdir.mk>
