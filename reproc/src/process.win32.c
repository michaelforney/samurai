#define _WIN32_WINNT _WIN32_WINNT_VISTA

#include "process.h"

#include "error.h"
#include "macro.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

const HANDLE PROCESS_INVALID = INVALID_HANDLE_VALUE; // NOLINT

extern const int REPROC_SIGKILL;
extern const int REPROC_SIGTERM;

static const DWORD CREATION_FLAGS =
    // Create each child process in a new process group so we don't send
    // `CTRL-BREAK` signals to more than one child process in
    // `process_terminate`.
    CREATE_NEW_PROCESS_GROUP |
    // Create each child process with a Unicode environment as we accept any
    // UTF-16 encoded environment (including Unicode characters). Create each
    CREATE_UNICODE_ENVIRONMENT |
    // Create each child with an extended STARTUPINFOEXW structure so we can
    // specify which handles should be inherited.
    EXTENDED_STARTUPINFO_PRESENT;

static SECURITY_ATTRIBUTES HANDLE_DO_NOT_INHERIT = {
  .nLength = sizeof(SECURITY_ATTRIBUTES),
  .bInheritHandle = false,
  .lpSecurityDescriptor = NULL
};

// Argument escaping implementation is based on the following blog post:
// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/

static bool argument_should_escape(const char *argument)
{
  assert(argument);

  bool should_escape = false;

  for (size_t i = 0; i < strlen(argument); i++) {
    should_escape = should_escape || argument[i] == ' ' ||
                    argument[i] == '\t' || argument[i] == '\n' ||
                    argument[i] == '\v' || argument[i] == '\"';
  }

  return should_escape;
}

static size_t argument_escaped_size(const char *argument)
{
  assert(argument);

  size_t argument_size = strlen(argument);

  if (!argument_should_escape(argument)) {
    return argument_size;
  }

  size_t size = 2; // double quotes

  for (size_t i = 0; i < argument_size; i++) {
    size_t num_backslashes = 0;

    while (i < argument_size && argument[i] == '\\') {
      i++;
      num_backslashes++;
    }

    if (i == argument_size) {
      size += num_backslashes * 2;
    } else if (argument[i] == '"') {
      size += num_backslashes * 2 + 2;
    } else {
      size += num_backslashes + 1;
    }
  }

  return size;
}

static size_t argument_escape(char *dest, const char *argument)
{
  assert(dest);
  assert(argument);

  size_t argument_size = strlen(argument);

  if (!argument_should_escape(argument)) {
    memcpy(dest, argument, argument_size);
    return argument_size;
  }

  const char *begin = dest;

  *dest++ = '"';

  for (size_t i = 0; i < argument_size; i++) {
    size_t num_backslashes = 0;

    while (i < argument_size && argument[i] == '\\') {
      i++;
      num_backslashes++;
    }

    if (i == argument_size) {
      memset(dest, '\\', num_backslashes * 2);
      dest += num_backslashes * 2;
    } else if (argument[i] == '"') {
      memset(dest, '\\', num_backslashes * 2 + 1);
      dest += num_backslashes * 2 + 1;
      *dest++ = '"';
    } else {
      memset(dest, '\\', num_backslashes);
      dest += num_backslashes;
      *dest++ = argument[i];
    }
  }

  *dest++ = '"';

  return (size_t)(dest - begin);
}

