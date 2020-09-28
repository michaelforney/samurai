#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "util.h"

int
isdumb(void)
{
	const char *term;

	term = getenv("TERM");
	return !isatty(1) || !term || !strcmp(term, "dumb");
}

void
printdesc(bool dumb, size_t statuslen, struct string *description)
{
	struct winsize ws;

	if (dumb) {
		puts(description->s);
		return;
	}

	ioctl(1, TIOCGWINSZ, &ws);
	if (statuslen + description->n > ws.ws_col) {
		switch (ws.ws_col) {
			case 3: putchar('.');  /* fallthrough */
			case 2: putchar('.');  /* fallthrough */
			case 1: putchar('.');  /* fallthrough */
			case 0: break;
			default: printf("%.*s...%s",
					(ws.ws_col - 3) / 2 - (int)statuslen, description->s,
					description->s + description->n - (ws.ws_col - 3) / 2);
		}
	} else {
		fputs(description->s, stdout);
	}
	printf("\033[K");
	fflush(stdout);
}
