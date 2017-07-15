#include <stdbool.h>
#include <stdio.h>
#include <search.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <err.h>
#include "arg.h"
#include "build.h"
#include "env.h"
#include "graph.h"
#include "parse.h"

#define HTAB_NEL 8192

FILE *f;
char *argv0;

static _Noreturn void
usage(void)
{
	fprintf(stderr, "usage: %s [-C dir] [-f buildfile] [-j numjobs]\n", argv0);
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct environment *env;
	struct edge *e;
	struct node *n;
	const char *buildname = "build.ninja";
	int numjobs = 0;
	size_t i;

	ARGBEGIN {
	case 'C':
		if (chdir(EARGF(usage())) < 0)
			err(1, "chdir");
		break;
	case 'f':
		buildname = EARGF(usage());
		break;
	case 'j':
		numjobs = strtol(EARGF(usage()), 0, 10);
		if (numjobs <= 0)
			errx(1, "invalid -j parameter");
		break;
	default:
		usage();
	} ARGEND

	if (!numjobs) {
#ifdef _SC_NPROCESSORS_ONLN
		int n = sysconf(_SC_NPROCESSORS_ONLN);
		switch (n) {
		case -1: case 0: case 1:
			numjobs = 2;
			break;
		case 2:
			numjobs = 3;
			break;
		default:
			numjobs = n + 2;
			break;
		}
#else
		numjobs = 2;
#endif
	}

	if (!hcreate(HTAB_NEL))
		err(1, "hcreate");
	f = fopen(buildname, "r");
	if (!f)
		err(1, "fopen %s", buildname);
	env = mkenv(NULL);
	parse(env);
	fclose(f);

	if (argc) {
		for (; *argv; ++argv) {
			n = nodeget(*argv, false);
			if (!n)
				errx(1, "unknown target: '%s'", *argv);
			addtarget(n);
		}
	} else {
		/* by default build all nodes which are not used by any edges */
		for (e = alledges; e; e = e->next) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (n->nuse == 0)
					addtarget(n);
			}
		}
	}
	build(numjobs);

	return 0;
}