static char *argv_join(const char *const *argv)
{
  assert(argv);

  // Determine the size of the concatenated string first.
  size_t joined_size = 1; // Count the null terminator.
  for (int i = 0; argv[i] != NULL; i++) {
    joined_size += argument_escaped_size(argv[i]);

    if (argv[i + 1] != NULL) {
      joined_size++; // Count whitespace.
    }
  }

  char *joined = calloc(joined_size, sizeof(char));
  if (joined == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  char *current = joined;
  for (int i = 0; argv[i] != NULL; i++) {
    current += argument_escape(current, argv[i]);

    // We add a space after each argument in the joined arguments string except
    // for the final argument.
    if (argv[i + 1] != NULL) {
      *current++ = ' ';
    }
  }

  *current = '\0';

  return joined;
}

static size_t environment_join_size(const char *const *environment)
{
  assert(environment);

  size_t joined_size = 1; // Count the null terminator.
  for (int i = 0; environment[i] != NULL; i++) {
    joined_size += strlen(environment[i]) + 1; // Count the null terminator.
  }

  return joined_size;
}

static char *environment_join(const char *const *environment)
{
  assert(environment);

  char *joined = calloc(environment_join_size(environment), sizeof(char));
  if (joined == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  char *current = joined;
  for (int i = 0; environment[i] != NULL; i++) {
    size_t to_copy = strlen(environment[i]) + 1; // Include null terminator.
    memcpy(current, environment[i], to_copy);
    current += to_copy;
  }

  *current = '\0';

  return joined;
}

static wchar_t *string_to_wstring(const char *string, size_t size)
{
  assert(string);
  // Overflow check although we really don't expect this to ever happen. This
  // makes the following casts to `int` safe.
  assert(size <= INT_MAX);

  // Determine wstring size (`MultiByteToWideChar` returns the required size if
  // its last two arguments are `NULL` and 0).
  int r = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string, (int) size,
                              NULL, 0);
  if (r == 0) {
    return NULL;
  }

  // `MultiByteToWideChar` does not return negative values so the cast to
  // `size_t` is safe.
  wchar_t *wstring = calloc((size_t) r, sizeof(wchar_t));
  if (wstring == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  // Now we pass our allocated string and its size as the last two arguments
  // instead of `NULL` and 0 which makes `MultiByteToWideChar` actually perform
  // the conversion.
  r = MultiByteToWideChar(CP_UTF8, 0, string, (int) size, wstring, r);
  if (r == 0) {
    free(wstring);
    return NULL;
  }

  return wstring;
}

static const DWORD NUM_ATTRIBUTES = 1;

static LPPROC_THREAD_ATTRIBUTE_LIST setup_attribute_list(HANDLE *handles,
                                                         size_t num_handles)
{
  assert(handles);

  int r = 0;

  // Make sure all the given handles can be inherited.
  for (size_t i = 0; i < num_handles; i++) {
    r = SetHandleInformation(handles[i], HANDLE_FLAG_INHERIT,
                             HANDLE_FLAG_INHERIT);
    if (r == 0) {
      return NULL;
    }
  }

  // Get the required size for `attribute_list`.
  SIZE_T attribute_list_size = 0;
  r = InitializeProcThreadAttributeList(NULL, NUM_ATTRIBUTES, 0,
                                        &attribute_list_size);
  if (r == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return NULL;
  }

  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = malloc(attribute_list_size);
  if (attribute_list == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  r = InitializeProcThreadAttributeList(attribute_list, NUM_ATTRIBUTES, 0,
                                        &attribute_list_size);
  if (r == 0) {
    free(attribute_list);
    return NULL;
  }

  // Add the handles to be inherited to `attribute_list`.
  r = UpdateProcThreadAttribute(attribute_list, 0,
                                PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                num_handles * sizeof(HANDLE), NULL, NULL);
  if (r == 0) {
    DeleteProcThreadAttributeList(attribute_list);
    return NULL;
  }

  return attribute_list;
}

int process_start(HANDLE *process,
                  const char *const *argv,
                  struct process_options options)
{
  assert(process);

  if (argv == NULL) {
    return -ERROR_CALL_NOT_IMPLEMENTED;
  }

  assert(argv[0] != NULL);

  char *command_line = NULL;
  wchar_t *command_line_wstring = NULL;
  char *environment_line = NULL;
  wchar_t *environment_line_wstring = NULL;
  wchar_t *working_directory_wstring = NULL;
  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = NULL;
  PROCESS_INFORMATION info = { PROCESS_INVALID, HANDLE_INVALID, 0, 0 };
  int r = 0;

  // Join `argv` to a whitespace delimited string as required by
  // `CreateProcessW`.
  command_line = argv_join(argv);
  if (command_line == NULL) {
    goto finish;
  }

  // Convert UTF-8 to UTF-16 as required by `CreateProcessW`.
  command_line_wstring = string_to_wstring(command_line,
                                           strlen(command_line) + 1);
  if (command_line_wstring == NULL) {
    goto finish;
  }

  // Idem for `environment` if it isn't `NULL`.
  if (options.environment != NULL) {
    environment_line = environment_join(options.environment);
    if (environment_line == NULL) {
      goto finish;
    }

    size_t joined_size = environment_join_size(options.environment);
    environment_line_wstring = string_to_wstring(environment_line, joined_size);
    if (environment_line_wstring == NULL) {
      goto finish;
    }
  }

  // Idem for `working_directory` if it isn't `NULL`.
  if (options.working_directory != NULL) {
    size_t working_directory_size = strlen(options.working_directory) + 1;
    working_directory_wstring = string_to_wstring(options.working_directory,
                                                  working_directory_size);
    if (working_directory_wstring == NULL) {
      goto finish;
    }
  }

  // Windows Vista added the `STARTUPINFOEXW` structure in which we can put a
  // list of handles that should be inherited. Only these handles are inherited
  // by the child process. Other code in an application that calls
  // `CreateProcess` without passing a `STARTUPINFOEXW` struct containing the
  // handles it should inherit can still unintentionally inherit handles meant
  // for a reproc child process. See https://stackoverflow.com/a/2345126 for
  // more information.
  HANDLE handles[] = { options.handle.exit, options.handle.in, options.handle.out,
                       options.handle.err };
  size_t num_handles = ARRAY_SIZE(handles);

  if (options.handle.out == options.handle.err) {
    // CreateProcess doesn't like the same handle being specified twice in the
    // `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` attribute.
    num_handles--;
  }

  attribute_list = setup_attribute_list(handles, num_handles);
  if (attribute_list == NULL) {
    goto finish;
  }

  STARTUPINFOEXW extended_startup_info = {
    .StartupInfo = { .cb = sizeof(extended_startup_info),
                     .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW,
                     // `STARTF_USESTDHANDLES`
                     .hStdInput = options.handle.in,
                     .hStdOutput = options.handle.out,
                     .hStdError = options.handle.err,
                     // `STARTF_USESHOWWINDOW`. Make sure the console window of
                     // the child process isn't visible. See
                     // https://github.com/DaanDeMeyer/reproc/issues/6 and
                     // https://github.com/DaanDeMeyer/reproc/pull/7 for more
                     // information.
                     .wShowWindow = SW_HIDE },
    .lpAttributeList = attribute_list
  };

  LPSTARTUPINFOW startup_info_address = &extended_startup_info.StartupInfo;

  // Child processes inherit the error mode of their parents. To avoid child
  // processes creating error dialogs we set our error mode to not create error
  // dialogs temporarily which is inherited by the child process.
  DWORD previous_error_mode = SetErrorMode(SEM_NOGPFAULTERRORBOX);

  r = CreateProcessW(NULL, command_line_wstring, &HANDLE_DO_NOT_INHERIT,
                     &HANDLE_DO_NOT_INHERIT, true, CREATION_FLAGS,
                     environment_line_wstring, working_directory_wstring,
                     startup_info_address, &info);

  SetErrorMode(previous_error_mode);

  if (r == 0) {
    goto finish;
  }

  *process = info.hProcess;

finish:
  free(command_line);
  free(command_line_wstring);
  free(environment_line);
  free(environment_line_wstring);
  free(working_directory_wstring);
  DeleteProcThreadAttributeList(attribute_list);
  handle_destroy(info.hThread);

  return error_unify_or_else(r, 1);
}

int process_wait(HANDLE process)
{
  assert(process);

  int r = 0;

  r = (int) WaitForSingleObject(process, INFINITE);
  if ((DWORD) r == WAIT_FAILED) {
    return error_unify(0);
  }

  DWORD status = 0;
  r = GetExitCodeProcess(process, &status);
  if (r == 0) {
    return error_unify(r);
  }

  // `GenerateConsoleCtrlEvent` causes a process to exit with this exit code.
  // Because `GenerateConsoleCtrlEvent` has roughly the same semantics as
  // `SIGTERM`, we map its exit code to `SIGTERM`.
  if (status == 3221225786) {
    status = (DWORD) REPROC_SIGTERM;
  }

  assert(status <= INT_MAX);
  return (int) status;
}

int process_terminate(HANDLE process)
{
  assert(process && process != PROCESS_INVALID);

  // `GenerateConsoleCtrlEvent` can only be called on a process group. To call
  // `GenerateConsoleCtrlEvent` on a single child process it has to be put in
  // its own process group (which we did when starting the child process).
  BOOL r = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId(process));

  return error_unify(r);
}

int process_kill(HANDLE process)
{
  assert(process && process != PROCESS_INVALID);

  // We use 137 (`SIGKILL`) as the exit status because it is the same exit
  // status as a process that is stopped with the `SIGKILL` signal on POSIX
  // systems.
  BOOL r = TerminateProcess(process, (DWORD) REPROC_SIGKILL);

  return error_unify(r);
}

HANDLE process_destroy(HANDLE process)
{
  return handle_destroy(process);
}
