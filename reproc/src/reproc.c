#include <reproc/reproc.h>

#include "clock.h"
#include "error.h"
#include "handle.h"
#include "init.h"
#include "macro.h"
#include "pipe.h"
#include "process.h"
#include "redirect.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

struct reproc_t {
  process_type handle;
  struct {
    pipe_type in;
    pipe_type out;
    pipe_type err;
    pipe_type exit;
  } pipe;
  int status;
  reproc_stop_actions stop;
  int timeout;
  int64_t deadline;
};

enum { STATUS_NOT_STARTED = -1, STATUS_IN_PROGRESS = -2, STATUS_IN_CHILD = -3 };

const int REPROC_SIGKILL = UINT8_MAX + 9;
const int REPROC_SIGTERM = UINT8_MAX + 15;

const int REPROC_INFINITE = -1;
const int REPROC_DEADLINE = -2;

static int
parse_redirect(reproc_redirect *redirect, bool parent, bool discard, FILE *file)
{
  if (file) {
    ASSERT_EINVAL(!redirect->type && !redirect->handle && !redirect->file);
    redirect->type = REPROC_REDIRECT_FILE;
    redirect->file = file;
  }

  if (redirect->type == REPROC_REDIRECT_HANDLE || redirect->handle) {
    ASSERT_EINVAL(!redirect->type || redirect->type == REPROC_REDIRECT_HANDLE);
    ASSERT_EINVAL(redirect->handle);
    ASSERT_EINVAL(!redirect->file);
    redirect->type = REPROC_REDIRECT_HANDLE;
  }

  if (redirect->type == REPROC_REDIRECT_FILE || redirect->file) {
    ASSERT_EINVAL(!redirect->type || redirect->type == REPROC_REDIRECT_FILE);
    ASSERT_EINVAL(redirect->file);
    ASSERT_EINVAL(!redirect->handle);
    redirect->type = REPROC_REDIRECT_FILE;
  }

  if (!redirect->type) {
    if (parent) {
      ASSERT_EINVAL(!discard);
      redirect->type = REPROC_REDIRECT_PARENT;
    } else if (discard) {
      ASSERT_EINVAL(!parent);
      redirect->type = REPROC_REDIRECT_DISCARD;
    } else {
      redirect->type = REPROC_REDIRECT_PIPE;
    }
  }

  return 0;
}

static int parse_options(const char *const *argv, reproc_options *options)
{
  assert(options);

  int r = -1;

  r = parse_redirect(&options->redirect.in, options->redirect.parent,
                     options->redirect.discard, NULL);
  if (r < 0) {
    return r;
  }

  r = parse_redirect(&options->redirect.out, options->redirect.parent,
                     options->redirect.discard, options->redirect.file);
  if (r < 0) {
    return r;
  }

  r = parse_redirect(&options->redirect.err, options->redirect.parent,
                     options->redirect.discard, options->redirect.file);
  if (r < 0) {
    return r;
  }

  if (options->input.data != NULL || options->input.size > 0) {
    ASSERT_EINVAL(options->input.data != NULL);
    ASSERT_EINVAL(options->input.size > 0);
    ASSERT_EINVAL(options->redirect.in.type == REPROC_REDIRECT_PIPE);
  }

  if (options->fork) {
    ASSERT_EINVAL(argv == NULL);
  } else {
    ASSERT_EINVAL(argv != NULL);
    ASSERT_EINVAL(argv[0] != NULL);
  }

  // Default to waiting indefinitely but still allow setting a timeout of zero
  // by setting `timeout` to `REPROC_NONBLOCKING`.
  if (options->timeout == 0) {
    options->timeout = REPROC_INFINITE;
  }

  if (options->deadline == 0) {
    options->deadline = REPROC_INFINITE;
  }

  bool is_noop = options->stop.first.action == REPROC_STOP_NOOP &&
                 options->stop.second.action == REPROC_STOP_NOOP &&
                 options->stop.third.action == REPROC_STOP_NOOP;

  if (is_noop) {
    options->stop.first.action = REPROC_STOP_WAIT;
    options->stop.first.timeout = REPROC_DEADLINE;
    options->stop.second.action = REPROC_STOP_TERMINATE;
    options->stop.second.timeout = REPROC_INFINITE;
  }

  return 0;
}

static int
redirect_pipe(pipe_type *parent, handle_type *child, REPROC_STREAM stream)
{
  assert(parent);
  assert(child);

  pipe_type pipe[] = { PIPE_INVALID, PIPE_INVALID };

  int r = pipe_init(&pipe[0], &pipe[1]);
  if (r < 0) {
    return r;
  }

  *parent = stream == REPROC_STREAM_IN ? pipe[1] : pipe[0];
  *child = stream == REPROC_STREAM_IN ? (handle_type) pipe[0]
                                      : (handle_type) pipe[1];

  r = pipe_nonblocking(*parent, true);

  return error_unify(r);
}

