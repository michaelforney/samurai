#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "graph.h"
#include "parse.h"
#include "scan.h"
#include "util.h"

struct parseoptions parseopts;
const char *ninjaversion = "1.8.2";
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
parselet(struct scanner *s, struct evalstring **val)
{
	scanchar(s, '=');
	*val = scanstring(s, false);
	scannewline(s);
}

static void
parserule(struct scanner *s, struct environment *env)
{
	struct rule *r;
	char *var;
	struct evalstring *val;

	r = mkrule(scanname(s));
	scannewline(s);
	while (scanindent(s)) {
		var = scanname(s);
		parselet(s, &val);
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
parseedge(struct scanner *s, struct environment *env)
{
	struct edge *e;
	struct evalstring *out, *in, *str, **end;
	char *name;
	struct string *val;
	struct node *n;
	size_t i;
	int p;

	e = mkedge(env);

	for (out = NULL, end = &out; (str = scanstring(s, true)); ++e->nout)
		pushstr(&end, str);
	e->outimpidx = e->nout;
	if (scanpipe(s, 1)) {
		for (; (str = scanstring(s, true)); ++e->nout)
			pushstr(&end, str);
	}
	if (e->nout == 0)
		scanerror(s, "expected output path");
	scanchar(s, ':');
	name = scanname(s);
	e->rule = envrule(env, name);
	if (!e->rule)
		errx(1, "undefined rule: %s", name);
	free(name);
	for (in = NULL, end = &in; (str = scanstring(s, true)); ++e->nin)
		pushstr(&end, str);
	e->inimpidx = e->nin;
	p = scanpipe(s, 1|2);
	if (p == 1) {
		for (; (str = scanstring(s, true)); ++e->nin)
			pushstr(&end, str);
		p = scanpipe(s, 2);
	}
	e->inorderidx = e->nin;
	if (p == 2) {
		for (; (str = scanstring(s, true)); ++e->nin)
			pushstr(&end, str);
	}
	scannewline(s);
	while (scanindent(s)) {
		name = scanname(s);
		parselet(s, &str);
		val = enveval(env, str);
		envaddvar(e->env, name, val);
		delevalstr(str);
	}

	e->out = xreallocarray(NULL, e->nout, sizeof(*n));
	for (i = 0; i < e->nout; out = str) {
		str = out->next;
		val = enveval(e->env, out);
		delevalstr(out);
		canonpath(val);
		n = mknode(val);
		if (n->gen) {
			if (parseopts.dupbuilderr)
				errx(1, "multiple rules generate '%s'", n->path->s);
			warnx("multiple rules generate '%s'", n->path->s);
			--e->nout;
			if (i < e->outimpidx)
				--e->outimpidx;
		} else {
			n->gen = e;
			e->out[i] = n;
			++i;
		}
	}

	e->in = xreallocarray(NULL, e->nin, sizeof(*n));
	for (i = 0; i < e->nin; in = str, ++i) {
		str = in->next;
		val = enveval(e->env, in);
		delevalstr(in);
		canonpath(val);
		e->in[i] = mknode(val);
	}

	val = edgevar(e, "pool");
	if (val)
		e->pool = poolget(val->s);
}

static void
parseinclude(struct scanner *s, struct environment *env, bool newscope)
{
	struct evalstring *str;
	struct string *path;

	str = scanstring(s, true);
	if (!str)
		scanerror(s, "expected include path");
	scannewline(s);
	path = enveval(env, str);
	delevalstr(str);

	if (newscope)
		env = mkenv(env);
	parse(path->s, env);
	free(path);
}

static void
parsedefault(struct scanner *s, struct environment *env)
{
	struct evalstring *targ, *str, **end;
	struct string *path;
	struct node *n;
	size_t ntarg;

	for (targ = NULL, ntarg = 0, end = &targ; (str = scanstring(s, true)); ++ntarg)
		pushstr(&end, str);
	deftarg = xreallocarray(deftarg, ndeftarg + ntarg, sizeof(*deftarg));
	for (; targ; targ = str) {
		str = targ->next;
		path = enveval(env, targ);
		delevalstr(targ);
		n = nodeget(path->s);
		if (!n)
			errx(1, "unknown target '%s'", path->s);
		free(path);
		deftarg[ndeftarg++] = n;
	}
	scannewline(s);
}

static void
parsepool(struct scanner *s, struct environment *env)
{
	struct pool *p;
	struct evalstring *val;
	struct string *str;
	char *var, *end;

	p = mkpool(scanname(s));
	scannewline(s);
	while (scanindent(s)) {
		var = scanname(s);
		parselet(s, &val);
		if (strcmp(var, "depth") == 0) {
			str = enveval(env, val);
			p->maxjobs = strtol(str->s, &end, 10);
			if (*end)
				errx(1, "invalid pool depth: %s", str->s);
			free(str);
		} else {
			errx(1, "unexpected pool variable: %s", var);
		}
	}
	if (!p->maxjobs)
		errx(1, "pool has no depth: %s", p->name);
}

static void
checkversion(const char *ver)
{
	if (strcmp(ver, ninjaversion) > 0)
		errx(1, "ninja_required_version is newer than %s: %s", ninjaversion, ver);
}

void
parse(const char *name, struct environment *env)
{
	struct scanner s;
	char *var;
	struct string *val;
	struct evalstring *str;

	scaninit(&s, name);
	for (;;) {
		switch (scankeyword(&s, &var)) {
		case RULE:
			parserule(&s, env);
			break;
		case BUILD:
			parseedge(&s, env);
			break;
		case INCLUDE:
			parseinclude(&s, env, false);
			break;
		case SUBNINJA:
			parseinclude(&s, env, true);
			break;
		case DEFAULT:
			parsedefault(&s, env);
			break;
		case POOL:
			parsepool(&s, env);
			break;
		case VARIABLE:
			parselet(&s, &str);
			val = enveval(env, str);
			if (strcmp(var, "ninja_required_version") == 0)
				checkversion(val->s);
			envaddvar(env, var, val);
			delevalstr(str);
			break;
		case EOF:
			scanclose(&s);
			return;
		}
	}
}
