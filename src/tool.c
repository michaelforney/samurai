#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "arg.h"
#include "env.h"
#include "graph.h"
#include "tool.h"
#include "util.h"

static int
cleanpath(struct string *path)
{
	if (path) {
		if (remove(path->s) == 0) {
			printf("remove %s\n", path->s);
		} else if (errno != ENOENT) {
			warn("remove %s:", path->s);
			return -1;
		}
	}

	return 0;
}

static int
cleanedge(struct edge *e)
{
	int ret = 0;
	size_t i;

	for (i = 0; i < e->nout; ++i) {
		if (cleanpath(e->out[i]->path) < 0)
			ret = -1;
	}
	if (cleanpath(edgevar(e, "rspfile", false)) < 0)
		ret = -1;
	if (cleanpath(edgevar(e, "depfile", false)) < 0)
		ret = -1;

	return ret;
}

static int
cleantarget(struct node *n)
{
	int ret = 0;
	size_t i;

	if (!n->gen || n->gen->rule == &phonyrule)
		return 0;
	if (cleanpath(n->path) < 0)
		ret = -1;
	for (i = 0; i < n->gen->nin; ++i) {
		if (cleantarget(n->gen->in[i]) < 0)
			ret = -1;
	}

	return ret;
}

static int
clean(int argc, char *argv[])
{
	int ret = 0;
	bool cleangen = false, cleanrule = false;
	struct edge *e;
	struct node *n;
	struct rule *r;

	ARGBEGIN {
	case 'g':
		cleangen = true;
		break;
	case 'r':
		cleanrule = true;
		break;
	default:
		fprintf(stderr, "usage: %s ... -t clean [-gr] [targets...]\n", argv0);
		return 2;
	} ARGEND


	if (cleanrule) {
		if (!argc)
			fatal("expected a rule to clean");
		for (; *argv; ++argv) {
			r = envrule(rootenv, *argv);
			if (!r) {
				warn("unknown rule '%s'", *argv);
				ret = 1;
				continue;
			}
			for (e = alledges; e; e = e->allnext) {
				if (e->rule != r)
					continue;
				if (cleanedge(e) < 0)
					ret = 1;
			}
		}
	} else if (argc > 0) {
		for (; *argv; ++argv) {
			n = nodeget(*argv, 0);
			if (!n) {
				warn("unknown target '%s'", *argv);
				ret = 1;
				continue;
			}
			if (cleantarget(n) < 0)
				ret = 1;
		}
	} else {
		for (e = alledges; e; e = e->allnext) {
			if (e->rule == &phonyrule)
				continue;
			if (!cleangen && edgevar(e, "generator", true))
				continue;
			if (cleanedge(e) < 0)
				ret = 1;
		}
	}

	return ret;
}

static const struct tool tools[] = {
	{"clean", clean},
};

const struct tool *
toolget(const char *name)
{
	const struct tool *t;
	size_t i;

	t = NULL;
	for (i = 0; i < LEN(tools); ++i) {
		if (strcmp(name, tools[i].name) == 0) {
			t = &tools[i];
			break;
		}
	}
	if (!t)
		fatal("unknown tool '%s'", name);

	return t;
}
