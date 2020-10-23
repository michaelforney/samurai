struct tool {
	const char *name;
	const char *description;
	int (*run)(int, char *[]);
};

const struct tool *toolget(const char *);
