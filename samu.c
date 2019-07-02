#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* for chdir */
#include "arg.h"
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "parse.h"
#include "tool.h"
#include "util.h"

const char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-C dir] [-f buildfile] [-j maxjobs] [-k maxfail]\n", argv0);
	exit(2);
}

static char *
getbuilddir(void)
{
	struct string *builddir;

	builddir = envvar(rootenv, "builddir");
	if (!builddir)
		return NULL;
	if (makedirs(builddir, false) < 0)
		exit(1);
	return builddir->s;
}

static void
builddefault(void)
{
	struct edge *e;
	struct node *n;
	size_t i;

	if (ndeftarg > 0) {
		for (i = 0; i < ndeftarg; ++i)
			buildadd(deftarg[i]);
	} else {
		/* by default build all nodes which are not used by any edges */
		for (e = alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (n->nuse == 0)
					buildadd(n);
			}
		}
	}
}

static void
debugflag(const char *flag)
{
	if (strcmp(flag, "explain") == 0)
		buildopts.explain = true;
	else
		fatal("unknown debug flag '%s'", flag);
}

static void
warnflag(const char *flag)
{
	if (strcmp(flag, "dupbuild=err") == 0)
		parseopts.dupbuildwarn = false;
	else if (strcmp(flag, "dupbuild=warn") == 0)
		parseopts.dupbuildwarn = true;
	else
		fatal("unknown warning flag '%s'", flag);
}

static void
handle_maxjobs(long num)
{
	buildopts.maxjobs = num > 0 ? num : -1;
}		

static void
parseenvargs(char *s)
{
	char *copy;
	char *p;
	int argc;
	enum { maxArgs = 64 };
	char *env_argv[maxArgs];
	char **argv = &env_argv[0];
	char *arg, *end;
	long num;
		
	if (s == NULL) {
		return;
	}

	copy = xmemdup(s, strlen(s) + 1);
	
	argc = 1;
	argv[0] = NULL;
	
	p = strtok(copy, " ");
	while (p && argc < maxArgs - 1) {
		argv[argc++] = p;
		p = strtok(NULL, " ");
	}
	argv[argc] = NULL;

	ARGBEGIN {
	case '-':
		arg = EARGF(usage());
		if (strcmp(arg, "verbose") == 0) {
			buildopts.verbose = true;
		} else {
			fatal("invalid long argument in SAMUFLAGS");
		}
		break;
	case 'j':
		num = strtol(EARGF(usage()), &end, 10);
		if (*end || num < 0)
			fatal("invalid -j parameter in SAMUFLAGS");
		handle_maxjobs(num);
		break;
	case 'v':
		buildopts.verbose = true;
		break;
	default:
		fatal("invalid flags in SAMUFLAGS");
	} ARGEND
	
	free(copy);
}

static const char *
progname(const char *arg, const char *def)
{
	const char *slash;

	if (!arg)
		return def;
	slash = strrchr(arg, '/');
	return slash ? slash + 1 : arg;
}

int
main(int argc, char *argv[])
{
	char *builddir, *manifest = "build.ninja", *end, *arg;
	const struct tool *tool = NULL;
	struct node *n;
	long num;
	int tries;

	argv0 = progname(argv[0], "samu");
	parseenvargs(getenv("SAMUFLAGS"));
	ARGBEGIN {
	case '-':
		arg = EARGF(usage());
		if (strcmp(arg, "version") == 0) {
			puts(ninjaversion);
			return 0;
		} else if (strcmp(arg, "verbose") == 0) {
			buildopts.verbose = true;
		} else {
			usage();
		}
		break;
	case 'C':
		if (chdir(EARGF(usage())) < 0)
			fatal("chdir:");
		break;
	case 'd':
		debugflag(EARGF(usage()));
		break;
	case 'f':
		manifest = EARGF(usage());
		break;
	case 'j':
		num = strtol(EARGF(usage()), &end, 10);
		if (*end || num < 0)
			fatal("invalid -j parameter");
		handle_maxjobs(num);
		break;
	case 'k':
		num = strtol(EARGF(usage()), &end, 10);
		if (*end)
			fatal("invalid -k parameter");
		buildopts.maxfail = num > 0 ? num : -1;
		break;
	case 't':
		tool = toolget(EARGF(usage()));
		goto argdone;
	case 'v':
		buildopts.verbose = true;
		break;
	case 'w':
		warnflag(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND
argdone:
	if (!buildopts.maxjobs) {
#ifdef _SC_NPROCESSORS_ONLN
		int n = sysconf(_SC_NPROCESSORS_ONLN);
		switch (n) {
		case -1: case 0: case 1:
			buildopts.maxjobs = 2;
			break;
		case 2:
			buildopts.maxjobs = 3;
			break;
		default:
			buildopts.maxjobs = n + 2;
			break;
		}
#else
		buildopts.maxjobs = 2;
#endif
	}

	tries = 0;
retry:
	/* (re-)initialize global graph, environment, and parse structures */
	graphinit();
	envinit();
	parseinit();

	/* parse the manifest */
	parse(manifest, rootenv);

	if (tool)
		return tool->run(argc, argv);

	/* load the build log */
	builddir = getbuilddir();
	loginit(builddir);
	depsinit(builddir);

	/* rebuild the manifest if it's dirty */
	n = nodeget(manifest, 0);
	if (n && n->gen) {
		buildadd(n);
		if (n->dirty) {
			build();
			if (++tries > 100)
				fatal("manifest '%s' dirty after 100 tries", manifest);
			goto retry;
		}
	}

	/* finally, build any specified targets or the default targets */
	if (argc) {
		for (; *argv; ++argv) {
			n = nodeget(*argv, 0);
			if (!n)
				fatal("unknown target '%s'", *argv);
			buildadd(n);
		}
	} else {
		builddefault();
	}
	build();
	logclose();
	depsclose();

	return 0;
}
