#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "util.h"

/*
.ninja_deps file format

The header identifying the format is the string "# ninjadeps\n", followed by a
4-byte integer specifying the format version. After this is a series of binary
records. All integers in .ninja_deps are written in system byte-order.

A record starts with a 4-byte integer indicating the record type and size. If
the high bit is set, then it is a dependency record. Otherwise, it is a node
record. In either case, the remaining 31 bits specify the size in bytes of the
rest of the record. The size must be a multiple of 4, and no larger than than
2^19.

Node records are given in incrementing ID order, and must be given before any
dependency record that refers to it. The last 4-byte integer in the record is
used as a checksum to prevent corruption. Counting from 0, the n-th node record
(specifying the node with ID n) will have a checksum of ~n (bitwise negation of
n). The remaining bytes of the record specify the path of the node, padded with
NUL bytes to the next 4-byte boundary (start of the checksum value).

A dependency record contains a list of dependencies for the edge that built a
particular node. The first 4-byte integer is the node ID. The second 4-byte
integer is the UNIX mtime of the node when it was built. Following this is a
sequence of 4-byte integers specifying the IDs of the dependency nodes for this
edge, which will have been specified previously in node records.
*/

/* maximum record size (in bytes) */
#define MAX_RECORD_SIZE (1<<19)

struct nodearray {
	struct node **node;
	size_t len;
};

struct entry {
	struct node *node;
	struct nodearray deps;
	uint32_t mtime;
};

static const char depsname[] = ".ninja_deps";
static const char depstmpname[] = ".ninja_deps.tmp";
static const char depsheader[] = "# ninjadeps\n";
static const uint32_t depsver = 3;
static FILE *depsfile;
static uint32_t *depsbuf;
static struct entry *entries;
static size_t entrieslen, entriescap;

static bool
recordid(struct node *n)
{
	uint32_t sz;

	if (n->id != -1)
		return false;
	if (entrieslen == INT32_MAX)
		errx(1, "too many nodes");
	n->id = entrieslen++;
	sz = (n->path->n + 7) & ~3;
	if (sz + 4 >= MAX_RECORD_SIZE)
		errx(1, "ID record too large");
	depsbuf[0] = sz;
	memcpy(&depsbuf[1], n->path->s, n->path->n);
	memset((char *)&depsbuf[1] + n->path->n, 0, sz - n->path->n - 4);
	depsbuf[sz / 4] = ~n->id;
	fwrite(depsbuf, 4 + sz, 1, depsfile);

	return true;
}

static void
recorddeps(struct node *out, struct nodearray *deps, uint32_t mtime)
{
	uint32_t sz;
	size_t i;

	sz = 8 + deps->len * 4;
	if (sz + 4 >= MAX_RECORD_SIZE)
		errx(1, "deps record too large");
	depsbuf[0] = sz | 0x80000000;
	depsbuf[1] = out->id;
	depsbuf[2] = mtime;
	for (i = 0; i < deps->len; ++i)
		depsbuf[3 + i] = deps->node[i]->id;
	fwrite(depsbuf, 4 + sz, 1, depsfile);
}

