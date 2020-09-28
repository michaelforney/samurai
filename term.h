#include <stdbool.h>

struct string;

/* determine if terminal is dumb or not */
int isdumb(void);

/* print description */
void printdesc(bool dumb, size_t statuslen, struct string *description);
