#	$NetBSD: Makefile,v 1.4 2018/12/15 18:03:17 gson Exp $

LIVEIMGBASE=	Minix-${DISTRIBVER}-i386-live	# gives ${IMGBASE}.img

EMUIMAGEMB?=	1536            	# 2GB USB flash memories aren't 2GiB
LIVEIMAGEMB=	${EMUIMAGEMB}

BUILDEXTRA+=	boot.cfg		# Add boot.cfg file
CLEANFILES+=	boot.cfg

prepare_md_post:
	${TOOL_SED} "s/@@VERSION@@/${DISTRIBVER}/" < ${.CURDIR}/boot.cfg.in > boot.cfg


.include "${.CURDIR}/../Makefile.liveimage"
