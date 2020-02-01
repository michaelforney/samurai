#define _WIN32_WINNT _WIN32_WINNT_VISTA

#include "handle.h"

#include "error.h"

#include <assert.h>
#include <io.h>
#include <windows.h>

const HANDLE HANDLE_INVALID = INVALID_HANDLE_VALUE; // NOLINT

int handle_from(FILE *file, HANDLE *handle)
{
  int r = -1;

  r = _fileno(file);
  if (r < 0) {
    return -ERROR_INVALID_HANDLE;
  }

  intptr_t result = _get_osfhandle(r);
  if (result == -1) {
    return -ERROR_INVALID_HANDLE;
  }

  *handle = (HANDLE) result;

  return error_unify(r);
}

int handle_cloexec(handle_type handle, bool enable)
{
  (void) handle;
  (void) enable;
  return -ERROR_CALL_NOT_IMPLEMENTED;
}

HANDLE handle_destroy(HANDLE handle)
{
  if (handle == NULL || handle == HANDLE_INVALID) {
    return HANDLE_INVALID;
  }

  int r = 0;

  r = CloseHandle(handle);
  ASSERT_UNUSED(r != 0);

  return HANDLE_INVALID;
}
