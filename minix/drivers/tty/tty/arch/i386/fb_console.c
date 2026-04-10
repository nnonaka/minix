/* 
 * dummy serial console output
 * TODO complete fb console
 */

#include <minix/drivers.h>
#include <termios.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/video.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/sys_config.h>
#include <minix/vm.h>
#include "tty.h"
#include "console.h"
#include "raster.h"
#include "wscons_raster.h"
#include "wsdisplayvar.h"
#include "wsemulvar.h"

/* Set this to 1 if you want console output duplicated on the first
 * serial line.
  */
#define DUP_CONS_TO_SER	1

#define	KASSERT(x)	assert(x)


extern const struct wsemul_ops wsemul_vt100_ops;
extern const struct wsemul_ops wsemul_dumb_ops;

const struct wsdisplay_emulops fbcons_emulops = {
	rcons_cursor,
	rcons_mapchar,
	rcons_putchar,
	rcons_copycols,
	rcons_erasecols,
	rcons_copyrows,
	rcons_eraserows,
	rcons_allocattr
};

struct wsscreen_descr fb_stdscreen = {
	"std",
	0, 0,	/* ncols, nrows */
	&fbcons_emulops,
	0, 0,	/* fontwidth, fontheight */
	WSSCREEN_WSCOLORS | WSSCREEN_REVERSE
};

struct wsscreen_internal {
	const struct wsdisplay_emulops *emulops;
	void	*emulcookie;

	const struct wsscreen_descr *scrdata;

	const struct wsemul_ops *wsemul;
	void	*wsemulcookie;
};

/* Private variables used by the console driver. */
static unsigned font_lines;	/* font lines per character */
static unsigned scr_width;	/* # characters on a line */
static unsigned scr_lines;	/* # lines on the screen */
static unsigned scr_size;	/* # characters on the screen */

static int wsdisplay_console_initted;
static int wsdisplay_console_attached;
//static struct wsdisplay_softc *wsdisplay_console_device;
static struct wsscreen_internal wsdisplay_console_conf;

static struct consdev wsdisplay_cons = {
	NULL, NULL, NULL, wsdisplay_cnputc,
	NULL, NULL, NULL, NULL, NODEV, CN_NORMAL
};
static struct consdev *wsdisplay_ocn;
static struct consdev *cn_tab;

static int disabled_vc = -1;	/* Virtual console that was active when 
				 * disable_console was called.
				 */

static char *console_memory = NULL;
//static char *font_memory = NULL;

/* Per console data. */
typedef struct console {
  tty_t *c_tty;			/* associated TTY struct */
  int c_column;			/* current column number (0-origin) */
  int c_row;			/* current row (0 at top of screen) */
  int c_rwords;			/* number of WORDS (not bytes) in outqueue */
  int c_line;			/* line no */
} console_t;

struct fb_devconfig {
	int	dc_type;	/* WSCONS display type */

	vaddr_t	dc_vaddr;	/* memory space virtual base address */
	paddr_t	dc_paddr;	/* memory space physical base address */
	psize_t	dc_size;	/* size of slot memory */

	int	dc_offset;	/* offset from dc_vaddr to base of flat fb */

	int	dc_wid;		/* width of frame buffer */
	int	dc_ht;		/* height of frame buffer */
	int	dc_depth;	/* depth of frame buffer */
	int	dc_rowbytes;	/* bytes in fb scan line */

	struct raster dc_raster; /* raster description */
	struct rcons dc_rcons;	/* raster blitter control info */

	int isconsole;		/* console device */
};

struct fb_devconfig fb_console_dc;

static int nr_cons= 1;		/* actual number of consoles */
static console_t cons_table[NR_CONS];
static console_t *curcons = NULL;	/* currently visible */

static int shutting_down = FALSE;	/* don't allow console switches */

static int cons_write(struct tty *tp, int try);
static void cons_echo(tty_t *tp, int c);
static void out_char(console_t *cons, int c);
static void flush(console_t *cons);
static void disable_console(void);
static void reenable_console(void);
static int cons_ioctl(tty_t *tp, int);

