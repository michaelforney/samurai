#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "util.h"

struct nodearray {
	struct node **node;
	size_t len;
};

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
		if (isalnum(c) || strchr("._-/+", c)) {
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
	struct string *depfile;
	struct nodearray *deps;

	depfile = edgevar(e, "depfile");
	if (!depfile)
		return;
	deps = depsparse(depfile->s, e->out[0]->path);
	if (deps)
		edgeadddeps(e, deps->node, deps->len);
	else
		e->flags |= FLAG_DIRTY_OUT;
}
