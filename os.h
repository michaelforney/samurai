#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

struct ostimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

#ifdef _WIN32

    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    typedef void* HANDLE;
    typedef long long ssize_t;

    #define setvbuf(stream, buff, mode, len) (void)0


    struct osjob {
	    bool has_data;
	    bool valid;

	    OVERLAPPED overlapped;
	    HANDLE output;
	    HANDLE hProcess;

        DWORD to_read;

        char buff[4096];
    };

#else
    #include "poll.h"
    struct osjob {
        bool has_data;
        bool valid;

        int pid;
        int fd;
    };
#endif

struct buffer;
struct string;

void osgetcwd(char *, size_t);
/* changes the working directory to the given path */
void oschdir(const char *);
/* creates all the parent directories of the given path */
int osmkdirs(struct string *, bool);
/* queries the mtime of a file in nanoseconds since the UNIX epoch */
int64_t osmtime(const char *);
/* get current monotonic time */
int osclock_gettime_monotonic(struct ostimespec*);

// os-specific job functions

struct osjob_ctx;

struct osjob_ctx* osjob_ctx_create();
void osjob_ctx_close(struct osjob_ctx* ctx);
int osjob_create(struct osjob_ctx *ctx, struct osjob *created, struct string *cmd, bool console);
int osjob_wait(struct osjob_ctx *ctx, struct osjob ojobs[], size_t jobs_count, int timeout);
ssize_t osjob_work(struct osjob_ctx *ctx, struct osjob *ojob, void *buf, size_t buflen);
int osjob_done(struct osjob_ctx *ctx, struct osjob *ojob, struct string *cmd);
int osjob_close(struct osjob_ctx *ctx, struct osjob *ojob);
