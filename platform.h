#pragma once

#if defined( _WIN32 )
#  define PLATFORM_WINDOWS 1
#elif defined( __unix__ )
#  define _POSIX_C_SOURCE 200809L
#  define PLATFORM_UNIX 1
#else
#  error new platform
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct platformprocess {
	uint64_t impl[8];
};

struct job;
struct string;

void initplatform(size_t maxjobs);
void shutdownplatform();

bool createprocess(struct string *command, struct platformprocess *p, bool captureoutput);
bool readprocessoutput(struct platformprocess p, char *buf, size_t buflen, size_t *n);

bool waitexit(struct job *j);
void killprocess(struct platformprocess p);

size_t waitforjobs(const struct job *jobs, size_t n);