void
depsinit(const char *builddir)
{
	char *depspath = (char *)depsname, *depstmppath = (char *)depstmpname;
	uint32_t ver, sz, id;
	size_t len, i, j, nrecord;
	bool isdep;
	struct string *path;
	struct node *n;
	struct edge *e;
	struct entry *entry, *oldentries;

	/* XXX: when ninja hits a bad record, it truncates the log to the last
	 * good record. perhaps we should do the same. */

	if (!depsbuf)
		depsbuf = xmalloc(MAX_RECORD_SIZE);
	if (depsfile)
		fclose(depsfile);
	entrieslen = 0;
	if (builddir)
		xasprintf(&depspath, "%s/%s", builddir, depsname);
	depsfile = fopen(depspath, "a+");
	if (!depsfile)
		err(1, "open %s", depspath);
	if (!fgets((char *)depsbuf, sizeof(depsheader), depsfile))
		goto rewrite;
	if (fread(&ver, sizeof(ver), 1, depsfile) != 1) {
		warn("deps read failed");
		goto rewrite;
	}
	if (strcmp((char *)depsbuf, depsheader) != 0) {
		warnx("invalid deps header");
		goto rewrite;
	}
	if (ver != depsver) {
		warnx("unknown deps version");
		goto rewrite;
	}
	for (nrecord = 0;; ++nrecord) {
		if (fread(&sz, sizeof(sz), 1, depsfile) != 1)
			break;
		isdep = sz & 0x80000000;
		sz &= 0x7fffffff;
		if (sz > MAX_RECORD_SIZE) {
			warnx("deps record too large");
			goto rewrite;
		}
		if (fread(depsbuf, sz, 1, depsfile) != 1) {
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
			id = depsbuf[0];
			if (id >= entrieslen) {
				warnx("invalid node ID: %" PRIu32, id);
				goto rewrite;
			}
			entry = &entries[id];
			entry->mtime = depsbuf[1];
			e = entry->node->gen;
			if (!e || !edgevar(e, "deps"))
				continue;
			sz /= 4;
			free(entry->deps.node);
			entry->deps.len = sz;
			entry->deps.node = xreallocarray(NULL, sz, sizeof(n));
			for (i = 0; i < sz; ++i) {
				id = depsbuf[2 + i];
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
			if (entrieslen != ~depsbuf[sz / 4 - 1]) {
				warnx("corrupt deps log, bad checksum");
				goto rewrite;
			}
			if (entrieslen == INT32_MAX) {
				warnx("too many nodes in deps log");
				goto rewrite;
			}
			len = sz - 4;
			while (((char *)depsbuf)[len - 1] == '\0')
				--len;
			path = mkstr(len);
			memcpy(path->s, depsbuf, len);
			path->s[len] = '\0';

			n = mknode(path);
			if (entrieslen >= entriescap) {
				entriescap = entriescap ? entriescap * 2 : 1024;
				entries = xreallocarray(entries, entriescap, sizeof(entries[0]));
			}
			n->id = entrieslen;
			entries[entrieslen++] = (struct entry){.node = n};
		}
	}
	if (ferror(depsfile)) {
		warn("deps read failed");
		goto rewrite;
	}
	if (nrecord <= 1000 || nrecord < 3 * entrieslen) {
		if (builddir)
			free(depspath);
		return;
	}

rewrite:
	fclose(depsfile);
	if (builddir)
		xasprintf(&depstmppath, "%s/%s", builddir, depstmpname);
	depsfile = fopen(depstmppath, "w");
	if (!depsfile)
		err(1, "open %s", depstmppath);
	fwrite(depsheader, 1, sizeof(depsheader) - 1, depsfile);
	fwrite(&depsver, 1, sizeof(depsver), depsfile);

	/* reset ID for all current entries */
	for (i = 0; i < entrieslen; ++i)
		entries[i].node->id = -1;
	/* save a temporary copy of the old entries */
	oldentries = xreallocarray(NULL, entrieslen, sizeof(entries[0]));
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
	if (rename(depstmppath, depspath) < 0)
		err(1, "deps file rename failed");
	if (builddir) {
		free(depstmppath);
		free(depspath);
	}
}

void
depsclose(void)
{
	fflush(depsfile);
	if (ferror(depsfile))
		errx(1, "deps file write failed");
	fclose(depsfile);
}

static struct nodearray *
depsparse(const char *name, struct string *out)
{
	static struct buffer buf;
	static struct nodearray deps;
	static size_t depscap;
	struct string *path;
	FILE *f;
	int c, n;
	bool sawcolon;

	f = fopen(name, "r");
	if (!f)
		return NULL;
	sawcolon = false;
	buf.len = 0;
	deps.len = 0;
	for (;;) {
		c = fgetc(f);
		switch (c) {
		case '\\':
			/* handle the crazy escaping generated by clang and gcc */
			n = 0;
			do {
				c = fgetc(f);
				if (++n % 2 == 0)
					bufadd(&buf, '\\');
			} while (c == '\\');
			switch (c) {
			case '#':
				/* assume no comments */
				for (; n > 2; n -= 2)
					bufadd(&buf, '\\');
				bufadd(&buf, c);
				continue;
			case ' ':
			case '\t':
				if (n % 2 != 0) {
					bufadd(&buf, c);
					continue;
				}
				/* fallthrough */
			default:
				for (; n > 0; n -= 2)
					bufadd(&buf, '\\');
			}
			break;
		case '$':
			c = fgetc(f);
			if (c != '$') {
				warnx("bad depfile: contains variable reference");
				goto err;
			}
			bufadd(&buf, c);
			continue;
		}
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
					deps.node = xreallocarray(deps.node, depscap, sizeof(deps.node[0]));
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
	struct nodearray *deps = NULL;
	struct node *n;

	n = e->out[0];
	deptype = edgevar(e, "deps");
	if (deptype) {
		if (n->id != -1 && (n->mtime < 0 || n->mtime / 1000000000 <= entries[n->id].mtime))
			deps = &entries[n->id].deps;
		else if (buildopts.explain)
			warnx("explain %s: missing or outdated record in .ninja_deps", n->path->s);
	} else {
		depfile = edgevar(e, "depfile");
		if (!depfile)
			return;
		deps = depsparse(depfile->s, n->path);
		if (buildopts.explain && !deps)
			warnx("explain %s: missing or invalid dep file", n->path->s);
	}
	if (deps) {
		edgeadddeps(e, deps->node, deps->len);
	} else {
		n->dirty = true;
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
	uint32_t mtime;

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
	mtime = out->mtime / 1000000000;
	deps = depsparse(depfile->s, out->path);
	remove(depfile->s);
	if (!deps)
		return;
	update = false;
	entry = NULL;
	if (recordid(out)) {
		update = true;
	} else {
		entry = &entries[out->id];
		if (entry->mtime != mtime || entry->deps.len != deps->len)
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
		recorddeps(out, deps, mtime);
}
