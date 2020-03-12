#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static void
printjson(const char *s, size_t n, bool join)
{
	size_t i;
	char c;

	for (i = 0; i < n; ++i) {
		c = s[i];
		switch (c) {
		case '"':
		case '\\':
			putchar('\\');
			break;
		case '\n':
			if (join)
				c = ' ';
			break;
		case '\0':
			return;
		}
		putchar(c);
	}
}

static int
compdb(int argc, char *argv[])
{
	char dir[PATH_MAX], *p;
	struct edge *e;
	struct string *cmd, *rspfile, *content;
	bool expandrsp = false, first = true;
	int i;
	size_t off;

	ARGBEGIN {
	case 'x':
		expandrsp = true;
		break;
	default:
		fprintf(stderr, "usage: %s ... -t compdb [-x] rules...\n", argv0);
		return 2;
	} ARGEND

	if (!getcwd(dir, sizeof(dir)))
		fatal("getcwd:");

	putchar('[');
	for (e = alledges; e; e = e->allnext) {
		if (e->nin == 0)
			continue;
		for (i = 0; i < argc; ++i) {
			if (strcmp(e->rule->name, argv[i]) == 0) {
				if (first)
					first = false;
				else
					putchar(',');

				printf("\n  {\n    \"directory\": \"");
				printjson(dir, -1, false);

				printf("\",\n    \"command\": \"");
				cmd = edgevar(e, "command", true);
				rspfile = expandrsp ? edgevar(e, "rspfile", true) : NULL;
				p = rspfile ? strstr(cmd->s, rspfile->s) : NULL;
				if (!p || p == cmd->s || p[-1] != '@') {
					printjson(cmd->s, cmd->n, false);
				} else {
					off = p - cmd->s;
					printjson(cmd->s, off - 1, false);
					content = edgevar(e, "rspfile_content", true);
					printjson(content->s, content->n, true);
					off += rspfile->n;
					printjson(cmd->s + off, cmd->n - off, false);
				}

				printf("\",\n    \"file\": \"");
				printjson(e->in[0]->path->s, -1, false);

				printf("\",\n    \"output\": \"");
				printjson(e->out[0]->path->s, -1, false);

				printf("\"\n  }");
				break;
			}
		}
	}
	puts("\n]");

	fflush(stdout);
	if (ferror(stdout))
		fatal("write failed");

	return 0;
}

static void
targets_rule(int argc, char *argv[])
{
	struct edge *e;
	size_t i;
	for (e = alledges; e; e = e->allnext) {
		if (argc == 2) {
			for (i = 0; i < e->nin; ++i)
				if (!e->in[i]->gen) {
					printf("%s\n", e->in[i]->path->s);
			}
		} else if (argc == 3) {
			if (strcmp(e->rule->name, argv[2]) == 0) {
				for (i = 0; i < e->nout; ++i)
					printf("%s\n", e->out[i]->path->s);
			}
		}
	}
}

static void
targets_depth(struct node *n, size_t depth, size_t indent)
{
	struct edge *e = n->gen;
	size_t i;

	for (i = 0; i < indent; ++i)
		printf("  ");
	if (e) {
		printf("%s: %s\n", n->path->s, e->rule->name);
		if (depth > 1 || depth <= 0) {
			for (i = 0; i < e->nin; ++i)
				targets_depth(e->in[i], depth - 1, indent + 1);
		}
	} else {
		printf("%s\n", n->path->s);
	}
}

static void
targets_all(void)
{
	struct edge *e = NULL;
	size_t i;
	for (e = alledges; e; e = e->allnext) {
		for (i = 0; i < e->nout; ++i)
			printf("%s: %s\n", e->out[i]->path->s, e->rule->name);
	}
}

static int
targets(int argc, char *argv[])
{
	size_t depth = 1;
	if (argc >= 2) {
		const char *mode = argv[1];
		if (strcmp(mode, "rule") == 0) {
			targets_rule(argc, argv);
			return 0;
		} else if (strcmp(mode, "depth") == 0) {
			if (argc > 2) {
				char *end;
				depth = (size_t)strtol(argv[2], &end, 10);
				if (*end)
					fprintf(stderr, "depth not valid");
			}
		} else if (strcmp(mode, "all") == 0) {
			targets_all();
			return 0;
		} else {
			fprintf(stderr, "unknown target tool mode '%s'\n", mode);
			return 2;
		}
	}

	struct edge *e = NULL;
	size_t n;
	for (e = alledges; e; e = e->allnext) {
		for (n = 0; n < e->nout; ++n) {
			if (e->out[n]->nuse != 0)
				continue;

			targets_depth(e->out[n], depth, 0);
		}
	}

	return 0;
}

static const struct tool tools[] = {
	{"clean", clean},
	{"compdb", compdb},
	{"targets", targets},
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