static void fb_cons_stop(void);
static void fb_scr_init(struct tty *tp);
static int fb_con_loadfont(endpoint_t endpt, cp_grant_id_t grant);
static void fb_select_console(int cons_line);

cons_sw_t fb_cons_sw = {
	fb_scr_init,
	fb_con_loadfont,
	fb_cons_stop,
	fb_select_console,
	disable_console,
	reenable_console
};

/*===========================================================================*
 *				cons_write				     *
 *				tp tells which terminal is to be used *
 *===========================================================================*/
static int cons_write(register struct tty *tp, int try)
{
/* Copy as much data as possible to the output queue, then start I/O.  On
 * memory-mapped terminals, such as the IBM console, the I/O will also be
 * finished, and the counts updated.  Keep repeating until all I/O done.
 */

  int count;
  int result = OK;
  register char *tbuf;
  char buf[64];
  console_t *cons = tp->tty_priv;

  if (try) return 1;	/* we can always write to console */

  /* Check quickly for nothing to do, so this can be called often without
   * unmodular tests elsewhere.
   */
  if ((count = tp->tty_outleft) == 0 || tp->tty_inhibited) return 0;

  /* Copy the user bytes to buf[] for decent addressing. Loop over the
   * copies, since the user buffer may be much larger than buf[].
   */
  do {
	if (count > sizeof(buf)) count = sizeof(buf);
	if (tp->tty_outcaller == KERNEL) {
		/* We're trying to print on kernel's behalf */
		memcpy(buf, (char *) tp->tty_outgrant + tp->tty_outcum, count);
	} else {
		if ((result = sys_safecopyfrom(tp->tty_outcaller,
				tp->tty_outgrant, tp->tty_outcum,
				(vir_bytes) buf, count)) != OK) {
			break;
		}
	}
	tbuf = buf;

	/* Update terminal data structure. */
	tp->tty_outcum += count;
	tp->tty_outleft -= count;

	/* Output each byte of the copy to the screen.  Avoid calling
	 * out_char() for the "easy" characters, put them into the buffer
	 * directly.
	 */
	do {
		out_char(cons, *tbuf++);
	} while (--count != 0);
  } while ((count = tp->tty_outleft) != 0 && !tp->tty_inhibited);

  flush(cons);			/* transfer anything buffered to the screen */

  /* Reply to the writer if all output is finished or if an error occurred. */
  if (tp->tty_outleft == 0 || result != OK) {
	if (tp->tty_outcaller != KERNEL)
		chardriver_reply_task(tp->tty_outcaller, tp->tty_outid,
			result != OK ? result : tp->tty_outcum);
	tp->tty_outcum = tp->tty_outleft = 0;
	tp->tty_outcaller = NONE;
  }

  return 0;
}

/*===========================================================================*
 *				cons_echo				     *
 *				tp pointer to tty struct
 *				c character to be echoed
 *===========================================================================*/
static void cons_echo(register tty_t *tp, int c)
{
/* Echo keyboard input (print & flush). */
  console_t *cons = tp->tty_priv;

  out_char(cons, c);
  flush(cons);
}

/*===========================================================================*
 *				out_char				     *
 *				cons - pointer to console struct
 *				c - character to output
 *===========================================================================*/
static void out_char(register console_t *cons, int i)
{
	struct wsscreen_internal *dc;
	u_char c = i;

#if DUP_CONS_TO_SER
  if (cons == &cons_table[0] && c != '\0')
  {
	if (c == '\n')
		ser_putc('\r');
	ser_putc(c);
  }
#endif
	// TODO
	dc = &wsdisplay_console_conf;
	(*dc->wsemul->output)(dc->wsemulcookie, &c, 1, 1);

}

/*===========================================================================*
 *				flush					     *
 *				cons - pointer to console struct
 *===========================================================================*/
static void flush(register console_t *cons)
{
  tty_t *tp = cons->c_tty;
  
    /* Check and update the cursor position. */
  if (cons->c_column < 0) cons->c_column = 0;
  if (cons->c_column > scr_width) cons->c_column = scr_width;
  if (cons->c_row < 0) cons->c_row = 0;
  if (cons->c_row >= scr_lines) cons->c_row = scr_lines - 1;
  //cur = cons->c_org + cons->c_row * scr_width + cons->c_column;
  //if (cur != cons->c_cur)
  //  UPDATE_CURSOR(cons, cur);
}

