#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "dyndep.h"
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

static bool
parselet(struct scanner *s, struct evalstring **val)
{
	if (!scanchar(s, '=') || !scanstring(s, false, val))
		return false;
	return scannewline(s);
}

static bool
parserule(struct scanner *s, struct environment *env)
{
	struct rule *r;
	char *var;
	struct evalstring *val;
	bool hascommand = false, hasrspfile = false, hasrspcontent = false;
	int indent;

	var = scanname(s);
	if (!var)
		return false;
	r = mkrule(var);
	if (!scannewline(s))
		return false;
	while ((indent = scanindent(s)) > 0) {
		var = scanname(s);
		if (!var || !parselet(s, &val))
			return false;
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
	if (indent < 0)
		return false;
	if (!hascommand)
		fatal("rule '%s' has no command", r->name);
	if (hasrspfile != hasrspcontent)
		fatal("rule '%s' has rspfile and no rspfile_content or vice versa", r->name);
	envaddrule(env, r);
	return true;
}

static bool
parseedge(struct scanner *s, struct environment *env)
{
	struct edge *e;
	struct evalstring *str, **path;
	char *name;
	struct string *val, *dynval;
	struct node *n;
	size_t i;
	int p, indent;

	e = mkedge(env);

	if (!scanpaths(s))
		return false;
	e->outimpidx = npaths;
	p = scanpipe(s, 1);
	if (p < 0)
		return false;
	if (p == 1 && !scanpaths(s))
		return false;
	e->nout = npaths;
	if (e->nout == 0) {
		scanerror(s, "expected output path");
		return false;
	}
	if (!scanchar(s, ':'))
		return false;
	name = scanname(s);
	if (!name)
		return false;
	e->rule = envrule(env, name);
	if (!e->rule)
		fatal("undefined rule '%s'", name);
	free(name);
	if (!scanpaths(s))
		return false;
	e->inimpidx = npaths - e->nout;
	p = scanpipe(s, 1 | 2);
	if (p < 0)
		return false;
	if (p == 1) {
		if (!scanpaths(s))
			return false;
		p = scanpipe(s, 2);
		if (p < 0)
			return false;
	}
	e->indynidx = e->inorderidx = npaths - e->nout;
	if (p == 2 && !scanpaths(s))
		return false;
	e->nin = npaths - e->nout;
	if (!scannewline(s))
		return false;
	while ((indent = scanindent(s)) > 0) {
		name = scanname(s);
		if (!name || !parselet(s, &str))
			return false;
		val = enveval(env, str);
		envaddvar(e->env, name, val);
	}
	if (indent < 0)
		return false;

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
	e->outdynidx = e->nout;

	dynval = edgevar(e, "dyndep", false);
	if (dynval && dynval->n)
		canonpath(dynval);

	e->in = xreallocarray(NULL, e->nin, sizeof(e->in[0]));
	for (i = 0; i < e->nin; ++i, ++path) {
		val = enveval(e->env, *path);
		canonpath(val);
		n = mknode(val);
		e->in[i] = n;
		nodeuse(n, e);
		if (dynval && dynval->n && strcmp(n->path->s, dynval->s) == 0) {
			dyndepuse(mkdyndep(n), e);
			dynval = NULL;
		}
	}
	npaths = 0;

	if (dynval && dynval->n)
		fatal("dyndep '%s' is not an input", dynval->s);

	val = edgevar(e, "pool", true);
	if (val)
		e->pool = poolget(val->s);
	return true;
}

static bool
parseinclude(struct scanner *s, struct environment *env, bool newscope)
{
	struct evalstring *str;
	struct string *path;

	if (!scanstring(s, true, &str))
		return false;
	if (!str) {
		scanerror(s, "expected include path");
		return false;
	}
	if (!scannewline(s))
		return false;
	path = enveval(env, str);

	if (newscope)
		env = mkenv(env);
	parse(path->s, env);
	free(path);
	return true;
}

static bool
parsedefault(struct scanner *s, struct environment *env)
{
	struct string *path;
	struct node *n;
	size_t i;

	if (!scanpaths(s))
		return false;
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
	if (!scannewline(s))
		return false;
	npaths = 0;
	return true;
}

static bool
parsepool(struct scanner *s, struct environment *env)
{
	struct pool *p;
	struct evalstring *val;
	struct string *str;
	char *var, *end;
	int indent;

	var = scanname(s);
	if (!var)
		return false;
	p = mkpool(var);
	if (!scannewline(s))
		return false;
	while ((indent = scanindent(s)) > 0) {
		var = scanname(s);
		if (!var || !parselet(s, &val))
			return false;
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
	if (indent < 0)
		return false;
	if (!p->maxjobs)
		fatal("pool '%s' has no depth", p->name);
	return true;
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
		case ERROR:
			scanfatal(&s);
			break;
		case RULE:
			if (!parserule(&s, env))
				scanfatal(&s);
			break;
		case BUILD:
			if (!parseedge(&s, env))
				scanfatal(&s);
			break;
		case INCLUDE:
			if (!parseinclude(&s, env, false))
				scanfatal(&s);
			break;
		case SUBNINJA:
			if (!parseinclude(&s, env, true))
				scanfatal(&s);
			break;
		case DEFAULT:
			if (!parsedefault(&s, env))
				scanfatal(&s);
			break;
		case POOL:
			if (!parsepool(&s, env))
				scanfatal(&s);
			break;
		case VARIABLE:
			if (!parselet(&s, &str))
				scanfatal(&s);
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