static int redirect(pipe_type *parent,
                    handle_type *child,
                    REPROC_STREAM stream,
                    reproc_redirect redirect,
                    handle_type out)
{
  assert(parent);
  assert(child);

  int r = -1;

  switch (redirect.type) {

    case REPROC_REDIRECT_PIPE:
      r = redirect_pipe(parent, child, stream);
      break;

    case REPROC_REDIRECT_PARENT:
      r = redirect_parent(child, (REDIRECT_STREAM) stream);
      if (r == REPROC_EPIPE) {
        // Discard if the corresponding parent stream is closed.
        r = redirect_discard(child, (REDIRECT_STREAM) stream);
      }

      if (r < 0) {
        break;
      }

      *parent = PIPE_INVALID;

      break;

    case REPROC_REDIRECT_DISCARD:
      r = redirect_discard(child, (REDIRECT_STREAM) stream);
      if (r < 0) {
        break;
      }

      *parent = PIPE_INVALID;

      break;

    case REPROC_REDIRECT_HANDLE:
      assert(redirect.handle);

      r = 0;

      *child = redirect.handle;
      *parent = PIPE_INVALID;

      break;

    case REPROC_REDIRECT_FILE:
      assert(redirect.file);

      r = handle_from(redirect.file, child);
      if (r < 0) {
        break;
      }

      *parent = PIPE_INVALID;

      break;

    case REPROC_REDIRECT_STDOUT:
      assert(stream == REPROC_STREAM_ERR);
      assert(out != HANDLE_INVALID);

      r = 0;

      *child = out;
      *parent = PIPE_INVALID;

      break;

    default:
      r = REPROC_EINVAL;
      break;
  }

  return r;
}

static handle_type redirect_destroy(handle_type handle, REPROC_REDIRECT type)
{
  switch (type) {
    case REPROC_REDIRECT_PIPE:
      // We know `handle` is a pipe if `REDIRECT_PIPE` is used so the cast is
      // safe. This little hack prevents us from having to introduce a generic
      // handle type.
      pipe_destroy((pipe_type) handle);
      break;
    case REPROC_REDIRECT_DISCARD:
      handle_destroy(handle);
      break;
    case REPROC_REDIRECT_PARENT:
    case REPROC_REDIRECT_FILE:
    case REPROC_REDIRECT_HANDLE:
    case REPROC_REDIRECT_STDOUT:
      break;
  }

  return HANDLE_INVALID;
}

static int expiry(int timeout, int64_t deadline)
{
  if (timeout == REPROC_INFINITE && deadline == REPROC_INFINITE) {
    return REPROC_INFINITE;
  }

  if (deadline == REPROC_INFINITE) {
    return timeout;
  }

  int64_t now = reproc_now();

  if (now >= deadline) {
    return 0;
  }

  // `deadline` exceeds `reproc_now` by at most a full `int` so the cast is
  // safe.
  int remaining = (int) (deadline - now);

  if (timeout == REPROC_INFINITE) {
    return remaining;
  }

  return MIN(timeout, remaining);
}

static int find_earliest_expiry(reproc_event_source *sources,
                                size_t num_sources)
{
  assert(sources);
  assert(num_sources > 0 && num_sources <= INT_MAX);

  int earliest = 0;
  int min = REPROC_INFINITE;

  for (int i = 0; i < (int) num_sources; i++) {
    reproc_t *process = sources[i].process;
    int current = expiry(process->timeout, process->deadline);

    if (min == REPROC_INFINITE || current < min) {
      earliest = i;
      min = current;
    }
  }

  return earliest;
}

reproc_t *reproc_new(void)
{
  reproc_t *process = malloc(sizeof(reproc_t));
  if (process == NULL) {
    return NULL;
  }

  *process = (reproc_t){ .handle = PROCESS_INVALID,
                         .pipe = { .in = PIPE_INVALID,
                                   .out = PIPE_INVALID,
                                   .err = PIPE_INVALID,
                                   .exit = PIPE_INVALID },
                         .status = STATUS_NOT_STARTED,
                         .timeout = REPROC_INFINITE,
                         .deadline = REPROC_INFINITE };

  return process;
}

