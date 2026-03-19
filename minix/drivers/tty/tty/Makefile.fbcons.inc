# Makefile for framebuffer console driver

.include <bsd.own.mk>

SRCS +=	wsemul_vt100.c
SRCS +=	wsemul_vt100_subr.c
SRCS +=	wscons_rinit.c 
SRCS +=	wscons_rops.c
SRCS +=	raster_op.c
SRCS +=	raster_text.c
SRCS += wsdisplay_emu.c


