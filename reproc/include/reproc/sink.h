#pragma once

#include <reproc/reproc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Used by `reproc_drain` to provide data to the caller. Each time data is
read, `function` is called with `context` .*/
typedef struct reproc_sink {
  bool (*function)(REPROC_STREAM stream,
                   const uint8_t *buffer,
                   size_t size,
                   void *context);
  void *context;
} reproc_sink;

/*! Pass `REPROC_SINK_NULL` as the sink for output streams that have not been
redirected to a pipe. */
REPROC_EXPORT extern const reproc_sink REPROC_SINK_NULL;

/*!
Calls `reproc_read` on `stream` until `reproc_read` returns an error or one of
the sinks returns false. The `out` and `err` sinks receive the output from
stdout and stderr respectively. The same sink may be passed to both `out` and
`err`.

`reproc_drain` always starts by calling both sinks once with an empty buffer and
`stream` set to `REPROC_STREAM_IN` to give each sink the chance to process all
output from the previous call to `reproc_drain` one by one.

Note that his function returns 0 instead of `REPROC_EPIPE` when both output
streams of the child process are closed.

For examples of sinks, see `sink.h`.

Actionable errors:
- `REPROC_ETIMEDOUT`
*/
REPROC_EXPORT int
reproc_drain(reproc_t *process, reproc_sink out, reproc_sink err);

/*!
Stores the output (both stdout and stderr) of a process in `output`.

Expects a `char **` with its value set to `NULL` as its initial context.
(Re)Allocates memory as necessary to store the output and assigns it as the
value of the given context. If allocating more memory fails, the already
allocated memory is freed and the value of the given context is set to `NULL`.

After calling `reproc_drain` with `reproc_sink_string`, the value of `output`
will either point to valid memory or will be set to `NULL`. This means it is
always safe to call `free` on `output`'s value after `reproc_drain` finishes.

Because the context this function expects does not store the output size,
`strlen` is called each time data is read to calculate the current size of the
output. This might cause performance problems when draining processes that
produce a lot of output.

Similarly, this sink will not work on processes that have null terminators in
their output because `strlen` is used to calculate the current output size.

The `drain` example shows how to use `reproc_sink_string`.
```
*/
REPROC_EXPORT reproc_sink reproc_sink_string(char **output);

/*! Discards the output of a process. */
REPROC_EXPORT reproc_sink reproc_sink_discard(void);

/*! Calls `free` on `ptr` and returns `NULL`. Use this function to free memory
allocated by `reproc_sink_string`. This avoids issues with allocating across
module (DLL) boundaries on Windows. */
REPROC_EXPORT void *reproc_free(void *ptr);
