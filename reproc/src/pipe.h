#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
typedef uint64_t pipe_type; // `SOCKET`
#else
typedef int pipe_type; // fd
#endif

// Keep in sync with `REPROC_EVENT`.
enum {
  PIPE_EVENT_IN = 1 << 0,
  PIPE_EVENT_OUT = 1 << 1,
  PIPE_EVENT_ERR = 1 << 2,
  PIPE_EVENT_EXIT = 1 << 3
};

typedef struct {
  pipe_type in;
  pipe_type out;
  pipe_type err;
  pipe_type exit;
  int events;
} pipe_set;

enum { PIPES_PER_SET = 4 };

extern const pipe_type PIPE_INVALID;

// Creates a new anonymous pipe. `parent` and `child` are set to the parent and
// child endpoint of the pipe respectively.
int pipe_init(pipe_type *read, pipe_type *write);

// Sets `pipe` to nonblocking mode.
int pipe_nonblocking(pipe_type pipe, bool enable);

// Reads up to `size` bytes into `buffer` from the pipe indicated by `pipe` and
// returns the amount of bytes read.
int pipe_read(pipe_type pipe, uint8_t *buffer, size_t size);

// Writes up to `size` bytes from `buffer` to the pipe indicated by `pipe` and
// returns the amount of bytes written.
int pipe_write(pipe_type pipe, const uint8_t *buffer, size_t size);

// Returns the first stream of `in`, `out` and `err` that has data available to
// read. 0 => in, 1 => out, 2 => err.
//
// Returns `REPROC_EPIPE` if `in`, `out` and `err` are invalid.
int pipe_wait(pipe_set *sets, size_t num_sets, int timeout);

pipe_type pipe_destroy(pipe_type pipe);
