#pragma once

// Use this to assert inside a `PROTECT/UNPROTECT` block to avoid clang dead
// store warnings.
#define ASSERT_UNUSED(expression)                                              \
  do {                                                                         \
    (void) !(expression);                                                      \
    assert((expression));                                                      \
  } while (0)

// Returns `r` if `expression` is false.
#define ASSERT_RETURN(expression, r)                                           \
  do {                                                                         \
    if (!(expression)) {                                                       \
      return (r);                                                              \
    }                                                                          \
  } while (false)

#define ASSERT_EINVAL(expression) ASSERT_RETURN(expression, REPROC_EINVAL)

// Returns a common representation of platform-specific errors. In practice, the
// value of `errno` or `GetLastError` is negated and returned if `r` indicates
// an error occurred (`r < 0` on Linux and `r == 0` on Windows).
//
// Returns 0 if no error occurred. If `r` has already been passed through
// `error_unify` before, it is returned unmodified.
int error_unify(int r);

// `error_unify` but returns `success` if no error occurred.
int error_unify_or_else(int r, int success);

const char *error_string(int error);
