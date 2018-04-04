struct tool {
	const char *name;
	int (*run)(int, char *[]);
};

const struct tool *toolget(const char *);
