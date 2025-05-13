#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

struct ostimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

#ifdef _WIN32
    typedef void* HANDLE;
    typedef HANDLE fd_t;
    typedef HANDLE pid_t;
    typedef long long ssize_t;


    #define WNOHANG 1

    #define WEXITSTATUS(status)   status
    #define WTERMSIG(status)      0
    #define WSTOPSIG(status)      WEXITSTATUS(status)
    #define WIFEXITED(status)     (WTERMSIG(status) == 0)
    #define WIFSIGNALED(status)   0

    #define setvbuf(stream, buff, mode, len) (void)0
#else
    #include "poll.h"
    struct osjob {
        bool has_data;
        bool valid;
        int pid;
        int fd;
    };
    struct osjob_ctx {
        struct pollfd* pfds;
        size_t pfds_len;
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

struct osjob_ctx;

int osjob_create(struct osjob *created, struct string *cmd, bool console);
/*ojobs is array of osjob*, entries may be NULL (invalid osjob).*/
int osjob_wait(struct osjob_ctx *ctx, struct osjob *ojobs, size_t jobs_count, int timeout);
/*read out into buffer*/
ssize_t osjob_work(struct osjob *ojob, void* buf, size_t buflen);
int osjob_close(struct osjob* ojob);
int osjob_done(struct osjob* ojob, struct string* cmd);
