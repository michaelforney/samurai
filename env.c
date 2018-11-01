#include <stdlib.h>
#include <string.h>
#include "env.h"
#include "graph.h"
#include "tree.h"
#include "util.h"

struct environment {
	struct environment *parent;
	struct treenode *bindings;
	struct treenode *rules;
};

struct environment *rootenv;
struct rule phonyrule = {.name = "phony"};
struct pool consolepool = {.name = "console", .maxjobs = 1};
static struct treenode *pools;

static void addpool(struct pool *);

void
envinit(void)
{
	/* TODO: delete old root environment and pools (in case we rebuilt
	 * build.ninja). for now, we leak memory. */
	rootenv = mkenv(NULL);
	envaddrule(rootenv, &phonyrule);
	pools = NULL;
	addpool(&consolepool);
}

static void
addvar(struct treenode **tree, char *var, void *val)
{
	char *old;

	old = treeinsert(tree, var, val);
	if (old)
		free(old);
}

struct environment *
mkenv(struct environment *parent)
{
	struct environment *env;

	env = xmalloc(sizeof(*env));
	env->parent = parent;
	env->bindings = NULL;
	env->rules = NULL;

	return env;
}

struct string *
envvar(struct environment *env, char *var)
{
	struct string *s;

	do {
		s = treefind(env->bindings, var);
		if (s)
			return s;
		env = env->parent;
	} while (env);

	return NULL;
}

void
envaddvar(struct environment *env, char *var, struct string *val)
{
	addvar(&env->bindings, var, val);
}

static struct string *
merge(struct evalstring *str, size_t n)
{
	struct string *result;
	struct evalstringpart *p;
	char *s;

	result = mkstr(n);
	s = result->s;
	if (str) {
		for (p = str->parts; p; p = p->next) {
			if (!p->str)
				continue;
			memcpy(s, p->str->s, p->str->n);
			s += p->str->n;
		}
	}
	*s = '\0';

	return result;
}

struct string *
enveval(struct environment *env, struct evalstring *str)
{
	size_t n;
	struct evalstringpart *p;

	n = 0;
	if (str) {
		for (p = str->parts; p; p = p->next) {
			if (p->var)
				p->str = envvar(env, p->var);
			if (p->str)
				n += p->str->n;
		}
	}

	return merge(str, n);
}

void
envaddrule(struct environment *env, struct rule *r)
{
	if (treeinsert(&env->rules, r->name, r))
		errx(1, "rule %s already defined", r->name);
}

struct rule *
envrule(struct environment *env, char *name)
{
	struct rule *r;

	do {
		r = treefind(env->rules, name);
		if (r)
			return r;
		env = env->parent;
	} while (env);

	return NULL;
}

static struct string *
pathlist(struct node **nodes, size_t n, char sep)
{
	size_t i, len;
	struct string *path, *result;
	char *s;

	if (n == 0)
		return NULL;
	for (i = 0, len = 0; i < n; ++i) {
		nodeescape(nodes[i]);
		len += nodes[i]->shellpath->n;
	}
	result = mkstr(len + n - 1);
	s = result->s;
	for (i = 0; i < n; ++i) {
		path = nodes[i]->shellpath;
		memcpy(s, path->s, path->n);
		s += path->n;
		*s++ = sep;
	}
	*--s = '\0';

	return result;
}

struct rule *
mkrule(char *name)
{
	struct rule *r;

	r = xmalloc(sizeof(*r));
	r->name = name;
	r->bindings = NULL;

	return r;
}

void
ruleaddvar(struct rule *r, char *var, struct evalstring *val)
{
	addvar(&r->bindings, var, val);
}

struct string *
edgevar(struct edge *e, char *var)
{
	struct string *val;
	struct evalstring *str;
	struct evalstringpart *p;
	size_t n;

	val = treefind(e->env->bindings, var);
	if (val)
		return val;
	if (strcmp(var, "in") == 0) {
		val = pathlist(e->in, e->inimpidx, ' ');
	} else if (strcmp(var, "in_newline") == 0) {
		val = pathlist(e->in, e->inimpidx, '\n');
	} else if (strcmp(var, "out") == 0) {
		val = pathlist(e->out, e->outimpidx, ' ');
	} else {
		val = envvar(e->env->parent, var);
		if (val)
			return val;
		str = treefind(e->rule->bindings, var);
		if (!str)
			return NULL;
		n = 0;
		for (p = str->parts; p; p = p->next) {
			if (p->var)
				p->str = edgevar(e, p->var);
			if (p->str)
				n += p->str->n;
		}
		val = merge(str, n);
	}
	envaddvar(e->env, var, val);

	return val;
}

static void
addpool(struct pool *p)
{
	if (treeinsert(&pools, p->name, p))
		errx(1, "pool redefined: %s", p->name);
}

struct pool *
mkpool(char *name)
{
	struct pool *p;

	p = xmalloc(sizeof(*p));
	p->name = name;
	p->numjobs = 0;
	p->maxjobs = 0;
	p->work = NULL;
	addpool(p);

	return p;
}

struct pool *
poolget(char *name)
{
	struct pool *p;

	p = treefind(pools, name);
	if (!p)
		errx(1, "unknown pool: %s", name);

	return p;
}
