#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "util.h"

/* maximum record length (in uint32_t) */
#define MAX_RECORD (1<<17)

struct nodearray {
	struct node **node;
	size_t len;
};

struct entry {
	struct node *node;
	struct nodearray deps;
	uint32_t mtime;
};

static int depsfd = -1;
static const char depsname[] = ".ninja_deps";
static const char depstmpname[] = ".ninja_deps.tmp";
static const char depsheader[] = "# ninjadeps\n";
static const uint32_t depsver = 3;
static struct entry *entries;
static size_t entrieslen, entriescap;

static void
writeall(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len) {
		n = write(fd, p, len);
		if (n <= 0)
			err(1, "write");
		p += n;
		len -= n;
	}
}

static bool
recordid(struct node *n)
{
	uint32_t buf[MAX_RECORD], sz;

	if (n->id != -1)
		return false;
	n->id = entrieslen++;
	if (n->id == -1)
		errx(1, "too many nodes");
	sz = (n->path->n + 7) & ~3;
	if (sz + 4 >= sizeof(buf))
		errx(1, "ID record too large");
	buf[0] = sz;
	memcpy(&buf[1], n->path->s, n->path->n);
	memset((char *)&buf[1] + n->path->n, 0, sz - n->path->n - 4);
	buf[sz / 4] = ~n->id;
	writeall(depsfd, buf, 4 + sz);

	return true;
}

static void
recorddeps(struct node *out, struct nodearray *deps, uint32_t mtime)
{
	uint32_t buf[MAX_RECORD], sz;
	size_t i;

	sz = 8 + deps->len * 4;
	if (sz + 4 >= sizeof(buf))
		errx(1, "deps record too large");
	buf[0] = sz | 0x80000000;
	buf[1] = out->id;
	buf[2] = mtime;
	for (i = 0; i < deps->len; ++i)
		buf[3 + i] = deps->node[i]->id;
	writeall(depsfd, buf, 4 + sz);
}

