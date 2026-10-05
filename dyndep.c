#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "dyndep.h"
#include "env.h"
#include "graph.h"
#include "scan.h"
#include "util.h"

struct dyndepentry {
	struct edge *edge;
	struct node **outs;
	size_t nouts;
	struct node **ins;
	size_t nins;
	bool restat;
};

struct nodearray {
	struct node **node;
	size_t len, cap;
};

struct parse_state {
	jmp_buf jmp;
	struct scanner scanner;
	struct dyndepentry *entries;
	size_t nentries, capentries;
	/* paths of the statement currently being parsed */
	struct nodearray outs, ins;
};

static struct dyndep *alldyndeps;

static void
addnode(struct nodearray *nodes, struct node *n)
{
	if (nodes->len == nodes->cap) {
		nodes->cap = nodes->cap ? nodes->cap * 2 : 32;
		nodes->node = xreallocarray(nodes->node, nodes->cap, sizeof(nodes->node[0]));
	}
	nodes->node[nodes->len++] = n;
}

static void
resetpaths(struct nodearray *a)
{
	a->node = NULL;
	a->len = a->cap = 0;
}

static void
freepaths(struct nodearray *a)
{
	free(a->node);
	resetpaths(a);
}

static void
parselet(struct scanner *s, struct evalstring **val)
{
	scanchar(s, '=');
	*val = scanstring(s, false);
	scannewline(s);
}

/* parse a decimal integer the way atoi() does, stopping at the first
 * non-digit and yielding 0 if there is no digit at all.  Only 0 and 1 are
 * meaningful for version numbers, so the value saturates to avoid overflow. */
static long
parseint(const char *s, const char **end)
{
	long val = 0;

	while (*s == ' ' || *s == '\t')
		++s;
	for (; *s >= '0' && *s <= '9'; ++s) {
		if (val < 2)
			val = val * 10 + (*s - '0');
	}
	*end = s;
	return val;
}

static bool
parseversion(const char *ver)
{
	const char *end;
	long major, minor;

	/* Ninja parses the major and minor versions with atoi(), which
	 * ignores any trailing garbage and yields 0 if there is no number. */
	major = parseint(ver, &end);
	minor = *end == '.' ? parseint(end + 1, &end) : 0;
	return major == 1 && minor == 0;
}

static void
parsepaths(struct scanner *s, struct environment *env, struct nodearray *nodes)
{
	struct evalstring *str;
	struct string *val;

	nodes->len = 0;
	while ((str = scanstring(s, true))) {
		val = enveval(env, str);
		if (val->n == 0)
			scanerror(s, "empty path in dyndep file");
		canonpath(val);
		addnode(nodes, mknode(val));
	}
}

static void
parseedge(struct scanner *s, struct environment *env, struct parse_state *p)
{
	struct dyndepentry *entry;
	struct node *n;
	struct edge *e;
	struct string *val;
	struct evalstring *str;
	struct nodearray *outs = &p->outs, *ins = &p->ins;
	char *name;
	size_t i;
	int pipe;
	bool seenrestat = false, restat = false;

	str = scanstring(s, true);
	if (!str)
		scanerror(s, "expected explicit output");
	val = enveval(env, str);
	if (val->n == 0)
		scanerror(s, "empty explicit output in dyndep file");
	canonpath(val);
	n = nodeget(val->s, val->n);
	if (!n || !n->gen)
		scanerror(s, "no build statement exists for '%s'", val->s);
	e = n->gen;
	free(val);

	for (i = 0; i < p->nentries; ++i)
		if (p->entries[i].edge == e)
			scanerror(s, "multiple statements for '%s'", n->path->s);

	pipe = scanpipe(s, 1 | 2);
	if (pipe == 2)
		scanerror(s, "order-only outputs are not supported in dyndep files");
	if (pipe == 1)
		parsepaths(s, env, outs);
	scanchar(s, ':');
	name = scanname(s);
	if (strcmp(name, "dyndep") != 0)
		scanerror(s, "expected build command name 'dyndep'");
	free(name);

	str = scanstring(s, true);
	if (str) {
		delevalstr(str);
		scanerror(s, "explicit inputs are not supported in dyndep files");
	}
	pipe = scanpipe(s, 1 | 2);
	if (pipe == 2)
		scanerror(s, "order-only inputs are not supported in dyndep files");
	if (pipe == 1)
		parsepaths(s, env, ins);
	scannewline(s);
	while (scanindent(s)) {
		name = scanname(s);
		if (strcmp(name, "restat") != 0)
			scanerror(s, "unexpected variable '%s' in dyndep file", name);
		if (seenrestat)
			scanerror(s, "duplicate restat binding in dyndep file");
		seenrestat = true;
		free(name);
		parselet(s, &str);
		val = enveval(env, str);
		/* Ninja treats any nonempty value as true, including "0". */
		restat = val->n != 0;
		free(val);
	}

	if (p->nentries == p->capentries) {
		p->capentries = p->capentries ? p->capentries * 2 : 8;
		p->entries = xreallocarray(p->entries, p->capentries, sizeof(p->entries[0]));
	}
	entry = &p->entries[p->nentries++];
	entry->edge = e;
	entry->outs = outs->node;
	entry->nouts = outs->len;
	entry->ins = ins->node;
	entry->nins = ins->len;
	entry->restat = restat;
	/* the entries own the path arrays now */
	resetpaths(outs);
	resetpaths(ins);
}

static void
freeentries(struct parse_state *p)
{
	size_t i;

	for (i = 0; i < p->nentries; ++i) {
		free(p->entries[i].outs);
		free(p->entries[i].ins);
	}
	free(p->entries);
	freepaths(&p->outs);
	freepaths(&p->ins);
	p->entries = NULL;
	p->nentries = p->capentries = 0;
}