void
wsdisplay_cnputc(dev_t dev, int i)
{
	struct wsscreen_internal *dc;
	u_char c = i;

	if (!wsdisplay_console_initted)
		return;

	dc = &wsdisplay_console_conf;
	(*dc->wsemul->output)(dc->wsemulcookie, &c, 1, 1);
}

/*
 * Callbacks for the emulation code.
 */
void
wsdisplay_emulbell(void *v)
{
	struct wsscreen *scr = v;

	if (scr == NULL)		/* console, before real attach */
		return;

	//if (scr->scr_flags & SCR_GRAPHICS) /* can this happen? */
	//	return;

	//(void) wsdisplay_internal_ioctl(scr->sc, scr, WSKBDIO_BELL, NULL,
	//				FWRITE, NULL);
}

void
wsdisplay_cnattach(const struct wsscreen_descr *type, void *cookie,
	int ccol, int crow, long defattr)
{
	const struct wsemul_ops *wsemul;

	KASSERT(wsdisplay_console_initted < 2);
	KASSERT(type->nrows > 0);
	KASSERT(type->ncols > 0);
	KASSERT(crow < type->nrows);
	KASSERT(ccol < type->ncols);

	wsdisplay_console_conf.emulops = type->textops;
	wsdisplay_console_conf.emulcookie = cookie;
	wsdisplay_console_conf.scrdata = type;

	//wsemul = &wsemul_vt100_ops;
	wsemul = &wsemul_dumb_ops;
	wsdisplay_console_conf.wsemul = wsemul;
	wsdisplay_console_conf.wsemulcookie = (*wsemul->cnattach)(type, cookie,
								  ccol, crow,
								  defattr);

	if (cn_tab != &wsdisplay_cons)
		wsdisplay_ocn = cn_tab;

	if (wsdisplay_ocn != NULL && wsdisplay_ocn->cn_halt != NULL)
		wsdisplay_ocn->cn_halt(wsdisplay_ocn->cn_dev);

	cn_tab = &wsdisplay_cons;
	wsdisplay_console_initted = 2;
}

static void
fb_clear(struct fb_devconfig *dc)
{
	int i, rows;

	/* clear the display */
	rows = dc->dc_ht;
	for (i = 0; rows-- > 0; i += dc->dc_rowbytes)
		memset((u_char *)dc->dc_vaddr + dc->dc_offset + i,
		    0, dc->dc_rowbytes);
}

static void
fb_init(struct fb_devconfig *dc)
{
	struct raster *rap;
	struct rcons *rcp;

	fb_clear(dc);

	rap = &dc->dc_raster;
	rap->width = dc->dc_wid;
	rap->height = dc->dc_ht;
	rap->depth = dc->dc_depth;
	rap->linelongs = dc->dc_rowbytes / sizeof(u_int32_t);
	rap->pixels = (u_int32_t *)(dc->dc_vaddr + dc->dc_offset);

	/* initialize the raster console blitter */
	rcp = &dc->dc_rcons;
	rcp->rc_sp = rap;
	rcp->rc_crow = rcp->rc_ccol = -1;
	rcp->rc_crowp = &rcp->rc_crow;
	rcp->rc_ccolp = &rcp->rc_ccol;
	rcons_init(rcp, 24, 80);

	fb_stdscreen.nrows = dc->dc_rcons.rc_maxrow;
	fb_stdscreen.ncols = dc->dc_rcons.rc_maxcol;
}

/*===========================================================================*
 *				scr_init				     *
 *===========================================================================*/
