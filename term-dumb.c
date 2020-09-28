#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

int
isdumb(void)
{
	return 1;
}

void
printdesc(bool dumb, size_t statuslen, struct string *description)
{
	/* don't warn for unused parameters */
	(void)dumb;
	(void)statuslen;
	puts(description->s);
}
