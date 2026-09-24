/* Tests for how select(2) and poll(2) answer for descriptors that are not
 * open.
 *
 * The two differ on purpose.  select(2) fails the whole call with EBADF,
 * whatever else was in its sets; poll(2) answers that one entry with
 * POLLNVAL and goes on with the rest, which is what lets a program poll a
 * set that it does not fully control.  An fd_set is FD_SETSIZE wide while a
 * process can only have OPEN_MAX descriptors, so the bits in between are
 * bad descriptors too, not a bad argument.
 *
 * The last subtest is about the other end of the same question: when a pipe
 * is writable.  A full one is not, however much select(2) used to say
 * otherwise, and something has to wake a waiter once it stops being full.
 *
 * An alarm guards every wait: a regression here is as likely to hang as to
 * answer wrongly.
 */
#include <sys/select.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>

#include "common.h"

static int pipefd[2] = { -1, -1 };

static void
alarm_handler(int sig)
{

	/* Nothing: the point is to interrupt a wait that should not have
	 * happened.  A blocked call returns EINTR and the check fails. */
	(void)sig;
}

static void
guard(unsigned int secs)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(0);
	alarm(secs);
}

/*
 * Find a descriptor that is not open.
 */
static int
closed_fd(void)
{
	int fd;

	for (fd = 3; fd < OPEN_MAX; fd++)
		if (fcntl(fd, F_GETFD) == -1 && errno == EBADF)
			return fd;

	e(0);
	return -1;
}

/*
 * Is the given descriptor writable right now?  Returns 1, 0, or -1.
 */
static int
writable(int fd, int secs)
{
	struct timeval tv;
	fd_set wr;

	FD_ZERO(&wr);
	FD_SET(fd, &wr);
	tv.tv_sec = secs;
	tv.tv_usec = 0;

	guard(10);
	switch (select(fd + 1, NULL, &wr, NULL, &tv)) {
	case -1:
		alarm(0);
		return -1;
	case 0:
		alarm(0);
		return 0;
	default:
		alarm(0);
		return FD_ISSET(fd, &wr) ? 1 : -1;
	}
}

/*
 * select(2) rejects a descriptor that is not open, wherever in the sets it
 * sits.
 */
static void
test99a(void)
{
	fd_set rd;
	int bad;

	subtest = 1;

	bad = closed_fd();

	FD_ZERO(&rd);
	FD_SET(bad, &rd);
	guard(5);
	errno = 0;
	if (select(bad + 1, &rd, NULL, NULL, NULL) != -1) e(0);
	if (errno != EBADF) e(0);
	alarm(0);

	/* A descriptor beyond OPEN_MAX cannot be open either: that is a bad
	 * descriptor, not a bad nfds. */
	FD_ZERO(&rd);
	FD_SET(OPEN_MAX, &rd);
	guard(5);
	errno = 0;
	if (select(OPEN_MAX + 1, &rd, NULL, NULL, NULL) != -1) e(0);
	if (errno != EBADF) e(0);
	alarm(0);

	FD_ZERO(&rd);
	FD_SET(FD_SETSIZE - 1, &rd);
	guard(5);
	errno = 0;
	if (select(FD_SETSIZE, &rd, NULL, NULL, NULL) != -1) e(0);
	if (errno != EBADF) e(0);
	alarm(0);

	/* The write and exception sets are no different. */
	FD_ZERO(&rd);
	FD_SET(bad, &rd);
	errno = 0;
	if (select(bad + 1, NULL, &rd, NULL, NULL) != -1) e(0);
	if (errno != EBADF) e(0);

	errno = 0;
	if (select(bad + 1, NULL, NULL, &rd, NULL) != -1) e(0);
	if (errno != EBADF) e(0);
}

/*
 * A set that is FD_SETSIZE wide but has nothing set above OPEN_MAX is a
 * perfectly ordinary call: the excess is ignored.  Only a range that cannot
 * be expressed at all is EINVAL.
 */
static void
test99b(void)
{
	struct timeval tv;
	fd_set rd;

	subtest = 2;

	if (write(pipefd[1], "x", 1) != 1) e(0);

	FD_ZERO(&rd);
	FD_SET(pipefd[0], &rd);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	guard(10);
	if (select(FD_SETSIZE, &rd, NULL, NULL, &tv) != 1) e(0);
	if (!FD_ISSET(pipefd[0], &rd)) e(0);
	alarm(0);

	FD_ZERO(&rd);
	errno = 0;
	if (select(FD_SETSIZE + 1, &rd, NULL, NULL, &tv) != -1) e(0);
	if (errno != EINVAL) e(0);

	errno = 0;
	if (select(-1, &rd, NULL, NULL, &tv) != -1) e(0);
	if (errno != EINVAL) e(0);

	/* Drain the pipe again. */
	{
		char c;

		if (read(pipefd[0], &c, 1) != 1) e(0);
	}
}

/*
 * poll(2) answers a bad descriptor with POLLNVAL, and keeps its promises
 * about the others in the same call.
 */
