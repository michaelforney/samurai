#include <reproc/run.h>

#include <reproc/sink.h>

int reproc_run(const char *const *argv, reproc_options options)
{
  if (!options.redirect.discard) {
    options.redirect.parent = true;
  }

  return reproc_run_ex(argv, options, REPROC_SINK_NULL, REPROC_SINK_NULL);
}

int reproc_run_ex(const char *const *argv,
                  reproc_options options,
                  reproc_sink out,
                  reproc_sink err)
{
  reproc_t *process = NULL;
  int r = REPROC_ENOMEM;

  process = reproc_new();
  if (process == NULL) {
    goto finish;
  }

  r = reproc_start(process, argv, options);
  if (r < 0) {
    goto finish;
  }

  r = reproc_drain(process, out, err);
  if (r < 0) {
    goto finish;
  }

  r = reproc_wait(process, REPROC_DEADLINE);
  if (r < 0) {
    goto finish;
  }

finish:
  reproc_destroy(process);

  return r;
}
