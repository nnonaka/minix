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

/* Set this to 1 if you want console output duplicated on the first
 * serial line.
  */
#define DUP_CONS_TO_SER	1

/* The clock task should provide an interface for this */
#define TIMER_FREQ  1193182L    /* clock frequency for timer in PC and AT */

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

/* Private variables used by the console driver. */
static int wrap;		/* hardware can wrap? */
static int softscroll;		/* 1 = software scrolling, 0 = hardware */
static int beeping;		/* speaker is beeping? */
static long disable_beep = -1;	/* do not use speaker if set to 1 */
static unsigned font_lines;	/* font lines per character */
static unsigned scr_width;	/* # characters on a line */
static unsigned scr_lines;	/* # lines on the screen */
static unsigned scr_size;	/* # characters on the screen */

static int disabled_vc = -1;	/* Virtual console that was active when 
				 * disable_console was called.
				 */
static int disabled_sm;	/* Scroll mode to be restored when re-enabling
				 * console
				 */

static char *console_memory = NULL;
static char *font_memory = NULL;

/* Per console data. */
typedef struct console {
	const struct wsdisplay_emulops *emulops;
	const struct wsemul_ops *wsemul;
	const struct wsscreen_descr *scrdata;
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

static int nr_cons= 1;		/* actual number of consoles */
static console_t cons_table[NR_CONS];
static console_t *curcons = NULL;	/* currently visible */

static int shutting_down = FALSE;	/* don't allow console switches */

static int cons_write(struct tty *tp, int try);
static void cons_echo(tty_t *tp, int c);
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
		ser_putc(*tbuf++);
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

  ser_putc(c);
  flush(cons);
}

/*===========================================================================*
 *				flush					     *
 *				cons - pointer to console struct
 *===========================================================================*/
static void flush(register console_t *cons)
{
  tty_t *tp = cons->c_tty;
}

/*===========================================================================*
 *				beep_disabled				     *
 *===========================================================================*/
static long beep_disabled(void)
{
/* Return whether the user requested that beeps not be performed.
 */

  /* Perform first-time initialization if necessary. */
  if (disable_beep < 0) {
	disable_beep = 0;	/* the default is on */

	(void) env_parse("nobeep", "d", 0, &disable_beep, 0, 1);
  }

  return disable_beep;
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

  /* Associate console and TTY. */
  line = tp - &tty_table[0];
  if (line >= nr_cons) return;
  cons = &cons_table[line];
  cons->c_tty = tp;
  cons->c_line = line;
  tp->tty_priv = cons;

  /* Fill in TTY function hooks. */
  tp->tty_devwrite = cons_write;
  tp->tty_echo = cons_echo;
  tp->tty_ioctl = cons_ioctl;

  select_console(0);
  cons_ioctl(tp, 0);
}

void
fb_clear(struct fb_devconfig *dc)
{
	// TODO
}

void
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

	//fb_stdscreen.nrows = dc->dc_rcons.rc_maxrow;
	//fb_stdscreen.ncols = dc->dc_rcons.rc_maxcol;
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
	disabled_sm = softscroll;

	//cons_org0();
	softscroll = 1;
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

