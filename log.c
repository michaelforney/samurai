#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
loginit(const char *builddir)
{
	int ver;
	char *logpath = (char *)logname, *logtmppath = (char *)logtmpname, *p, *s;
	size_t nline, nentry, i;
	struct edge *e;
	struct node *n;
	int64_t mtime;
	struct buffer buf = {0};

	nline = 0;
	nentry = 0;

	if (logfile)
		fclose(logfile);
	if (builddir)
		xasprintf(&logpath, "%s/%s", builddir, logname);
	logfile = fopen(logpath, "a+");
	if (!logfile)
		goto rewrite;
	if (fscanf(logfile, logfmt, &ver) < 1)
		goto rewrite;
	if (ver != logver)
		goto rewrite;

	for (;;) {
		if (buf.cap - buf.len < BUFSIZ) {
			buf.cap = buf.cap ? buf.cap * 2 : BUFSIZ;
			buf.data = xrealloc(buf.data, buf.cap);
		}
		buf.data[buf.cap - 2] = '\0';
		if (!fgets(buf.data + buf.len, buf.cap - buf.len, logfile))
			break;
		if (buf.data[buf.cap - 2] && buf.data[buf.cap - 2] != '\n') {
			buf.len = buf.cap - 1;
			continue;
		}
		++nline;
		p = buf.data;
		buf.len = 0;
		if (!nextfield(&p))  /* start time */
			continue;
		if (!nextfield(&p))  /* end time */
			continue;
		s = nextfield(&p);  /* mtime (used for restat) */
		if (!s)
			continue;
		mtime = strtoll(s, &s, 10);
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
		if (n->logmtime == MTIME_MISSING)
			++nentry;
		n->logmtime = mtime;
		s = nextfield(&p);  /* command hash */
		if (!s)
			continue;
		n->hash = strtoull(s, &s, 16);
		if (*s) {
			warnx("corrupt log: invalid hash for %s", n->path->s);
			continue;
		}
	}
	free(buf.data);
	if (ferror(logfile))
		warnx("log read failed");
	if (nline <= 100 || nline <= 3 * nentry) {
		if (builddir)
			free(logpath);
		return;
	}

rewrite:
	if (logfile)
		fclose(logfile);
	if (builddir)
		xasprintf(&logtmppath, "%s/%s", builddir, logtmpname);
	logfile = fopen(logtmppath, "w");
	if (!logfile)
		err(1, "open %s", logtmppath);
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
	remove(logpath);
	if (rename(logtmppath, logpath) < 0)
		err(1, "log file rename failed");
	if (builddir) {
		free(logpath);
		free(logtmppath);
	}
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
	fprintf(logfile, "0\t0\t%" PRId64 "\t%s\t%" PRIx64 "\n", n->logmtime, n->path->s, n->hash);
}
