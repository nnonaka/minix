/* netbsd */
#define	SYSCONFDIR		"/etc"
#define	SBINDIR			"/sbin"
#define	LIBDIR			"/lib"
#define	LIBEXECDIR		"/libexec"
#define	DBDIR			"/var/db/dhcpcd"
#define	RUNDIR			"/var/run"
#if !defined(__minix)
#define	HAVE_IFAM_PID
#define	HAVE_IFAM_ADDRFLAGS
#endif /* !defined(__minix) */
#define	HAVE_IFADDRS_ADDRFLAGS
#define	HAVE_OPEN_MEMSTREAM
#define	HAVE_UTIL_H
#define	HAVE_SETPROCTITLE
#define	HAVE_SYS_QUEUE_H
#define	HAVE_SYS_RBTREE_H
#define	HAVE_REALLOCARRAY
#if !defined(__minix)
#define	HAVE_KQUEUE
#define	HAVE_KQUEUE1
#endif /* !defined(__minix) */
#define	HAVE_SYS_BITOPS_H
#define	HAVE_MD5_H
#define	SHA2_H			<sha2.h>
