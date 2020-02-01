#define _WIN32_WINNT _WIN32_WINNT_VISTA

#include "redirect.h"

#include "error.h"
#include "pipe.h"

#include <assert.h>
#include <windows.h>

static DWORD stream_to_id(REDIRECT_STREAM stream)
{
  switch (stream) {
    case REDIRECT_STREAM_IN:
      return STD_INPUT_HANDLE;
    case REDIRECT_STREAM_OUT:
      return STD_OUTPUT_HANDLE;
    case REDIRECT_STREAM_ERR:
      return STD_ERROR_HANDLE;
  }

  return 0;
}

int redirect_parent(HANDLE *out, REDIRECT_STREAM stream)
{
  assert(out);

  int r = 0;

  DWORD id = stream_to_id(stream);
  if (id == 0) {
    return -ERROR_INVALID_PARAMETER;
  }

  HANDLE *handle = GetStdHandle(id);
  if (handle == INVALID_HANDLE_VALUE) {
    return error_unify(r);
  }

  if (handle == NULL) {
    return -ERROR_BROKEN_PIPE;
  }

  *out = handle;

  return 0;
}

enum { FILE_NO_SHARE = 0, FILE_NO_TEMPLATE = 0 };

static SECURITY_ATTRIBUTES INHERIT = { .nLength = sizeof(SECURITY_ATTRIBUTES),
                                       .bInheritHandle = true,
                                       .lpSecurityDescriptor = NULL };

int redirect_discard(HANDLE *out, REDIRECT_STREAM stream)
{
  assert(out);

  DWORD mode = stream == REDIRECT_STREAM_IN ? GENERIC_READ : GENERIC_WRITE;
  int r = 0;

  HANDLE handle = CreateFile("NUL", mode, FILE_NO_SHARE, &INHERIT,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             (HANDLE) FILE_NO_TEMPLATE);
  if (handle == INVALID_HANDLE_VALUE) {
    return error_unify(r);
  }

  *out = handle;

  return 0;
}
