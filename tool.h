struct tool {
	const char *name;
	int (*run)(int, char *[]);
};

struct tool *toolget(const char *);
