#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lex.h"
#include "util.h"

struct file {
	const char *path;
	int fd;
	char buf[8<<10], *p, *end;
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
	{"build",    BUILD},
	{"default",  DEFAULT},
	{"include",  INCLUDE},
	{"pool",     POOL},
	{"rule",     RULE},
	{"subninja", SUBNINJA},
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
	f->p = f->buf;
	f->end = f->buf;
	f->fd = open(path, O_RDONLY);
	if (f->fd < 0)
		err(1, "open %s", path);

	return f;
}

void
fileclose(struct file *f)
{
	close(f->fd);
	free(f);
}

static void
fileread(struct file *f)
{
	ssize_t n;

	n = read(f->fd, f->buf, sizeof(f->buf));
	if (n < 0)
		err(1, "read");
	f->p = f->buf;
	f->end = f->buf + n;
}

static inline int
filepeek(struct file *f)
{
	if (f->p == f->end)
		fileread(f);
	return f->p < f->end ? *f->p : EOF;
}

static inline int
fileget(struct file *f)
{
	if (f->p == f->end)
		fileread(f);
	return f->p < f->end ? *f->p++ : EOF;
}

static int
isvar(int c)
{
	return isalnum(c) || strchr("_.-", c);
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
	while (filepeek(lexfile) == ' ')
		++lexfile->p;
}

int
next(void)
{
	struct file *f = lexfile;
	int c, t;

	if (f->tok) {
		t = f->tok;
		f->tok = 0;
		return t;
	}
	c = fileget(f);
peek:
	switch (c) {
	case '#':  /* comment */
		do c = fileget(f);
		while (c != '\n' && c != EOF);
		goto peek;
	case '|':
		if (filepeek(f) == '|') {
			++f->p;
			t = PIPE2;
		} else {
			t = PIPE;
		}
		break;
	case '\n':
		t = NEWLINE;
		break;
	case '=':
		t = EQUALS;
		break;
	case ':':
		t = COLON;
		break;
	case ' ':
		t = INDENT;
		while (filepeek(f) == ' ')
			++f->p;
		break;
	case EOF:
		t = EOF;
		break;
	default:
		if (!isvar(c))
			errx(1, "invalid character: %d", c);
		bufadd(&buf, c);
		while (isvar(filepeek(f)))
			bufadd(&buf, *f->p++);
		bufadd(&buf, '\0');
		t = keyword(buf.data);
		if (!t) {
			t = IDENT;
			lexident = xstrdup(buf.data, buf.len - 1);
		}
		break;
	}
	if (t == EOF)
		f->tok = EOF;
	else if (t != NEWLINE)
		whitespace();

	return t;
}

int
peek(void)
{
	return lexfile->tok = next();
}

void
expect(int tok)
{
	int t;

	t = next();
	if (t != tok)
		errx(1, "expected %s, saw %s", tokname[tok], tokstr(t));
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
	struct file *f = lexfile;
	int c;

	c = fileget(f);
	switch (c) {
	case '$':
	case ' ':
	case ':':
		bufadd(&buf, c);
		break;
	case '{':
		if (buf.len > 0)
			addstringpart(end, false);
		for (;;) {
			c = fileget(f);
			if (!isvar(c))
				break;
			bufadd(&buf, c);
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
		bufadd(&buf, c);
		while (issimplevar(filepeek(f)))
			bufadd(&buf, *f->p++);
		addstringpart(end, true);
	}
}

struct evalstring *
readstr(bool path)
{
	struct file *f = lexfile;
	struct evalstring *s;
	struct evalstringpart *parts = NULL, **end = &parts;

	buf.len = 0;
	for (;;) {
		switch (filepeek(f)) {
		case '$':
			++f->p;
			escape(&end);
			break;
		case ':':
		case '|':
		case ' ':
			if (!path) {
				bufadd(&buf, *f->p++);
				break;
			}
			goto out;
		case '\n':
		case EOF:
			goto out;
		default:
			bufadd(&buf, *f->p++);
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
	buf.len = 0;
	while (isvar(filepeek(lexfile)))
		bufadd(&buf, *lexfile->p++);
	if (!buf.len)
		errx(1, "bad identifier");
	whitespace();

	return xstrdup(buf.data, buf.len);
}
