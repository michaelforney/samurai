/* an unevaluated string */
struct evalstring {
	struct evalstringpart *parts;
	/* used temporarily only in parse.c:parseedge to keep track of
	 * input/output lists before we allocate the arrays. */
	struct evalstring *next;
};

struct evalstringpart {
	char *var;
	struct string *str;
	struct evalstringpart *next;
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
	POOL,
	RULE,
	SUBNINJA,
	PATH,
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
struct evalstring *readstr(bool ispath);
/* delete a string token */
void delstr(struct evalstring *);
/* read an identifier token */
char *readident(void);
