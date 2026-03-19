/* console.h */

#include <minix/chardriver.h>
#include <minix/timers.h>

struct cons_sw;
typedef void(*init_t) (struct tty *tp);
typedef int(*loadfont_t) (endpoint_t endpt, cp_grant_id_t grant);
typedef void(*stop_t) (void);
typedef void(*select_console_t) (int cons_line);
typedef void(*disable_console_t) (void);
typedef void(*reenable_console_t) (void);

typedef struct cons_sw {
	init_t sw_init;
	loadfont_t sw_loadfont;
	stop_t sw_stop;
	select_console_t sw_select_console;
	disable_console_t sw_disable_console;
	reenable_console_t sw_reenable_console;
} cons_sw_t;

