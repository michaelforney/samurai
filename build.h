#pragma once

#include "util.h"
#include "platform.h"

struct node;

struct job {
	struct string *cmd;
	struct edge *edge;
	struct buffer buf;
	struct platformprocess process;
	bool failed;
};

struct buildoptions {
	size_t maxjobs, maxfail;
	_Bool verbose, explain;
};

extern struct buildoptions buildopts;

/* schedule a particular target to be built */
void buildadd(struct node *);
/* execute rules to build the scheduled targets */
void build(void);
