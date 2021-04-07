#include <stdbool.h>
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
	struct environment *allnext;
};

struct environment *rootenv;
struct rule phonyrule = {.name = "phony"};
struct pool consolepool = {.name = "console", .maxjobs = 1};
static struct treenode *pools;
static struct environment *allenvs;

static void addpool(struct pool *);
static void delpool(void *);
static void delrule(void *);

void
envinit(void)
{
	struct environment *env;

	/* free old environments and pools in case we rebuilt the manifest */
	while (allenvs) {
		env = allenvs;
		allenvs = env->allnext;
		deltree(env->bindings, free, free);
		deltree(env->rules, NULL, delrule);
		free(env);
	}
	deltree(pools, NULL, delpool);

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
	env->allnext = allenvs;
	allenvs = env;

	return env;
}

struct string *
envvar(struct environment *env, char *var)
{
	struct treenode *n;

	do {
		n = treefind(env->bindings, var);
		if (n)
			return n->value;
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
	struct evalstring *p;
	char *s;

	result = mkstr(n);
	s = result->s;
	for (p = str; p; p = p->next) {
		if (!p->str)
			continue;
		memcpy(s, p->str->s, p->str->n);
		s += p->str->n;
	}
	*s = '\0';

	return result;
}

struct string *
enveval(struct environment *env, struct evalstring *str)
{
	size_t n;
	struct evalstring *p;
	struct string *res;

	n = 0;
	for (p = str; p; p = p->next) {
		if (p->var)
			p->str = envvar(env, p->var);
		if (p->str)
			n += p->str->n;
	}
	res = merge(str, n);
	delevalstr(str);

	return res;
}

void
envaddrule(struct environment *env, struct rule *r)
{
	if (treeinsert(&env->rules, r->name, r))
		fatal("rule '%s' redefined", r->name);
}

struct rule *
envrule(struct environment *env, char *name)
{
	struct treenode *n;

	do {
		n = treefind(env->rules, name);
		if (n)
			return n->value;
		env = env->parent;
	} while (env);

	return NULL;
}

static struct string *
pathlist(struct node **nodes, size_t n, char sep, bool escape)
{
	size_t i, len;
	struct string *path, *result;
	char *s;

	if (n == 0)
		return NULL;
	if (n == 1)
		return nodepath(nodes[0], escape);
	for (i = 0, len = 0; i < n; ++i)
		len += nodepath(nodes[i], escape)->n;
	result = mkstr(len + n - 1);
	s = result->s;
	for (i = 0; i < n; ++i) {
		path = nodepath(nodes[i], escape);
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

static void
delrule(void *ptr)
{
	struct rule *r = ptr;

	if (r == &phonyrule)
		return;
	deltree(r->bindings, free, delevalstr);
	free(r->name);
	free(r);
}

void
ruleaddvar(struct rule *r, char *var, struct evalstring *val)
{
	addvar(&r->bindings, var, val);
}

struct string *
edgevar(struct edge *e, char *var, bool escape)
{
	static void *const cycle = (void *)&cycle;
	struct evalstring *str, *p;
	struct treenode *n;
	size_t len;

	if (strcmp(var, "in") == 0)
		return pathlist(e->in, e->inimpidx, ' ', escape);
	if (strcmp(var, "in_newline") == 0)
		return pathlist(e->in, e->inimpidx, '\n', escape);
	if (strcmp(var, "out") == 0)
		return pathlist(e->out, e->outimpidx, ' ', escape);
	n = treefind(e->env->bindings, var);
	if (n)
		return n->value;
	n = treefind(e->rule->bindings, var);
	if (!n)
		return envvar(e->env->parent, var);
	if (n->value == cycle)
		fatal("cycle in rule variable involving '%s'", var);
	str = n->value;
	n->value = cycle;
	len = 0;
	for (p = str; p; p = p->next) {
		if (p->var)
			p->str = edgevar(e, p->var, escape);
		if (p->str)
			len += p->str->n;
	}
	n->value = str;
	return merge(str, len);
}

static void
addpool(struct pool *p)
{
	if (treeinsert(&pools, p->name, p))
		fatal("pool '%s' redefined", p->name);
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

static void
delpool(void *ptr)
{
	struct pool *p = ptr;

	if (p == &consolepool)
		return;
	free(p->name);
	free(p);
}

struct pool *
poolget(char *name)
{
	struct treenode *n;

	n = treefind(pools, name);
	if (!n)
		fatal("unknown pool '%s'", name);

	return n->value;
}