int reproc_start(reproc_t *process,
                 const char *const *argv,
                 reproc_options options)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status == STATUS_NOT_STARTED);

  struct {
    handle_type in;
    handle_type out;
    handle_type err;
    pipe_type exit;
  } child = { HANDLE_INVALID, HANDLE_INVALID, HANDLE_INVALID, PIPE_INVALID };
  int r = -1;

  r = init();
  if (r < 0) {
    goto finish;
  }

  r = parse_options(argv, &options);
  if (r < 0) {
    goto finish;
  }

  r = redirect(&process->pipe.in, &child.in, REPROC_STREAM_IN,
               options.redirect.in, HANDLE_INVALID);
  if (r < 0) {
    goto finish;
  }

  r = redirect(&process->pipe.out, &child.out, REPROC_STREAM_OUT,
               options.redirect.out, HANDLE_INVALID);
  if (r < 0) {
    goto finish;
  }

  r = redirect(&process->pipe.err, &child.err, REPROC_STREAM_ERR,
               options.redirect.err, child.out);
  if (r < 0) {
    goto finish;
  }

  r = pipe_init(&process->pipe.exit, &child.exit);
  if (r < 0) {
    goto finish;
  }

  if (options.input.data != NULL) {
    // `reproc_write` only needs the child process stdin pipe to be initialized.
    size_t written = 0;

    while (written < options.input.size) {
      r = reproc_write(process, options.input.data + written,
                       options.input.size - written);
      if (r < 0) {
        goto finish;
      }

      assert(written + (size_t) r <= options.input.size);
      written += (size_t) r;
    }

    r = reproc_close(process, REPROC_STREAM_IN);
    if (r < 0) {
      goto finish;
    }
  }

  struct process_options process_options = {
    .environment = options.environment,
    .working_directory = options.working_directory,
    .handle = { .in = child.in,
                .out = child.out,
                .err = child.err,
                .exit = (handle_type) child.exit }
  };

  r = process_start(&process->handle, argv, process_options);
  if (r < 0) {
    goto finish;
  }

  if (r > 0) {
    process->stop = options.stop;
    process->timeout = options.timeout;

    if (options.deadline != REPROC_INFINITE) {
      process->deadline = reproc_now() + options.deadline;
    }
  }

finish:
  // Either an error has ocurred or the child pipe endpoints have been copied to
  // the stdin/stdout/stderr streams of the child process. Either way, they can
  // be safely closed.
  redirect_destroy(child.in, options.redirect.in.type);
  redirect_destroy(child.out, options.redirect.out.type);
  redirect_destroy(child.err, options.redirect.err.type);
  pipe_destroy(child.exit);

  if (r < 0) {
    process->handle = process_destroy(process->handle);
    process->pipe.in = pipe_destroy(process->pipe.in);
    process->pipe.out = pipe_destroy(process->pipe.out);
    process->pipe.err = pipe_destroy(process->pipe.err);
    process->pipe.exit = pipe_destroy(process->pipe.exit);
    deinit();
  } else if (r == 0) {
    process->handle = PROCESS_INVALID;
    // `process_start` has already taken care of closing the handles for us.
    process->pipe.in = PIPE_INVALID;
    process->pipe.out = PIPE_INVALID;
    process->pipe.err = PIPE_INVALID;
    process->pipe.exit = PIPE_INVALID;
    process->status = STATUS_IN_CHILD;
  } else {
    process->status = STATUS_IN_PROGRESS;
  }

  return r;
}

static bool contains_valid_pipe(pipe_set *sets, size_t num_sets)
{
  for (size_t i = 0; i < num_sets; i++) {
    if (sets[i].in != PIPE_INVALID || sets[i].out != PIPE_INVALID ||
        sets[i].err != PIPE_INVALID) {
      return true;
    }
  }

  return false;
}

int reproc_poll(reproc_event_source *sources, size_t num_sources)
{
  ASSERT_EINVAL(sources);
  ASSERT_EINVAL(num_sources > 0);
  ASSERT_EINVAL(num_sources <= INT_MAX);

  int earliest = find_earliest_expiry(sources, num_sources);
  int timeout = expiry(sources[earliest].process->timeout,
                       sources[earliest].process->deadline);
  ASSERT_RETURN(timeout != 0, REPROC_ETIMEDOUT);

  int r = REPROC_ENOMEM;

  pipe_set *sets = calloc(sizeof(pipe_set), num_sources);
  if (sets == NULL) {
    return r;
  }

  for (size_t i = 0; i < num_sources; i++) {
    pipe_set *set = sets + i;
    reproc_t *process = sources[i].process;
    int interests = sources[i].interests;

    set->in = interests & REPROC_EVENT_IN ? process->pipe.in : PIPE_INVALID;
    set->out = interests & REPROC_EVENT_OUT ? process->pipe.out : PIPE_INVALID;
    set->err = interests & REPROC_EVENT_ERR ? process->pipe.err : PIPE_INVALID;
    set->exit = interests & REPROC_EVENT_EXIT ? process->pipe.exit
                                              : PIPE_INVALID;
  }

  if (!contains_valid_pipe(sets, num_sources)) {
    r = REPROC_EPIPE;
    goto finish;
  }

  r = pipe_wait(sets, num_sources, timeout);

  if (r == REPROC_ETIMEDOUT) {
    sources[earliest].events = REPROC_EVENT_TIMEOUT;
    r = 0;
    goto finish;
  }

  if (r < 0) {
    goto finish;
  }

  for (size_t i = 0; i < num_sources; i++) {
    sources[i].events = sets[i].events;
  }

finish:
  free(sets);

  return r;
}

