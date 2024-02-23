#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* for chdir */
#include <poll.h>   /* for poll  */
#include <fcntl.h>  /* for open, fcntl */
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
	fprintf(stderr, "usage: %s [-C dir] [-f buildfile] [-j maxjobs] [-k maxfail] [-l maxload] [-n]\n", argv0);
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
debugflag(const char *flag)
{
	if (strcmp(flag, "explain") == 0)
		buildopts.explain = true;
	else if (strcmp(flag, "keepdepfile") == 0)
		buildopts.keepdepfile = true;
	else if (strcmp(flag, "keeprsp") == 0)
		buildopts.keeprsp = true;
	else
		fatal("unknown debug flag '%s'", flag);
}

static void
loadflag(const char *flag)
{
#ifdef HAVE_GETLOADAVG
	double value;
	char *end;
	errno = 0;

	value = strtod(flag, &end);
	if (*end || value < 0 || errno != 0)
		fatal("invalid -l parameter");
	buildopts.maxload = value;
#else
	warn("job scheduling based on load average is not supported");
#endif
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
jobsflag(const char *flag)
{
	long num;
	char *end;

	num = strtol(flag, &end, 10);
	if (*end || num < 0)
		fatal("invalid -j parameter");
	buildopts.maxjobs = num > 0 ? num : -1;
}

static void
parsegmakeflags(const char *flag)
{
	char authbuf[1024];
	const char *auth, *end;
	size_t authlen;
	int rfd, wfd;

	if (!flag)
		return;
	for (; *flag != ' '; ++flag) {
		switch (*flag) {
		case 'n':
			buildopts.dryrun = true;
			break;
		case '\0':
			return;
		}
	}
	while (flag) {
		++flag;
		end = strchr(flag, ' ');
		if (strncmp(flag, "--jobserver-auth=", 17) == 0) {
			auth = flag + 17;
			authlen = end ? end - auth : strlen(auth);
		}
		flag = end;
	}
	if (authlen >= sizeof(authbuf)) {
		warn("jobserver: MAKEFLAGS option is too long; ignoring");
		return;
	}
	memcpy(authbuf, auth, authlen);
	authbuf[authlen] = 0;
	auth = authbuf;

	if (strncmp(auth, "fifo:", 5) == 0) {
		auth += 5;
		rfd = wfd = open(auth, O_RDONLY | O_CLOEXEC);
		if (rfd < 0) {
			warn("jobserver: open %s:", auth);
			return;
		}
	} else if (sscanf(auth, "%d,%d", &rfd, &wfd) == 2) {
		if (rfd < 0 || wfd < 0)
			return;  /* jobserver is disabled */
		if (fcntl(rfd, F_SETFD, FD_CLOEXEC) != 0 || fcntl(wfd, F_SETFD, FD_CLOEXEC) != 0) {
			warn("jobserver: fcntl set FD_CLOEXEC:");
			return;
		}
	} else {
		warn("jobserver: MAKEFLAGS option has unrecognized format; ignoring");
		return;
	}
	buildops.jobserver[0] = rfd;
	buildops.jobserver[1] = wfd;
}

static void
parseenvargs(char *env)
{
	char *arg, *argvbuf[64], **argv = argvbuf;
	int argc;

	if (!env)
		return;
	env = xmemdup(env, strlen(env) + 1);
	argc = 1;
	argv[0] = NULL;
	arg = strtok(env, " ");
	while (arg) {
		if ((size_t)argc >= LEN(argvbuf) - 1)
			fatal("too many arguments in SAMUFLAGS");
		argv[argc++] = arg;
		arg = strtok(NULL, " ");
	}
	argv[argc] = NULL;

	ARGBEGIN {
	case 'j':
		jobsflag(EARGF(usage()));
		break;
	case 'v':
		buildopts.verbose = true;
		break;
	case 'l':
		loadflag(EARGF(usage()));
		break;
	default:
		fatal("invalid option in SAMUFLAGS");
	} ARGEND

	free(env);
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
	parsegmakeflags(getenv("MAKEFLAGS"));
	ARGBEGIN {
	case '-':
		arg = EARGF(usage());
		if (strcmp(arg, "version") == 0) {
			printf("%d.%d.0\n", ninjamajor, ninjaminor);
			return 0;
		} else if (strcmp(arg, "verbose") == 0) {
			buildopts.verbose = true;
		} else {
			usage();
		}
		break;
	case 'C':
		arg = EARGF(usage());
		warn("entering directory '%s'", arg);
		if (chdir(arg) < 0)
			fatal("chdir:");
		break;
	case 'd':
		debugflag(EARGF(usage()));
		break;
	case 'f':
		manifest = EARGF(usage());
		break;
	case 'j':
		jobsflag(EARGF(usage()));
		break;
	case 'k':
		num = strtol(EARGF(usage()), &end, 10);
		if (*end)
			fatal("invalid -k parameter");
		buildopts.maxfail = num > 0 ? num : -1;
		break;
	case 'l':
		loadflag(EARGF(usage()));
		break;
	case 'n':
		buildopts.dryrun = true;
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
	if (buildopts.gmakepipe[0] >= 0) {
		if (buildopts.maxjobs)
			warn("ignoring -j setting as GNU Make job client is enabled");
		if (buildopts.maxload)
			warn("ignoring -l setting as GNU Make job client is enabled");
		buildopts.maxjobs = -1;
		buildopts.maxload = 0;
	} else if (!buildopts.maxjobs) {
#ifdef _SC_NPROCESSORS_ONLN
		int nproc = sysconf(_SC_NPROCESSORS_ONLN);
		switch (nproc) {
		case -1: case 0: case 1:
			buildopts.maxjobs = 2;
			break;
		case 2:
			buildopts.maxjobs = 3;
			break;
		default:
			buildopts.maxjobs = nproc + 2;
			break;
		}
#else
		buildopts.maxjobs = 2;
#endif
	}

	buildopts.statusfmt = getenv("NINJA_STATUS");
	if (!buildopts.statusfmt)
		buildopts.statusfmt = "[%s/%t] ";

	setvbuf(stdout, NULL, _IOLBF, 0);

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
			if (n->gen->flags & FLAG_DIRTY_OUT || n->gen->nprune > 0) {
				if (++tries > 100)
					fatal("manifest '%s' dirty after 100 tries", manifest);
				if (!buildopts.dryrun)
					goto retry;
			}
			/* manifest was pruned; reset state, then continue with build */
			buildreset();
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
		defaultnodes(buildadd);
	}
	build();
	logclose();
	depsclose();

	return 0;
}
