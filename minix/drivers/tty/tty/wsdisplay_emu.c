/* $NetBSD: wsdisplay.c,v 1.158 2019/07/25 20:26:39 jmcneill Exp $ */

/*
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: wsdisplay.c,v 1.158 2019/07/25 20:26:39 jmcneill Exp $");

#ifdef _KERNEL_OPT
#include "opt_wsdisplay_compat.h"
#include "opt_wsmsgattrs.h"
#endif

//#include "wskbd.h"
//#include "wsmux.h"
#include "wsdisplay.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
//#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
//#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
//#include <sys/kauth.h>
#include <sys/sysctl.h>
#include <assert.h>

#include <wsconsio.h>
//#include <wseventvar.h>
//#include <wsmuxvar.h>
#include <wsdisplayvar.h>
//#include <wsksymvar.h>
//#include <wsksymdef.h>
#include <wsemulvar.h>
//#include <wscons_callbacks.h>
//#include <dev/cons.h>

//#include "locators.h"

struct wsscreen_internal {
	const struct wsdisplay_emulops *emulops;
	void	*emulcookie;

	const struct wsscreen_descr *scrdata;

	const struct wsemul_ops *wsemul;
	void	*wsemulcookie;
};

struct wsscreen {
	struct wsscreen_internal *scr_dconf;

	struct tty *scr_tty;
	int	scr_hold_screen;		/* hold tty output */

	int scr_flags;
#define SCR_OPEN 1		/* is it open? */
#define SCR_WAITACTIVE 2	/* someone waiting on activation */
#define SCR_GRAPHICS 4		/* graphics mode, no text (emulation) output */
#define	SCR_DUMBFB 8		/* in use as a dumb fb (iff SCR_GRAPHICS) */
	const struct wscons_syncops *scr_syncops;
	void *scr_synccookie;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	int scr_rawkbd;
#endif

#ifdef WSDISPLAY_MULTICONS
	callout_t scr_getc_ch;
#endif

	struct wsdisplay_softc *sc;

#ifdef DIAGNOSTIC
	/* XXX this is to support a hack in emulinput, see comment below */
	int scr_in_ttyoutput;
#endif
};

#define	WSDISPLAYUNIT(dev)	(minor(dev) >> 8)
#define	WSDISPLAYSCREEN(dev)	(minor(dev) & 0xff)
#define ISWSDISPLAYSTAT(dev)	(WSDISPLAYSCREEN(dev) == 254)
#define ISWSDISPLAYCTL(dev)	(WSDISPLAYSCREEN(dev) == 255)
#define WSDISPLAYMINOR(unit, screen)	(((unit) << 8) | (screen))

#define	WSSCREEN_HAS_EMULATOR(scr)	((scr)->scr_dconf->wsemul != NULL)
#define	WSSCREEN_HAS_TTY(scr)	((scr)->scr_tty != NULL)


/*
 * Callbacks for the emulation code.
 */
void
wsdisplay_emulbell(void *v)
{
	struct wsscreen *scr = v;

	if (scr == NULL)		/* console, before real attach */
		return;

	if (scr->scr_flags & SCR_GRAPHICS) /* can this happen? */
		return;

	//(void) wsdisplay_internal_ioctl(scr->sc, scr, WSKBDIO_BELL, NULL,
	//				FWRITE, NULL);
}

void
wsdisplay_emulinput(void *v, const u_char *data, u_int count)
{
	struct wsscreen *scr = v;
	struct tty *tp;
	int (*ifcn)(int, struct tty *);

	if (v == NULL)			/* console, before real attach */
		return;

	if (scr->scr_flags & SCR_GRAPHICS) /* XXX can't happen */
		return;
	if (!WSSCREEN_HAS_TTY(scr))
		return;

	tp = scr->scr_tty;

	/*
	 * XXX bad hack to work around locking problems in tty.c:
	 * ttyinput() will try to lock again, causing deadlock.
	 * We assume that wsdisplay_emulinput() can only be called
	 * from within wsdisplaystart(), and thus the tty lock
	 * is already held. Use an entry point which doesn't lock.
	 */
/* TODO
	assert(scr->scr_in_ttyoutput);
	ifcn = tp->t_linesw->l_rint;
	if (ifcn == ttyinput)
		ifcn = ttyinput_wlock;

	while (count-- > 0)
		(*ifcn)(*data++, tp);
*/
}