static void fb_scr_init(tty_t *tp)
{
/* Initialize the screen driver. */
  console_t *cons;
  int line;
  int s;
  static unsigned page_size;
  struct kinfo kinfo;		/* kernel information */
  struct kinfo_framebuffer *kfb;
	struct fb_devconfig *dc = &fb_console_dc;
	long defattr;
  	
  printf("fb_scr_init: start\n");
  /* Associate console and TTY. */
  line = tp - &tty_table[0];
  if (line >= nr_cons) return;
  cons = &cons_table[line];
  cons->c_tty = tp;
  cons->c_line = line;
  tp->tty_priv = cons;
  
  /* Get the fb parameters that describe the VDU. */
  if (! wsdisplay_console_initted) {

    if (OK != (s=sys_getkinfo(&kinfo))) {
        panic("Couldn't get kernel information: %d", s);
    }

	if (kinfo.mb_version == 2) {
	  kfb = &kinfo.fb;
		if (kfb->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
			panic("framebuffer type is not RGB");

	dc->dc_paddr = kfb->framebuffer_addr;

	dc->dc_wid = kfb->framebuffer_width;
	dc->dc_ht = kfb->framebuffer_height;
	dc->dc_depth = kfb->framebuffer_bpp;
	dc->dc_rowbytes = kfb->framebuffer_pitch * (kfb->framebuffer_bpp/8);

	dc->dc_size = dc->dc_ht * dc->dc_rowbytes;
	dc->dc_offset = 0;

	console_memory = vm_map_phys(SELF, (void *) dc->dc_paddr, dc->dc_size);

	if(console_memory == MAP_FAILED) 
  		panic("Console couldn't map video memory");

	dc->dc_vaddr = (vaddr_t)console_memory;

	printf("dc: width=%d, height=%d, depth=%d\n", dc->dc_wid, dc->dc_ht, dc->dc_depth);
	printf("    rowbytes=%d, size=%llu\n", dc->dc_rowbytes, dc->dc_size);
	printf("    paddr=%llx, vaddr=%lx\n", dc->dc_paddr, dc->dc_vaddr);
	
	/* set up the display */
	fb_init(&fb_console_dc);
  printf("fb_scr_init: fb_init passed\n");

	rcons_allocattr(&dc->dc_rcons, 0, 0, 0, &defattr);

	wsdisplay_cnattach(&fb_stdscreen, &dc->dc_rcons,
			0, 0, defattr);
  printf("fb_scr_init: wsdisplay_cnattach passed\n");

	dc->isconsole = 1;
	wsdisplay_console_initted = 1;
	}
	else {
	  	panic("UEFI version is not 2");
	}
			
  }

  /* Fill in TTY function hooks. */
  tp->tty_devwrite = cons_write;
  tp->tty_echo = cons_echo;
  tp->tty_ioctl = cons_ioctl;

  select_console(0);
  cons_ioctl(tp, 0);
}



/*===========================================================================*
 *				cons_stop				     *
 *===========================================================================*/
static void fb_cons_stop(void)
{
/* Prepare for halt or reboot. */
  select_console(0);
  shutting_down = TRUE;
}

/*===========================================================================*
 *				disable_console				     *
 *===========================================================================*/
static void disable_console(void)
{
	if (disabled_vc != -1)
		return;
	
	disabled_vc = ccurrent;

	select_console(0);

	/* Should also disable further output to virtual consoles */
}

/*===========================================================================*
 *				reenable_console			     *
 *===========================================================================*/
static void reenable_console(void)
{
	if (disabled_vc == -1)
		return;

	select_console(disabled_vc);
	disabled_vc = -1;
}

/*===========================================================================*
 *				select_console				     *
 *===========================================================================*/
static void fb_select_console(int cons_line)
{
/* Set the current console to console number 'cons_line'. */

  if (shutting_down) return;

  if (cons_line < 0 || cons_line >= nr_cons) return;

  ccurrent = cons_line;
  curcons = &cons_table[cons_line];

}

/*===========================================================================*
 *				con_loadfont				     *
 *===========================================================================*/
static int fb_con_loadfont(endpoint_t endpt, cp_grant_id_t grant)
{
  return OK;
}

/*===========================================================================*
 *				cons_ioctl				     *
 *===========================================================================*/
static int cons_ioctl(tty_t *tp, int UNUSED(try))
{
/* Set the screen dimensions. */

  tp->tty_winsize.ws_row= scr_lines;
  tp->tty_winsize.ws_col= scr_width;
  tp->tty_winsize.ws_xpixel= scr_width * 8;
  tp->tty_winsize.ws_ypixel= scr_lines * font_lines;

  return 0;
}