static void
test99c(void)
{
	struct pollfd pfd[4];
	int bad;

	subtest = 3;

	bad = closed_fd();

	/* One bad descriptor on its own: answered at once, even though the
	 * call was told to wait forever. */
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = bad;
	pfd[0].events = POLLIN;
	guard(5);
	if (poll(pfd, 1, -1) != 1) e(0);
	if (pfd[0].revents != POLLNVAL) e(0);
	alarm(0);

	/* A bad descriptor next to a good one: both are answered.  This is
	 * the shape that matters -- a program polling a set it did not build
	 * itself must not lose the whole call to one closed descriptor. */
	if (write(pipefd[1], "x", 1) != 1) e(0);

	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = bad;
	pfd[0].events = POLLIN;
	pfd[1].fd = pipefd[0];
	pfd[1].events = POLLIN;
	pfd[2].fd = -1;			/* ignored entirely */
	pfd[2].events = POLLIN;
	pfd[3].fd = FD_SETSIZE;		/* too wide to ever be open */
	pfd[3].events = POLLIN;

	guard(5);
	if (poll(pfd, 4, -1) != 3) e(0);
	alarm(0);
	if (pfd[0].revents != POLLNVAL) e(0);
	if (!(pfd[1].revents & POLLIN)) e(0);
	if (pfd[2].revents != 0) e(0);
	if (pfd[3].revents != POLLNVAL) e(0);

	{
		char c;

		if (read(pipefd[0], &c, 1) != 1) e(0);
	}

	/* With the pipe empty, the good descriptor is not ready.  The bad
	 * one is an event all the same, so the call must come back with it
	 * at once rather than wait for the other -- here, forever. */
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = pipefd[0];
	pfd[0].events = POLLIN;
	pfd[1].fd = bad;
	pfd[1].events = POLLIN;

	guard(5);
	if (poll(pfd, 2, -1) != 1) e(0);
	alarm(0);
	if (pfd[0].revents != 0) e(0);
	if (pfd[1].revents != POLLNVAL) e(0);

	/* The same with a timeout given, which must not be waited out. */
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = pipefd[0];
	pfd[0].events = POLLIN;
	pfd[1].fd = bad;
	pfd[1].events = POLLIN;

	guard(5);
	if (poll(pfd, 2, 30000) != 1) e(0);
	alarm(0);
	if (pfd[1].revents != POLLNVAL) e(0);

	/* Nothing but negative descriptors: a plain sleep, which must still
	 * honour its timeout rather than return at once. */
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = -1;
	pfd[0].events = POLLIN;
	guard(5);
	if (poll(pfd, 1, 100) != 0) e(0);
	alarm(0);
	if (pfd[0].revents != 0) e(0);
}

#define WAIT_USECS	200000		/* time for a child to get ready */

/*
 * How full a pipe has to be before it stops being writable, and that a
 * select(2) waiting for that is actually woken.
 */
static void
test99e(void)
{
	static char buf[PIPE_BUF];
	struct timeval tv;
	fd_set wr;
	int fd[2], status;
	pid_t pid;

	subtest = 5;

	if (pipe(fd) != 0) e(0);

	/* An empty pipe is writable. */
	if (writable(fd[1], 0) != 1) e(0);

	/* A pipe with something in it still is: a MINIX pipe holds exactly
	 * PIPE_BUF bytes, so the promise select(2) can make about it is
	 * that a byte will fit, not that an atomic write will. */
	if (write(fd[1], buf, 1) != 1) e(0);
	if (writable(fd[1], 0) != 1) e(0);

	/* A full one is not writable at all. */
	if (write(fd[1], buf, PIPE_BUF - 1) != PIPE_BUF - 1) e(0);
	if (writable(fd[1], 0) != 0) e(0);

	/* Taking a single byte out is enough to make it writable again. */
	if (read(fd[0], buf, 1) != 1) e(0);
	if (writable(fd[1], 0) != 1) e(0);

	/* Drain it. */
	if (read(fd[0], buf, PIPE_BUF - 1) != PIPE_BUF - 1) e(0);
	if (writable(fd[1], 0) != 1) e(0);

	/* A blocking wait is woken by the reader that makes room -- not one
	 * read later, and not never.  The child takes a single byte, which
	 * is all it takes to make the pipe writable again. */
	if (write(fd[1], buf, PIPE_BUF) != PIPE_BUF) e(0);

	switch (pid = fork()) {
	case -1:
		e(0);
		break;
	case 0:
		usleep(WAIT_USECS);
		if (read(fd[0], buf, 1) != 1) exit(1);
		exit(0);
	default:
		break;
	}

	FD_ZERO(&wr);
	FD_SET(fd[1], &wr);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	guard(10);
	if (select(fd[1] + 1, NULL, &wr, NULL, &tv) != 1) e(0);
	alarm(0);
	if (!FD_ISSET(fd[1], &wr)) e(0);

	if (waitpid(pid, &status, 0) != pid) e(0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(0);

	/* The child took one byte; take the rest, or the refill below has
	 * nowhere to go. */
	if (read(fd[0], buf, PIPE_BUF - 1) != PIPE_BUF - 1) e(0);

	/* And by the last reader going away: a write would fail with EPIPE
	 * rather than block, which counts as ready. */
	if (write(fd[1], buf, PIPE_BUF) != PIPE_BUF) e(0);

	switch (pid = fork()) {
	case -1:
		e(0);
		break;
	case 0:
		usleep(WAIT_USECS);
		exit(0);		/* closes the last read end */
	default:
		break;
	}

	if (close(fd[0]) != 0) e(0);	/* the child holds the only one now */

	FD_ZERO(&wr);
	FD_SET(fd[1], &wr);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	guard(10);
	if (select(fd[1] + 1, NULL, &wr, NULL, &tv) != 1) e(0);
	alarm(0);

	if (waitpid(pid, &status, 0) != pid) e(0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(0);

	if (close(fd[1]) != 0) e(0);
}

int
main(int argc, char **argv)
{
	int i, m;

	start(99);

	if (pipe(pipefd) != 0) e(0);

	m = (argc == 2) ? atoi(argv[1]) : 0x1F;

	for (i = 0; i < 2; i++) {
		if (m & 0x01) test99a();
		if (m & 0x02) test99b();
		if (m & 0x04) test99c();
		if (m & 0x10) test99e();
	}

	if (close(pipefd[0]) != 0) e(0);
	if (close(pipefd[1]) != 0) e(0);

	quit();

	return 0;
}
