#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "build.h"
#include "dyndep.h"
#include "env.h"
#include "graph.h"
#include "htab.h"
#include "scan.h"
#include "util.h"

struct nodearray {
	struct node **node;
	size_t len;
};

const char *dyndepversion = "1.0";

static struct dyndep *alldyndeps;

void
dyndepinit(void)
{
	struct dyndep *d;

	/* delete old dyndeps in case we rebuilt the manifest */
	while (alldyndeps) {
		d = alldyndeps;
		alldyndeps = d->allnext;
		free(d->use);
		free(d);
	}
}

struct dyndep *
mkdyndep(struct node *n)
{
	struct dyndep *d;

	if (n->dyndep)
		return n->dyndep;

	d = xmalloc(sizeof(*d));
	d->node = n;
	d->use = NULL;
	d->nuse = 0;
	d->update = NULL;
	d->nupdate = 0;
	d->done = false;
	d->allnext = alldyndeps;
	alldyndeps = d;
	n->dyndep = d;

	return d;
}

void
dyndepuse(struct dyndep *d, struct edge *e)
{
	e->dyndep = d;

	/* allocate in powers of two */
	if (!(d->nuse & (d->nuse - 1)))
		d->use = xreallocarray(d->use, d->nuse ? d->nuse * 2 : 1, sizeof(e));
	d->use[d->nuse++] = e;
}

static void
dyndepupdate(struct dyndep *d, struct node *n)
{
	/* allocate in powers of two */
	if (!(d->nupdate & (d->nupdate - 1)))
		d->update = xreallocarray(d->update, d->nupdate ? d->nupdate * 2 : 1, sizeof(n));
	d->update[d->nupdate++] = n;
}

static void
parselet(struct scanner *s, struct evalstring **val)
{
	scanchar(s, '=');
	*val = scanstring(s, false);
	scannewline(s);
}

static void
checkversion(const char *ver)
{
	if (strcmp(ver, dyndepversion) > 0)
		fatal("ninja_dyndep_version %s is newer than %s", ver, dyndepversion);
}

static void
dyndepparseedge(struct scanner *s, struct environment *env)
{
	struct dyndep *d;
	struct edge *e;
	struct node *n;
	static struct nodearray nodes;
	static size_t nodescap;
	struct evalstring *str;
	char *name;
	struct string *val;

	str = scanstring(s, true);
	if (!str)
		scanerror(s, "expected explicit output");

	val = enveval(env, str);
	n = nodeget(val->s, val->n);
	if (!n)
		fatal("unknown target '%s'", val->s);
	e = n->gen;
	if (!e)
		fatal("no action to generate '%s' in dyndep", val->s);
	free(val);
	d = e->dyndep;
	if (!d)
		fatal("target '%s' does not mention this dyndep", n->path->s);

	dyndepupdate(d, n);

	nodes.len = 0;
	if (scanpipe(s, 1)) {
		for (; (str = scanstring(s, true)); ) {
			val = enveval(e->env, str);
			canonpath(val);
			if (nodes.len == nodescap) {
				nodescap = nodes.node ? nodescap * 2 : 32;
				nodes.node = xreallocarray(nodes.node, nodescap, sizeof(nodes.node[0]));
			}
			nodes.node[nodes.len++] = mknode(val);
		}
	}
	if (nodes.len)
		edgeadddynouts(e, nodes.node, nodes.len);

	scanchar(s, ':');
	name = scanname(s);
	if (strcmp(name, "dyndep") != 0)
		fatal("unexpected rule '%s' in dyndep", name);
	free(name);

	nodes.len = 0;
	if (scanpipe(s, 1)) {
		for (; (str = scanstring(s, true)); ) {
			val = enveval(e->env, str);
			canonpath(val);
			if (nodes.len == nodescap) {
				nodescap = nodes.node ? nodescap * 2 : 32;
				nodes.node = xreallocarray(nodes.node, nodescap, sizeof(nodes.node[0]));
			}
			nodes.node[nodes.len++] = mknode(val);
		}
	}
	if (nodes.len)
		edgeadddyndeps(e, nodes.node, nodes.len);

	scannewline(s);
	while (scanindent(s)) {
		name = scanname(s);
		if (strcmp(name, "restat") != 0)
			fatal("unexpected variable '%s' in dyndep", name);
		parselet(s, &str);
		envaddvar(e->env, "restat", enveval(env, str));
	}

	e->flags |= FLAG_DYNDEP;
}

static void
dyndepparse(const char *name)
{
	struct scanner s;
	struct environment *env;
	struct evalstring *str;
	struct string *val;
	bool version = false;
	char *var;

	scaninit(&s, name);

	env = mkenv(NULL);
	for (;;) {
		switch (scankeyword(&s, &var)) {
		case BUILD:
			if (!version)
				goto version;
			dyndepparseedge(&s, env);
			break;
		case VARIABLE:
			if (version)
				scanerror(&s, "unexpected variable '%s'", var);
			parselet(&s, &str);
			val = enveval(env, str);
			if (strcmp(var, "ninja_dyndep_version") == 0)
				checkversion(val->s);
			envaddvar(env, var, val);
			version = true;
			break;
		case EOF:
			if (!version)
				goto version;
			scanclose(&s);
			return;
		default:
			scanerror(&s, "unexpected keyword");
		}
	}
version:
	scanerror(&s, "expected 'ninja_dyndep_version'");
}

bool
dyndepload(struct dyndep *d, bool prune)
{
	const char *name;
	struct edge *e;
	struct node *n;
	size_t i;

	if (d->done)
		return true;

	name = d->node->path->s;

	if (!d->node->gen)
		fatal("dyndep not created by any action: '%s'", name);

	if (!prune) {
		/* only load dyndep file if its clean and nothing is blocking it */
		if (d->node->gen->flags & FLAG_DIRTY || d->node->gen->nblock > 0)
			return false;
	} else {
		nodestat(d->node);
		if (d->node->mtime == MTIME_MISSING)
			return false;
	}

	if (buildopts.explain)
		warn("loading dyndep file: '%s'", name);

	dyndepparse(name);

	d->done = true;

	if (prune)
		return true;

	/* verify that all nodes with this dyndep are loaded */
	for (i = 0; i < d->nuse; i++) {
		e = d->use[i];
		if (!(e->flags & FLAG_DYNDEP))
			fatal("target '%s' not mentioned in dyndep file '%s'",
			    e->out[0]->path->s, d->node->path->s);
	}
	/* (re)-schedule the nodes updated by this dyndep */
	for (i = 0; i < d->nupdate; i++) {
		n = d->update[i];
		/* only update the node if it was previously added to the build */
		if (n->gen->flags & FLAG_WORK)
			buildupdate(n);
		n->gen->flags &= ~FLAG_DYNDEP;
	}

	return true;
}
