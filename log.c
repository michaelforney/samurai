#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "htab.h"
#include "graph.h"
#include "log.h"
#include "util.h"

static FILE *logfile;
static const char *logname = ".ninja_log";
static const char *logtmpname = ".ninja_log.tmp";
static const char *logfmt = "# ninja log v%d\n";
static const int logver = 5;

static char *
nextfield(char **end)
{
	char *s = *end;

	if (!*s) {
		warnx("corrupt log: missing field");
		return NULL;
	}
	*end += strcspn(*end, "\t\n");
	if (**end)
		*(*end)++ = '\0';

	return s;
}

void
loginit(int dirfd)
{
	int fd, ver;
	char *line = NULL, *p, *s;
	size_t sz = 0, nline, nentry, i;
	struct edge *e;
	struct node *n;
	time_t mtime;

	nline = 0;
	nentry = 0;

	if (logfile)
		fclose(logfile);
	fd = openat(dirfd, logname, O_RDWR | O_APPEND);
	if (fd < 0)
		goto rewrite;
	logfile = fdopen(fd, "a+");
	if (!logfile)
		goto rewrite;
	if (getline(&line, &sz, logfile) < 0)
		goto rewrite;
	if (sscanf(line, logfmt, &ver) < 1)
		goto rewrite;
	if (ver != logver)
		goto rewrite;

	while (getline(&line, &sz, logfile) >= 0) {
		++nline;
		p = line;
		if (!nextfield(&p))  /* start time */
			continue;
		if (!nextfield(&p))  /* end time */
			continue;
		s = nextfield(&p);  /* mtime (used for restat) */
		if (!s)
			continue;
		mtime = strtol(s, &s, 10);
		if (*s) {
			warnx("corrupt log: invalid mtime");
			continue;
		}
		s = nextfield(&p);  /* output path */
		if (!s)
			continue;
		n = nodeget(s);
		if (!n || !n->gen)
			continue;
		n->logmtime = mtime;
		s = nextfield(&p);  /* command hash */
		if (!s)
			continue;
		if (!n->hash)
			++nentry;
		n->hash = strtoull(s, &s, 16);
		if (*s) {
			warnx("corrupt log: invalid hash for %s", n->path->s);
			continue;
		}
	}
	if (ferror(logfile))
		warnx("log read failed");
	if (nline <= 100 || nline <= 3 * nentry)
		return;

rewrite:
	if (logfile)
		fclose(logfile);
	fd = openat(dirfd, logtmpname, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if (fd < 0)
		err(1, "open %s", logtmpname);
	logfile = fdopen(fd, "w");
	if (!logfile)
		err(1, "fdopen");
	fprintf(logfile, logfmt, logver);
	if (nentry > 0) {
		for (e = alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (!n->hash)
					continue;
				logrecord(n);
			}
		}
	}
	fflush(logfile);
	if (ferror(logfile))
		errx(1, "log file write failed");
	if (renameat(dirfd, logtmpname, dirfd, logname) < 0)
		err(1, "log file rename failed");
}

void
logclose(void)
{
	fflush(logfile);
	if (ferror(logfile))
		errx(1, "log file write failed");
	fclose(logfile);
}

void
logrecord(struct node *n)
{
	fprintf(logfile, "0\t0\t%ld\t%s\t%" PRIx64 "\n", (long)n->logmtime, n->path->s, n->hash);
}