static void
validateentries(struct scanner *s, struct parse_state *p, struct dyndep *d)
{
	struct dyndepentry *entry;
	struct node *n;
	size_t i, j, k, m;

	/* every edge that names this dyndep file must be described by it */
	for (i = 0; i < d->nuse; ++i) {
		for (j = 0; j < p->nentries; ++j)
			if (p->entries[j].edge == d->use[i])
				break;
		if (j == p->nentries)
			scanerror(s, "target '%s' not mentioned in dyndep file",
				d->use[i]->out[0]->path->s);
	}
	/* and it must not describe anything else */
	for (i = 0; i < p->nentries; ++i) {
		entry = &p->entries[i];
		for (j = 0; j < d->nuse; ++j)
			if (d->use[j] == entry->edge)
				break;
		if (j == d->nuse)
			scanerror(s, "dyndep file mentions target '%s', which does not name it",
				entry->edge->out[0]->path->s);
	}
	/* discovered outputs must not be produced by another edge */
	for (i = 0; i < p->nentries; ++i) {
		entry = &p->entries[i];
		for (j = 0; j < entry->nouts; ++j) {
			n = entry->outs[j];
			if (n->gen)
				scanerror(s, "multiple rules generate '%s'", n->path->s);
			for (k = 0; k < i; ++k)
				for (m = 0; m < p->entries[k].nouts; ++m)
					if (p->entries[k].outs[m] == n)
						scanerror(s, "multiple rules generate '%s'", n->path->s);
		}
	}
}

static bool
dyndepparse(const char *name, struct dyndep *d, char *err, size_t errlen)
{
	struct parse_state *p;
	struct environment *env;
	struct evalstring *str;
	struct string *val;
	volatile bool version = false, done = false;
	char *var;
	size_t i;

	p = xmalloc(sizeof(*p));
	p->entries = NULL;
	p->nentries = p->capentries = 0;
	p->outs.node = p->ins.node = NULL;
	scaninit(&p->scanner, name);
	p->scanner.errorjmp = &p->jmp;
	if (setjmp(p->jmp) != 0) {
		snprintf(err, errlen, "%s", p->scanner.error);
		scanclose(&p->scanner);
		freeentries(p);
		free(p);
		return false;
	}
	env = mkenv(NULL);
	for (;;) {
		switch (scankeyword(&p->scanner, &var)) {
		case BUILD:
			if (!version)
				scanerror(&p->scanner, "expected 'ninja_dyndep_version'");
			parseedge(&p->scanner, env, p);
			break;
		case VARIABLE:
			if (version || strcmp(var, "ninja_dyndep_version") != 0)
				scanerror(&p->scanner, "unexpected variable '%s'", var);
			parselet(&p->scanner, &str);
			val = enveval(env, str);
			if (!parseversion(val->s))
				scanerror(&p->scanner, "unsupported ninja_dyndep_version '%s'", val->s);
			envaddvar(env, var, val);
			version = true;
			break;
		case EOF:
			if (!version)
				scanerror(&p->scanner, "expected 'ninja_dyndep_version'");
			done = true;
			break;
		default:
			scanerror(&p->scanner, "unexpected keyword in dyndep file");
		}
		if (done)
			break;
	}
	validateentries(&p->scanner, p, d);
	scanclose(&p->scanner);
	for (i = 0; i < p->nentries; ++i) {
		struct dyndepentry *entry = &p->entries[i];
		if (entry->restat) {
			struct string *restat = mkstr(1);
			restat->s[0] = '1';
			restat->s[1] = '\0';
			envaddvar(entry->edge->env, "restat", restat);
		}
		if (entry->nouts)
			edgeadddynouts(entry->edge, entry->outs, entry->nouts);
		if (entry->nins)
			edgeadddyndeps(entry->edge, entry->ins, entry->nins);
	}
	freeentries(p);
	free(p);
	return true;
}

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

bool
dyndepload(struct dyndep *d, bool prune)
{
	struct node *n = d->node;
	struct edge *e;
	char err[512];
	size_t i;

	if (d->done)
		return true;
	if (n->mtime == MTIME_UNKNOWN)
		nodestat(n);
	/* a dyndep file that is about to be rebuilt must not be read yet */
	if (n->gen && !prune && !(n->gen->flags & FLAG_DONE) &&
	    (n->gen->flags & (FLAG_DIRTY | FLAG_QUEUED | FLAG_RUNNING)))
		return false;
	if (n->mtime == MTIME_MISSING) {
		if (prune)
			return false;
		/* the file has not been built yet */
		if (n->gen && !(n->gen->flags & FLAG_DONE))
			return false;
		if (n->gen)
			fatal("loading dyndep file '%s': %s", n->path->s, strerror(ENOENT));
		fatal("dyndep file is missing and not created by any action: '%s'", n->path->s);
	}
	if (buildopts.explain)
		warn("loading dyndep file: '%s'", n->path->s);
	if (!dyndepparse(n->path->s, d, err, sizeof(err))) {
		if (!prune)
			fatal("loading dyndep file '%s': %s", n->path->s, err);
		warn("could not load dyndep file '%s': %s", n->path->s, err);
		return false;
	}
	d->done = true;
	if (prune)
		return true;
	/* The graph changed: recompute every edge that uses this file. */
	for (i = 0; i < d->nuse; ++i) {
		e = d->use[i];
		if ((e->flags & FLAG_WORK) && !(e->flags & (FLAG_RUNNING | FLAG_DONE | FLAG_CYCLE)))
			buildupdate(e);
	}
	return true;
}
