/* an unevaluated string */
struct string {
	struct stringpart *parts;
	/* used temporarily only in parse.c:parseedge to keep track of
	 * input/output lists before we allocate the arrays. */
	struct string *next;
};

struct stringpart {
	char *var, *str;
	size_t len;
	struct stringpart *next;
};

enum token {
	BUILD = 1,
	COLON,
	DEFAULT,
	EQUALS,
	IDENT,
	INCLUDE,
	INDENT,
	NEWLINE,
	PIPE,
	PIPE2,
	RULE,
	SUBNINJA,
	PATH,
	VALUE,
};

/* identifier name for IDENT token. must be freed by parser. */
extern char *ident;

/* return the next token without consuming it */
int peek(void);
/* consume and return the next token */
int next(void);
/* consume the next token and error if it does not match argument */
void expect(int);
/* string representation of a token */
const char *tokstr(int);

/* read a string token */
struct string *readstr(bool ispath);
/* delete a string token */
void delstr(struct string *);