void
depsinit(int dirfd)
{
	uint32_t buf[MAX_RECORD];
	FILE *f;
	uint32_t ver, sz, id;
	size_t len, i, j, nrecord;
	bool isdep;
	struct string *path;
	struct node *n;
	struct edge *e;
	struct entry *entry, *oldentries;

	/* XXX: when ninja hits a bad record, it truncates the log to the last
	 * good record. perhaps we should do the same. */

	if (depsfd != -1)
		close(depsfd);
	entrieslen = 0;
	depsfd = openat(dirfd, depsname, O_RDONLY);
	if (depsfd < 0)
		goto rewrite;
	f = fdopen(depsfd, "r");
	if (!f)
		goto rewrite;
	if (!fgets((char *)buf, sizeof(buf), f))
		goto rewrite;
	if (fread(&ver, sizeof(ver), 1, f) != 1) {
		warn("deps read failed");
		goto rewrite;
	}
	if (strcmp((char *)buf, "# ninjadeps\n") != 0) {
		warnx("invalid deps header");
		goto rewrite;
	}
	if (ver != depsver) {
		warnx("unknown deps version");
		goto rewrite;
	}
	for (nrecord = 0;; ++nrecord) {
		if (fread(&sz, sizeof(sz), 1, f) != 1)
			break;
		isdep = sz & 0x80000000;
		sz &= 0x7fffffff;
		if (sz > sizeof(buf)) {
			warnx("deps record too large");
			goto rewrite;
		}
		if (fread(buf, sz, 1, f) != 1) {
			warn("deps read failed");
			goto rewrite;
		}
		if (sz % 4) {
			warnx("invalid size, must be multiple of 4: %" PRIu32, sz);
			goto rewrite;
		}
		if (isdep) {
			if (sz < 8) {
				warnx("invalid size, must be at least 8: %" PRIu32, sz);
				goto rewrite;
			}
			sz -= 8;
			id = buf[0];
			if (id >= entrieslen) {
				warnx("invalid node ID: %" PRIu32, id);
				goto rewrite;
			}
			entry = &entries[id];
			entry->mtime = buf[1];
			e = entry->node->gen;
			if (!e || !edgevar(e, "deps"))
				continue;
			sz /= 4;
			free(entry->deps.node);
			entry->deps.len = sz;
			entry->deps.node = xmalloc(sz * sizeof(n));
			for (i = 0; i < sz; ++i) {
				id = buf[2 + i];
				if (id >= entrieslen) {
					warnx("invalid node ID: %" PRIu32, id);
					goto rewrite;
				}
				entry->deps.node[i] = entries[id].node;
			}
		} else {
			if (sz <= 4) {
				warnx("invalid size, must larger than 4: %" PRIu32, sz);
				goto rewrite;
			}
			if (entrieslen != ~buf[sz / 4 - 1]) {
				warnx("corrupt deps log, bad checksum");
				goto rewrite;
			}
			len = sz - 4;
			while (((char *)buf)[len - 1] == '\0')
				--len;
			path = mkstr(len);
			memcpy(path->s, buf, len);
			path->s[len] = '\0';

			n = mknode(path);
			if (entrieslen >= entriescap) {
				entriescap = entriescap ? entriescap * 2 : 1024;
				entries = xrealloc(entries, entriescap * sizeof(entries[0]));
			}
			n->id = entrieslen;
			if (n->id == -1) {
				warnx("too many nodes in deps log");
				goto rewrite;
			}
			entries[entrieslen++] = (struct entry){.node = n};
		}
	}
	if (ferror(f)) {
		warn("deps read failed");
		goto rewrite;
	}
	fclose(f);
	if (nrecord <= 1000 || nrecord < 3 * entrieslen) {
		depsfd = openat(dirfd, depsname, O_WRONLY | O_APPEND);
		if (depsfd < 0)
			err(1, "open %s", depsname);
		return;
	}

rewrite:
	depsfd = openat(dirfd, depstmpname, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if (depsfd < 0)
		err(1, "open %s", depstmpname);
	memcpy(buf, depsheader, 12);
	buf[3] = depsver;
	writeall(depsfd, buf, 16);

	/* reset ID for all current entries */
	for (i = 0; i < entrieslen; ++i)
		entries[i].node->id = -1;
	/* save a temporary copy of the old entries */
	oldentries = xmalloc(entrieslen * sizeof(entries[0]));
	memcpy(oldentries, entries, entrieslen * sizeof(entries[0]));

	len = entrieslen;
	entrieslen = 0;
	for (i = 0; i < len; ++i) {
		entry = &oldentries[i];
		if (!entry->deps.len)
			continue;
		recordid(entry->node);
		entries[entry->node->id] = *entry;
		for (j = 0; j < entry->deps.len; ++j)
			recordid(entry->deps.node[j]);
		recorddeps(entry->node, &entry->deps, entry->mtime);
	}
	free(oldentries);
	if (renameat(dirfd, depstmpname, dirfd, depsname) < 0)
		err(1, "deps file rename failed");
}

static struct nodearray *
depsparse(const char *name, struct string *out)
{
	static struct buffer buf;
	static struct nodearray deps;
	static size_t depscap;
	struct string *path;
	FILE *f;
	int c;
	bool sawcolon;

	f = fopen(name, "r");
	if (!f)
		return NULL;
	sawcolon = false;
	buf.len = 0;
	deps.len = 0;
	for (;;) {
		c = fgetc(f);
		if (isalnum(c) || strchr("+,-./@_", c)) {
			bufadd(&buf, c);
			continue;
		}
		if (sawcolon) {
			if (!isspace(c) && c != EOF) {
				warnx("bad depfile: '%c' is not a valid target character", c);
				goto err;
			}
			if (buf.len > 0) {
				if (deps.len == depscap) {
					depscap = deps.node ? depscap * 2 : 32;
					deps.node = xrealloc(deps.node, depscap * sizeof(deps.node[0]));
				}
				path = mkstr(buf.len);
				memcpy(path->s, buf.data, buf.len);
				path->s[buf.len] = '\0';
				deps.node[deps.len++] = mknode(path);
			}
			if (c == '\n')
				sawcolon = false;
			else if (c == EOF)
				break;
		} else {
			if (c == EOF)
				break;
			if (c != ':') {
				warnx("bad depfile: expected ':', saw '%c'", c);
				goto err;
			}
			if (out->n != buf.len || memcmp(buf.data, out->s, buf.len) != 0) {
				warnx("bad depfile: output doesn't match $out: %s != %s", buf.data, out->s);
				goto err;
			}
			sawcolon = true;
		}
		buf.len = 0;
		for (;;) {
			c = fgetc(f);
			if (c == '\\') {
				if (fgetc(f) != '\n') {
					warnx("bad depfile: '\\' only allowed before newline");
					goto err;
				}
			} else if (!isblank(c)) {
			    break;
			}
		}
		ungetc(c, f);
	}
	if (ferror(f)) {
		warnx("depfile read failed");
		goto err;
	}
	fclose(f);
	return &deps;

err:
	fclose(f);
	return NULL;
}

void
depsload(struct edge *e)
{
	struct string *deptype, *depfile;
	struct nodearray *deps;
	struct node *n;

	deptype = edgevar(e, "deps");
	if (deptype) {
		n = e->out[0];
		if (n->id != -1 && (n->mtime.tv_nsec < 0 || n->mtime.tv_sec <= entries[n->id].mtime)) {
			deps = &entries[n->id].deps;
			edgeadddeps(e, deps->node, deps->len);
		} else {
			e->flags |= FLAG_DIRTY_OUT;
		}
	} else {
		depfile = edgevar(e, "depfile");
		if (!depfile)
			return;
		deps = depsparse(depfile->s, e->out[0]->path);
		if (deps)
			edgeadddeps(e, deps->node, deps->len);
		else
			e->flags |= FLAG_DIRTY_OUT;
	}
}

void
depsrecord(struct edge *e)
{
	struct string *deptype, *depfile;
	struct nodearray *deps;
	struct node *out, *n;
	struct entry *entry;
	size_t i;
	bool update;

	deptype = edgevar(e, "deps");
	if (!deptype || deptype->n == 0)
		return;
	if (strcmp(deptype->s, "gcc") != 0) {
		warnx("unsuported deps type: %s", deptype->s);
		return;
	}
	depfile = edgevar(e, "depfile");
	if (!depfile || depfile->n == 0) {
		warnx("deps but no depfile");
		return;
	}
	out = e->out[0];
	nodestat(out);
	deps = depsparse(depfile->s, out->path);
	unlink(depfile->s);
	if (!deps)
		return;
	update = false;
	entry = NULL;
	if (recordid(out)) {
		update = true;
	} else {
		entry = &entries[out->id];
		if (entry->mtime != out->mtime.tv_sec || entry->deps.len != deps->len)
			update = true;
		for (i = 0; i < deps->len && !update; ++i) {
			if (entry->deps.node[i] != deps->node[i])
				update = true;
		}
	}
	for (i = 0; i < deps->len; ++i) {
		n = deps->node[i];
		if (recordid(n))
			update = true;
	}
	if (update)
		recorddeps(out, deps, out->mtime.tv_sec);
}
