#include <err.h>
#include <search.h>
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

static void
parselet(char **var, struct string **val)
{
	*var = ident;
	expect(EQUALS);
	*val = readstr(false);
	expect(NEWLINE);
}

void
parserule(struct environment *env)
{
	struct rule *r;
	char *var;
	struct string *val;

	r = xmalloc(sizeof(*r));
	expect(IDENT);
	r->name = ident;
	expect(NEWLINE);
	r->bindings = NULL;
	while (peek() == INDENT) {
		next();
		expect(IDENT);
		parselet(&var, &val);
		ruleaddvar(r, var, val);
	}
	envaddrule(env, r);
}

static void
pushstr(struct string ***end, struct string *str)
{
	str->next = NULL;
	**end = str;
	*end = &str->next;
}

static void
parseedge(struct environment *env)
{
	struct edge *e;
	struct string *out, *in, *str, **end;
	char *var, *val, *s;
	struct node **n;

	e = mkedge();

	end = &out;
	for (; (str = readstr(true)); ++e->nout)
		pushstr(&end, str);
	if (peek() == PIPE) {
		e->outimpidx = e->nout;
		for (next(); (str = readstr(true)); ++e->nout)
			pushstr(&end, str);
	}
	expect(COLON);
	expect(IDENT);
	e->rule = envrule(env, ident);
	end = &in;
	for (; (str = readstr(true)); ++e->nin)
		pushstr(&end, str);
	if (peek() == PIPE) {
		e->inimpidx = e->nin;
		for (next(); (str = readstr(true)); ++e->nin)
			pushstr(&end, str);
	}
	if (peek() == PIPE2) {
		e->inorderidx = e->nin;
		for (next(); (str = readstr(true)); ++e->nin)
			pushstr(&end, str);
	}
	expect(NEWLINE);
	if (peek() == INDENT) {
		e->env = mkenv(env);
		do {
			next();
			expect(IDENT);
			parselet(&var, &str);
			val = enveval(env, str);
			envaddvar(e->env, var, val);
			delstr(str);
		} while (peek() == INDENT);
	} else {
		e->env = env;
	}

	e->out = xmalloc(e->nout * sizeof(*n));
	for (str = out, n = e->out; str; str = out, ++n) {
		out = str->next;
		s = enveval(e->env, str);
		delstr(str);
		*n = nodeget(s, true);
		if ((*n)->gen)
			errx(1, "multiple rules generate '%s'", s);
		(*n)->gen = e;
	}

	e->in = xmalloc(e->nin * sizeof(*n));
	for (str = in, n = e->in; str; str = in, ++n) {
		in = str->next;
		s = enveval(e->env, str);
		delstr(str);
		*n = nodeget(s, true);
		++(*n)->nuse;
	}
}

static void
parseinclude(struct environment *env, bool newscope)
{
	FILE *oldf = f;
	struct string *str;
	char *path;

	str = readstr(true);
	if (!str)
		errx(1, "expected include path");
	expect(NEWLINE);
	path = enveval(env, str);
	delstr(str);

	f = fopen(path, "r");
	if (!f)
		err(1, "fopen %s", path);
	if (newscope)
		env = mkenv(env);
	parse(env);
	fclose(f);
	free(path);
	f = oldf;
}

void
parse(struct environment *env)
{
	int c;
	char *var, *val;
	struct string *str;

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
			parselet(&var, &str);
			val = enveval(env, str);
			envaddvar(env, var, val);
			delstr(str);
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
