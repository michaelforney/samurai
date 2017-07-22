#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "env.h"
#include "graph.h"
#include "lex.h"
#include "parse.h"
#include "util.h"

extern FILE *f;
struct node **deftarg;
size_t ndeftarg;

void
parseinit(void)
{
	free(deftarg);
	deftarg = NULL;
	ndeftarg = 0;
}

static void
parselet(struct evalstring **val)
{
	expect(EQUALS);
	*val = readstr(false);
	expect(NEWLINE);
}

void
parserule(struct environment *env)
{
	struct rule *r;
	char *var;
	struct evalstring *val;

	r = mkrule(readident());
	expect(NEWLINE);
	while (peek() == INDENT) {
		next();
		var = readident();
		parselet(&val);
		ruleaddvar(r, var, val);
	}
	envaddrule(env, r);
}

static void
pushstr(struct evalstring ***end, struct evalstring *str)
{
	str->next = NULL;
	**end = str;
	*end = &str->next;
}

static void
parseedge(struct environment *env)
{
	struct edge *e;
	struct evalstring *out, *in, *str, **end;
	char *var;
	struct string *s;
	struct node **n;

	e = mkedge(env);

	for (out = NULL, end = &out; (str = readstr(true)); ++e->nout)
		pushstr(&end, str);
	e->outimpidx = e->nout;
	if (peek() == PIPE) {
		for (next(); (str = readstr(true)); ++e->nout)
			pushstr(&end, str);
	}
	expect(COLON);
	ident = readident();
	e->rule = envrule(env, ident);
	free(ident);
	for (in = NULL, end = &in; (str = readstr(true)); ++e->nin)
		pushstr(&end, str);
	e->inimpidx = e->nin;
	if (peek() == PIPE) {
		for (next(); (str = readstr(true)); ++e->nin)
			pushstr(&end, str);
	}
	e->inorderidx = e->nin;
	if (peek() == PIPE2) {
		for (next(); (str = readstr(true)); ++e->nin)
			pushstr(&end, str);
	}
	expect(NEWLINE);
	while (peek() == INDENT) {
		next();
		var = readident();
		parselet(&str);
		s = enveval(env, str);
		envaddvar(e->env, var, s);
		delstr(str);
	}

	e->out = xmalloc(e->nout * sizeof(*n));
	for (n = e->out; out; out = str, ++n) {
		str = out->next;
		s = enveval(e->env, out);
		delstr(out);
		canonpath(s);
		*n = mknode(s);
		if ((*n)->gen)
			errx(1, "multiple rules generate '%s'", s->s);
		(*n)->gen = e;
	}

	e->in = xmalloc(e->nin * sizeof(*n));
	for (n = e->in; in; in = str, ++n) {
		str = in->next;
		s = enveval(e->env, in);
		delstr(in);
		canonpath(s);
		*n = mknode(s);
		++(*n)->nuse;
	}

	s = edgevar(e, "pool");
	if (s)
		e->pool = poolget(s->s);
}

static void
parseinclude(struct environment *env, bool newscope)
{
	FILE *oldf = f;
	struct evalstring *str;
	struct string *path;

	str = readstr(true);
	if (!str)
		errx(1, "expected include path");
	expect(NEWLINE);
	path = enveval(env, str);
	delstr(str);

	f = fopen(path->s, "r");
	if (!f)
		err(1, "fopen %s", path->s);
	if (newscope)
		env = mkenv(env);
	parse(env);
	fclose(f);
	free(path);
	f = oldf;
}

static void
parsedefault(struct environment *env)
{
	struct evalstring *targ, *str, **end;
	struct string *path;
	struct node *n;
	size_t i, ntarg;

	for (targ = NULL, ntarg = 0, end = &targ; (str = readstr(true)); ++ntarg)
		pushstr(&end, str);
	deftarg = xrealloc(deftarg, (ndeftarg + ntarg) * sizeof(*deftarg));
	for (i = 0; targ; targ = str, ++i) {
		str = targ->next;
		path = enveval(env, targ);
		delstr(targ);
		n = nodeget(path->s);
		if (!n)
			errx(1, "unknown target '%s'", path->s);
		free(path);
		deftarg[ndeftarg++] = n;
	}
	expect(NEWLINE);
}

static void
parsepool(struct environment *env)
{
	struct pool *p;
	struct evalstring *val;
	struct string *s;
	char *var, *end;

	p = mkpool(readident());
	expect(NEWLINE);
	while (peek() == INDENT) {
		next();
		var = readident();
		parselet(&val);
		if (strcmp(var, "depth") == 0) {
			s = enveval(env, val);
			p->maxjobs = strtol(s->s, &end, 10);
			if (*end)
				errx(1, "invalid pool depth: %s", s->s);
		} else {
			errx(1, "unexpected pool variable: %s", var);
		}
	}
	if (!p->maxjobs)
		errx(1, "pool has no depth: %s", p->name);
}

void
parse(struct environment *env)
{
	int c;
	char *var;
	struct string *val;
	struct evalstring *str;

	for (;;) {
		c = next();
		switch (c) {
		case RULE:
			parserule(env);
			break;
		case BUILD:
			parseedge(env);
			break;
		case INCLUDE:
		case SUBNINJA:
			parseinclude(env, c == SUBNINJA);
			break;
		case IDENT:
			var = ident;
			parselet(&str);
			val = enveval(env, str);
			envaddvar(env, var, val);
			delstr(str);
			break;
		case DEFAULT:
			parsedefault(env);
			break;
		case POOL:
			parsepool(env);
			break;
		case EOF:
			return;
		case NEWLINE:
			break;
		default:
			errx(1, "unexpected token: %s", tokstr(c));
		}
	}
}
