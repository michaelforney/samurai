#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <err.h>
#include "arg.h"
#include "build.h"
#include "env.h"
#include "graph.h"
#include "parse.h"
#include "tool.h"

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
	struct edge *e;
	struct node *n;
	char *manifest = "build.ninja";
	int numjobs = 0, tries = 0;
	struct tool *tool = NULL;
	size_t i;

	argv0 = argv[0];
	ARGBEGIN {
	case 'C':
		if (chdir(EARGF(usage())) < 0)
			err(1, "chdir");
		break;
	case 'f':
		manifest = EARGF(usage());
		break;
	case 'j':
		numjobs = strtol(EARGF(usage()), 0, 10);
		if (numjobs <= 0)
			errx(1, "invalid -j parameter");
		break;
	case 't':
		tool = toolget(EARGF(usage()));
		goto argdone;
	default:
		usage();
	} ARGEND
argdone:

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

retry:
	graphinit();
	envinit();
	parseinit();
	f = fopen(manifest, "r");
	if (!f)
		err(1, "fopen %s", manifest);
	parse(rootenv);
	fclose(f);

	if (tool)
		return tool->run(argc, argv);

	n = nodeget(manifest);
	if (n && n->gen) {
		addtarget(n);
		if (n->dirty) {
			build(numjobs);
			if (++tries > 100)
				errx(1, "manifest '%s' dirty after 100 tries", manifest);
			goto retry;
		}
	}
	if (argc) {
		for (; *argv; ++argv) {
			n = nodeget(*argv);
			if (!n)
				errx(1, "unknown target: '%s'", *argv);
			addtarget(n);
		}
	} else {
		if (ndeftarg) {
			for (i = 0; i < ndeftarg; ++i)
				addtarget(deftarg[i]);
		} else {
			/* by default build all nodes which are not used by any edges */
			for (e = alledges; e; e = e->allnext) {
				for (i = 0; i < e->nout; ++i) {
					n = e->out[i];
					if (n->nuse == 0)
						addtarget(n);
				}
			}
		}
	}
	build(numjobs);

	return 0;
}
