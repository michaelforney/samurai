#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>

#include "graph.h"
#include "token.h"
#include "util.h"

/*
 * The jobserver protocol is simple: samurai can always run one job,
 * and needs to read a character from a pipe in order to start more.
 * The characters on the pipe are called "tokens" and the extra job is
 * referred to as the "free" or "gifted" token; its purpose is to guarantee
 * progress of each jobserver client and hence of the overall build.
 * When a job finishes, the token must be written back to the pipe, unless
 * it was the only running job (in which case you can keep the free token
 * for yourself).
 *
 * One complication is that the pipe should stay in blocking mode.  This is
 * because traditionally Make had nothing to do while waiting for a token;
 * with some tricks to avoid races, SIGCHLD could be used to interrupt
 * the blocking read.  This does not work well in a more modern event
 * loop, therefore samurai uses a thread that handles communication on
 * this pipe.  A simple mutex/condvar pair is used to send requests to
 * the auxiliary thread; any requests submitted during a read will only
 * be processed when read() returns, but anyway the auxiliary thread would
 * not be able to satisfy them (a blocking read means no available tokens).
 *
 * Communication from the auxiliary thread to the main thread, instead,
 * uses another OS pipe.  This pipe holds a character for each job that
 * samurai can start, and it can be non-blocking for easy integration with
 * the main thread's poll loop.  Furthermore, this pipe always starts with
 * one character, thus making no distinction between the free token and
 * those that the auxiliary thread has taken from the cross-process pipe.
 * This way, the main loop does not deal with the complications introduced
 * by the free token.
 */

/* Used by jobserver thread.  */
static int parent_rfd = -1;
static int local_wfd = -1;

/* Used by main thread.  */
static int parent_wfd = -1;
static int local_rfd = -1;
static pthread_t jobserver_thread;

/* Read/written under lock.  */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request = PTHREAD_COND_INITIALIZER;
static size_t my_tokens = 1;
static size_t pending_edges;
static bool done;

static bool tokenread(int fd)
{
	char ch;
	return read(fd, &ch, 1) > 0;
}

static void tokenwrite(int fd)
{
	write(fd, "X", 1);
}

static void tokenwait()
{
	while (!tokenread(parent_rfd)) {
		/*
		 * just in case someone does not follow the spec and makes
		 * the pipe non-blocking
		 */
		struct pollfd pfd = { parent_rfd, POLLIN, 0 };
		poll(&pfd, 1, -1);
	}
}

static void *tokenthread(void *unused)
{
	(void)unused;

	pthread_mutex_lock(&mutex);
	for (;;) {
		while (!pending_edges && !done)
			pthread_cond_wait(&request, &mutex);

		if (done)
			break;

		pending_edges--;

		/*
		 * one or more jobs couldn't find a token in local_rfd.
		 * get it from the global pool and give it to the main
		 * thread via local_wfd.
		 */
		pthread_mutex_unlock(&mutex);
		tokenwait();
		pthread_mutex_lock(&mutex);

		my_tokens++;
		tokenwrite(local_wfd);
	}
	pthread_mutex_unlock(&mutex);
	return NULL;
}

/* return false if there are no available tokens; in that case one has
 * been requested, and local_rfd will become POLLIN once it's been
 * retrieved */
bool tokenget(struct edge *e)
{
	bool got_token = false;

	if (parent_wfd == -1)
		return true;

	got_token = tokenread(local_rfd);
	if (!got_token && !e->reserve) {
		/*
		 * one increment of pending_edges corresponds to
		 * one token written to local_wfd.  make sure to
		 * only request a token once per edge!
		 */
		e->reserve = true;
		pthread_mutex_lock(&mutex);
		pending_edges++;
		pthread_cond_signal(&request);
		pthread_mutex_unlock(&mutex);
	}

	return got_token;
}

