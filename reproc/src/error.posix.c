#define _POSIX_C_SOURCE 200809L

#include "error.h"

#include "macro.h"

#include <reproc/reproc.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

const int REPROC_EINVAL = -EINVAL;
const int REPROC_EPIPE = -EPIPE;
const int REPROC_ETIMEDOUT = -ETIMEDOUT;
const int REPROC_ENOMEM = -ENOMEM;
const int REPROC_EWOULDBLOCK = -EWOULDBLOCK;

int error_unify(int r)
{
  return error_unify_or_else(r, 0);
}

int error_unify_or_else(int r, int success)
{
  // if `r < -1`, `r` has been passed through this function before so we just
  // return it as is.
  return r < -1 ? r : r == -1 ? -errno : success;
}

enum { ERROR_STRING_MAX_SIZE = 512 };

const char *error_string(int error)
{
  static THREAD_LOCAL char string[ERROR_STRING_MAX_SIZE];

  int r = strerror_r(abs(error), string, ARRAY_SIZE(string));
  if (r != 0) {
    return "Failed to retrieve error string";
  }

  return string;
}