int reproc_read(reproc_t *process,
                REPROC_STREAM stream,
                uint8_t *buffer,
                size_t size)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(stream == REPROC_STREAM_OUT || stream == REPROC_STREAM_ERR);
  ASSERT_EINVAL(buffer);

  pipe_type *pipe = stream == REPROC_STREAM_OUT ? &process->pipe.out
                                                : &process->pipe.err;
  if (*pipe == PIPE_INVALID) {
    return REPROC_EPIPE;
  }

  int r = pipe_read(*pipe, buffer, size);

  if (r == REPROC_EPIPE) {
    *pipe = pipe_destroy(*pipe);
  }

  return r;
}

int reproc_write(reproc_t *process, const uint8_t *buffer, size_t size)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);

  if (buffer == NULL) {
    // Allow `NULL` buffers but only if `size == 0`.
    ASSERT_EINVAL(size == 0);
    return 0;
  }

  if (process->pipe.in == PIPE_INVALID) {
    return REPROC_EPIPE;
  }

  int r = pipe_write(process->pipe.in, buffer, size);

  if (r == REPROC_EPIPE) {
    process->pipe.in = pipe_destroy(process->pipe.in);
  }

  return r;
}

int reproc_close(reproc_t *process, REPROC_STREAM stream)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);

  switch (stream) {
    case REPROC_STREAM_IN:
      process->pipe.in = pipe_destroy(process->pipe.in);
      break;
    case REPROC_STREAM_OUT:
      process->pipe.out = pipe_destroy(process->pipe.out);
      break;
    case REPROC_STREAM_ERR:
      process->pipe.err = pipe_destroy(process->pipe.err);
      break;
    default:
      return REPROC_EINVAL;
  }

  return 0;
}

int reproc_wait(reproc_t *process, int timeout)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  int r = -1;

  if (process->status >= 0) {
    return process->status;
  }

  if (timeout == REPROC_DEADLINE) {
    // If the deadline has expired, `expiry` returns 0 which means we'll only
    // check if the process is still running.
    timeout = expiry(REPROC_INFINITE, process->deadline);
  }

  pipe_set set = { .in = PIPE_INVALID,
                   .out = PIPE_INVALID,
                   .err = PIPE_INVALID,
                   .exit = process->pipe.exit };

  r = pipe_wait(&set, 1, timeout);
  if (r < 0) {
    return r;
  }

  assert(set.events & PIPE_EVENT_EXIT);

  r = process_wait(process->handle);
  if (r < 0) {
    return r;
  }

  process->pipe.exit = pipe_destroy(process->pipe.exit);

  return process->status = r;
}

int reproc_terminate(reproc_t *process)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  if (process->status >= 0) {
    return 0;
  }

  return process_terminate(process->handle);
}

int reproc_kill(reproc_t *process)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  if (process->status >= 0) {
    return 0;
  }

  return process_kill(process->handle);
}

int reproc_stop(reproc_t *process, reproc_stop_actions stop)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  reproc_stop_action actions[] = { stop.first, stop.second, stop.third };
  int r = -1;

  for (size_t i = 0; i < ARRAY_SIZE(actions); i++) {
    switch (actions[i].action) {
      case REPROC_STOP_NOOP:
        continue;
      case REPROC_STOP_WAIT:
        r = 0;
        break;
      case REPROC_STOP_TERMINATE:
        r = reproc_terminate(process);
        break;
      case REPROC_STOP_KILL:
        r = reproc_kill(process);
        break;
      default:
        return REPROC_EINVAL;
    }

    // Stop if `reproc_terminate` or `reproc_kill` fail.
    if (r < 0) {
      break;
    }

    r = reproc_wait(process, actions[i].timeout);
    if (r != REPROC_ETIMEDOUT) {
      break;
    }
  }

  return r;
}

reproc_t *reproc_destroy(reproc_t *process)
{
  ASSERT_RETURN(process, NULL);

  if (process->status == STATUS_IN_PROGRESS) {
    reproc_stop(process, process->stop);
  }

  process->handle = process_destroy(process->handle);
  process->pipe.in = pipe_destroy(process->pipe.in);
  process->pipe.out = pipe_destroy(process->pipe.out);
  process->pipe.err = pipe_destroy(process->pipe.err);
  process->pipe.exit = pipe_destroy(process->pipe.exit);

  if (process->status != STATUS_NOT_STARTED) {
    deinit();
  }

  free(process);

  return NULL;
}

const char *reproc_strerror(int error)
{
  return error_string(error);
}
