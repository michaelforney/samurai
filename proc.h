union process {
	void *ptr;
	pid_t pid;
};

int procspawn(union process *, char *, int[], _Bool);
void prockill(union process);
int procwait(union process, int *);
