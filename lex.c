#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"
#include "util.h"

struct file {
	const char *path;
	FILE *fp;
	int tok;
};

struct keyword {
	const char *name;
	int value;
};

struct file *lexfile;
char *lexident;
static struct buffer buf;

/* must stay in sorted order */
static struct keyword keywords[] = {
	{":",        COLON},
	{"=",        EQUALS},
	{"build",    BUILD},
	{"default",  DEFAULT},
	{"include",  INCLUDE},
	{"pool",     POOL},
	{"rule",     RULE},
	{"subninja", SUBNINJA},
	{"|",        PIPE},
	{"||",       PIPE2},
};

static const char *tokname[] = {
	[BUILD]    = "BUILD",
	[COLON]    = "COLON",
	[DEFAULT]  = "DEFAULT",
	[EQUALS]   = "EQUALS",
	[IDENT]    = "IDENT",
	[INCLUDE]  = "INCLUDE",
	[INDENT]   = "INDENT",
	[NEWLINE]  = "NEWLINE",
	[PIPE]     = "PIPE",
	[PIPE2]    = "PIPE2",
	[POOL]     = "POOL",
	[RULE]     = "RULE",
	[SUBNINJA] = "SUBNINJA",
	[PATH]     = "PATH",
};

struct file *
mkfile(const char *path)
{
	struct file *f;

	f = xmalloc(sizeof(*f));
	f->path = path;
	f->tok = 0;
	f->fp = fopen(path, "r");
	if (!f->fp)
		err(1, "fopen %s", path);

	return f;
}

void
fileclose(struct file *f)
{
	fclose(f->fp);
	free(f);
}

static void
bufadd(char c)
{
	char *newdata;
	size_t newcap;

	if (buf.len >= buf.cap) {
		newcap = buf.cap * 2 + 1;
		newdata = realloc(buf.data, newcap);
		if (!newdata)
			err(1, "realloc");
		buf.cap = newcap;
		buf.data = newdata;
	}
	buf.data[buf.len++] = c;
}

static int
isvar(int c)
{
	return isalnum(c) || strchr("_.-", c);
}

static void
token(void)
{
	FILE *f = lexfile->fp;
	int c;

	c = fgetc(f);
	switch (c) {
	case '#':  /* comment */
		do c = fgetc(f);
		while (c != '\n' && c != EOF);
		break;
	case '|':  /* check for || */
		c = fgetc(f);
		if (c == '|') {
			bufadd(c);
		} else {
			ungetc(c, f);
			c = '|';
		}
		break;
	}
	if (isvar(c)) {
		do {
			bufadd(c);
			c = fgetc(f);
		} while (isvar(c));
		ungetc(c, f);
	} else {
		bufadd(c);
	}
	bufadd('\0');
}

static int
keyword(const char *s)
{
	int low = 0, high = LEN(keywords) - 1, mid, cmp;

	while (low <= high) {
		mid = (low + high) / 2;
		cmp = strcmp(s, keywords[mid].name);
		if (cmp == 0)
			return keywords[mid].value;
		if (cmp < 0)
			high = mid - 1;
		else
			low = mid + 1;
	}

	return 0;
}

static int
issimplevar(int c)
{
	return isalnum(c) || strchr("_-", c);
}

static void
addstringpart(struct evalstringpart ***end, bool var)
{
	struct evalstringpart *p;

	p = xmalloc(sizeof(*p));
	p->next = NULL;
	**end = p;
	if (var) {
		p->var = xstrdup(buf.data, buf.len);
	} else {
		p->var = NULL;
		p->str = mkstr(buf.len);
		memcpy(p->str->s, buf.data, buf.len);
		p->str->s[buf.len] = '\0';
	}
	*end = &p->next;
	buf.len = 0;
}

static void
whitespace(void)
{
	FILE *f = lexfile->fp;
	int c;

	for (;;) {
		c = fgetc(f);
		switch (c) {
		case ' ':
			continue;
		default:
			goto done;
		}
	}
done:
	ungetc(c, f);
}

int
peek(void)
{
	int c, tok;

	if (lexfile->tok)
		return lexfile->tok;
	for (;;) {
		buf.len = 0;
		token();
		if (buf.len == 2) {
			c = buf.data[0];
			switch (c) {
			case ' ':
				tok = INDENT;
				goto out;
			case '=':
				tok = EQUALS;
				goto out;
			case '\n':
				tok = NEWLINE;
				goto out;
			case EOF:
				tok = EOF;
				goto out;
			}
		}
		tok = keyword(buf.data);
		if (tok)
			goto out;
		lexident = xstrdup(buf.data, buf.len - 1);
		tok = IDENT;
		break;
	}
out:
	if (tok != NEWLINE)
		whitespace();
	lexfile->tok = tok;

	return tok;
}

int
next(void)
{
	int t;

	t = peek();
	lexfile->tok = 0;

	return t;
}

void
expect(int tok)
{
	int t;

	t = next();
	if (t != tok)
		errx(1, "expected %s, saw %s", tokname[tok], tokname[t]);
}

const char *
tokstr(int t)
{
	static char s[256];

	switch (t) {
	case IDENT:
		snprintf(s, sizeof(s), "IDENT(%s)", lexident);
		return s;
	case EOF:
		return "EOF";
	default:
		return tokname[t];
	}
}

static void
escape(struct evalstringpart ***end)
{
	FILE *f = lexfile->fp;
	int c;

	c = fgetc(f);
	switch (c) {
	case '$':
	case ' ':
	case ':':
		bufadd(c);
		break;
	case '{':
		if (buf.len > 0)
			addstringpart(end, false);
		for (;;) {
			c = fgetc(f);
			if (!isvar(c))
				break;
		}
		if (c != '}')
			errx(1, "'%c' is not allowed in variable name", c);
		addstringpart(end, true);
		break;
	case '\n':
		whitespace();
		break;
	default:
		if (!issimplevar(c))
			errx(1, "bad $ escape: %c", c);
		if (buf.len > 0)
			addstringpart(end, false);
		do {
			bufadd(c);
			c = fgetc(f);
		} while (issimplevar(c));
		ungetc(c, f);
		addstringpart(end, true);
	}
}

struct evalstring *
readstr(bool path)
{
	FILE *f = lexfile->fp;
	struct evalstring *s;
	struct evalstringpart *parts = NULL, **end = &parts;
	int c;

	buf.len = 0;
	for (;;) {
		c = fgetc(f);
		switch (c) {
		case '$':
			escape(&end);
			break;
		case ':':
		case '|':
		case ' ':
			if (!path) {
				bufadd(c);
				break;
			}
			/* fallthrough */
		case '\n':
			ungetc(c, f);
			goto out;
		case EOF:
			goto out;
		default:
			bufadd(c);
		}
	}
out:
	if (buf.len > 0)
		addstringpart(&end, 0);
	if (path)
		whitespace();
	if (!parts)
		return NULL;
	s = xmalloc(sizeof(*s));
	s->parts = parts;

	return s;
}

void
delstr(struct evalstring *str)
{
	struct evalstringpart *p, *next;

	if (!str)
		return;
	for (p = str->parts; p; p = next) {
		next = p->next;
		if (p->var)
			free(p->var);
		else
			free(p->str);
		free(p);
	}
	free(str);
}

char *
readident(void)
{
	FILE *f = lexfile->fp;
	int c;

	buf.len = 0;
	for (;;) {
		c = fgetc(f);
		if (!isvar(c))
			break;
		bufadd(c);
	}
	ungetc(c, f);
	if (!buf.len)
		errx(1, "bad identifier");
	whitespace();

	return xstrdup(buf.data, buf.len);
}