/* put back a token when a job is done */
void tokenput(void)
{
	bool freetoken = false;
	if (parent_wfd == -1)
		return;

	pthread_mutex_lock(&mutex);
	if (my_tokens == 1)
		freetoken = true;
	else
		my_tokens--;
	pthread_mutex_unlock(&mutex);

	/*
	 * do not attempt to shortcut tokens directly within samurai:
	 * apart from the free token, any other token goes to the build
	 * job and *that* will result in a wakeup of the auxiliary thread.
	 * This is both simpler and fairer.
	 */
	tokenwrite(freetoken ? local_wfd : parent_wfd);
}

static void tokenexit(void)
{
	pthread_mutex_lock(&mutex);
	pending_edges = 0;
	done = true;
	pthread_cond_signal(&request);
	pthread_mutex_unlock(&mutex);
	pthread_join(jobserver_thread, NULL);

	/* samu could have some tokens if called from fatal().  */
	while (my_tokens > 1) {
		my_tokens--;
		tokenwrite(parent_wfd);
	}
}

static bool c_isdigit(char ch)
{
	return ch >= '0' && ch <= '9';
}

/* --jobserver-auth=R,W - file descriptors provided on the command line */
static int parse_makeflags_pipe(const char *makeflags)
{
	if (!c_isdigit(*makeflags))
		return false;
	parent_rfd = 0;
	do
		parent_rfd = parent_rfd * 10 + (*makeflags++ - '0');
	while (c_isdigit(*makeflags));
	if (*makeflags != ',')
		return -1;
	if (fcntl(parent_rfd, F_GETFL) == -1)
		return errno;

	makeflags++;
	if (!c_isdigit(*makeflags))
		return -1;
	parent_wfd = 0;
	do
		parent_wfd = parent_wfd * 10 + (*makeflags++ - '0');
	while (c_isdigit(*makeflags));
	if (*makeflags && !isspace(*makeflags))
		return -1;
	if (fcntl(parent_wfd, F_GETFL) == -1)
		return errno;

	return 0;
}

#define FLAG1 "--jobserver-auth="
#define FLAG2 "--jobserver-fds="

/*
 * return file descriptor signaling build() that it can start a
 * new edge
 */
int tokeninit(void)
{
	char *makeflags = getenv("MAKEFLAGS");
	const char *p;
	const char *makearg = NULL;
	int ret, fd[2];
	sigset_t blocked, old;

	if (!makeflags)
		return -1;

	makeflags = strdup(makeflags);
	p = strtok(makeflags, " \t");
	while (p) {
		/*
		 * the `MAKEFLAGS` variable may contain multiple instances of
		 * the option. Only the last instance is relevant.
		 */
		if (!memcmp(p, FLAG1, strlen(FLAG1)))
			makearg = p + strlen(FLAG1);
		else if (!memcmp(p, FLAG2, strlen(FLAG2)))
			makearg = p + strlen(FLAG2);

		p = strtok(NULL, " \t");
	}

	if (!makearg)
		goto out;

	ret = parse_makeflags_pipe(makearg);

	if (ret) {
		if (ret > 0)
			warn("could not open jobserver pipe: %s\n", strerror(ret));
		goto out;
	}

	if (pipe(fd) == -1) {
		ret = errno;
		warn("could not create jobserver pipe: %s\n", strerror(ret));
		goto out;
	}

	local_rfd = fd[0];
	local_wfd = fd[1];

	/*
	 * temporarily block all signals so that they are processed in
	 * the main thread
	 */
	sigfillset(&blocked);
	pthread_sigmask(SIG_SETMASK, &blocked, &old);
	ret = pthread_create(&jobserver_thread, NULL, tokenthread, NULL);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (ret) {
		warn("could not create jobserver thread: %s\n", strerror(ret));
		goto out_close;
	}

	atexit(tokenexit);

	/* the parent gives us a free token */
	tokenwrite(local_wfd);
	fcntl(local_rfd, F_SETFL, O_NONBLOCK);
	return local_rfd;

out_close:
	close(local_rfd);
	close(local_wfd);
	local_rfd = local_wfd = -1;
out:
	parent_rfd = parent_wfd = -1;
	free(makeflags);
	return -1;
}
