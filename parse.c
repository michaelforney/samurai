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
static struct node **deftarg;
static size_t ndeftarg;

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
	bool hascommand = false, hasrspfile = false, hasrspcontent = false;

	r = mkrule(scanname(s));
	scannewline(s);
	while (scanindent(s)) {
		var = scanname(s);
		parselet(s, &val);
		ruleaddvar(r, var, val);
		if (!val)
			continue;
		if (strcmp(var, "command") == 0)
			hascommand = true;
		else if (strcmp(var, "rspfile") == 0)
			hasrspfile = true;
		else if (strcmp(var, "rspfile_content") == 0)
			hasrspcontent = true;
	}
	if (!hascommand)
		fatal("rule '%s' has no command", r->name);
	if (hasrspfile != hasrspcontent)
		fatal("rule '%s' has rspfile and no rspfile_content or vice versa", r->name);
	envaddrule(env, r);
}

static void
parseedge(struct scanner *s, struct environment *env)
{
	struct edge *e;
	struct evalstring *str, **path;
	char *name;
	struct string *val;
	struct node *n;
	size_t i;
	int p;

	e = mkedge(env);

	scanpaths(s);
	e->outimpidx = npaths;
	if (scanpipe(s, 1))
		scanpaths(s);
	e->nout = npaths;
	if (e->nout == 0)
		scanerror(s, "expected output path");
	scanchar(s, ':');
	name = scanname(s);
	e->rule = envrule(env, name);
	if (!e->rule)
		fatal("undefined rule '%s'", name);
	free(name);
	scanpaths(s);
	e->inimpidx = npaths - e->nout;
	p = scanpipe(s, 1 | 2);
	if (p == 1) {
		scanpaths(s);
		p = scanpipe(s, 2);
	}
	e->inorderidx = npaths - e->nout;
	if (p == 2)
		scanpaths(s);
	e->nin = npaths - e->nout;
	scannewline(s);
	while (scanindent(s)) {
		name = scanname(s);
		parselet(s, &str);
		val = enveval(env, str);
		envaddvar(e->env, name, val);
	}

	e->out = xreallocarray(NULL, e->nout, sizeof(e->out[0]));
	for (i = 0, path = paths; i < e->nout; ++path) {
		val = enveval(e->env, *path);
		canonpath(val);
		n = mknode(val);
		if (n->gen) {
			if (!parseopts.dupbuildwarn)
				fatal("multiple rules generate '%s'", n->path->s);
			warn("multiple rules generate '%s'", n->path->s);
			--e->nout;
			if (i < e->outimpidx)
				--e->outimpidx;
		} else {
			n->gen = e;
			e->out[i] = n;
			++i;
		}
	}
	e->in = xreallocarray(NULL, e->nin, sizeof(e->in[0]));
	for (i = 0; i < e->nin; ++i, ++path) {
		val = enveval(e->env, *path);
		canonpath(val);
		n = mknode(val);
		e->in[i] = n;
		nodeuse(n, e);
	}
	npaths = 0;

	val = edgevar(e, "pool", true);
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

	if (newscope)
		env = mkenv(env);
	parse(path->s, env);
	free(path);
}

static void
parsedefault(struct scanner *s, struct environment *env)
{
	struct string *path;
	struct node *n;
	size_t i;

	scanpaths(s);
	deftarg = xreallocarray(deftarg, ndeftarg + npaths, sizeof(*deftarg));
	for (i = 0; i < npaths; ++i) {
		path = enveval(env, paths[i]);
		canonpath(path);
		n = nodeget(path->s, path->n);
		if (!n)
			fatal("unknown target '%s'", path->s);
		free(path);
		deftarg[ndeftarg++] = n;
	}
	scannewline(s);
	npaths = 0;
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
				fatal("invalid pool depth '%s'", str->s);
			free(str);
		} else {
			fatal("unexpected pool variable '%s'", var);
		}
	}
	if (!p->maxjobs)
		fatal("pool '%s' has no depth", p->name);
}

static void
checkversion(const char *ver)
{
	int major, minor = 0;

	if (sscanf(ver, "%d.%d", &major, &minor) < 1)
		fatal("invalid ninja_required_version");
	if (major > ninjamajor || (major == ninjamajor && minor > ninjaminor))
		fatal("ninja_required_version %s is newer than %d.%d", ver, ninjamajor, ninjaminor);
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
			break;
		case EOF:
			scanclose(&s);
			return;
		}
	}
}

void
defaultnodes(void fn(struct node *))
{
	struct edge *e;
	struct node *n;
	size_t i;

	if (ndeftarg > 0) {
		for (i = 0; i < ndeftarg; ++i)
			fn(deftarg[i]);
	} else {
		/* by default build all nodes which are not used by any edges */
		for (e = alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (n->nuse == 0)
					fn(n);
			}
		}
	}
}
