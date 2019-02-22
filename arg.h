extern char *argv0;

#define ARGBEGIN \
	for (;;) { \
		++argv, --argc; \
		if (argc == 0 || (*argv)[0] != '-') \
			break; \
		if ((*argv)[1] == '-' && !(*argv)[2]) { \
			++argv, --argc; \
			break; \
		} \
		for (char *opt_ = &(*argv)[1], done_ = 0; !done_ && *opt_; ++opt_) { \
			switch (*opt_)

#define ARGEND \
		} \
	}

#define EARGF(x) \
	(done_ = 1, *++opt_ ? opt_ : argv[1] ? --argc, *++argv : ((x), abort(), NULL))
