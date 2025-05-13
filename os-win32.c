#define WIN32_LEAN_AND_MEAN

#include <assert.h>
#include <stdlib.h>
#include <windows.h>

#include "os.h"
#include "graph.h"
#include "util.h"

void
osgetcwd(char *buf, size_t len)
{
	if (!GetCurrentDirectoryA(len, buf)) {
		fatal("GetCurrentDirectory:");
	}
}

void
oschdir(const char *dir)
{
	if (!SetCurrentDirectoryA(dir)) {
		fatal("SetCurrentDirectory %s:", dir);
	}
}

int
osmkdirs(struct string *_path, bool parent)
{
	char *path = _path->s;
	if (!parent) {
		if (!CreateDirectoryA(path, NULL)) {
			DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS) {
				warn("mkdirs %s:", path);
				return -1;
			}
		}
		return 0;
	}
	char folder[MAX_PATH];
	char *end;
	ZeroMemory(folder, sizeof(folder));

	end = strchr(path, L'\\');

	while (end != NULL) {
		strncpy(folder, path, end - path + 1);
		if (!CreateDirectoryA(folder, NULL)) {
			DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS) {
				warn("mkdirs %s:", folder);
				return -1;
			}
		}
		end = strchr(++end, L'\\');
	}
	return 0;
}

// taken fron ninja-build
static int64_t TimeStampFromFileTime(const FILETIME* filetime)
{
	// FILETIME is in 100-nanosecond increments since the Windows epoch.
	// We don't much care about epoch correctness but we do want the
	// resulting value to fit in a 64-bit integer.
	int64_t mtime = ((int64_t)filetime->dwHighDateTime << 32) |
	                ((int64_t)filetime->dwLowDateTime);
	// 1600 epoch -> 2000 epoch (subtract 400 years).
	return (int64_t)mtime - 12622770400LL * (1000000000LL / 100LL);
}

// taken fron ninja-build
int64_t
osmtime(const char *name)
{
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExA(name, GetFileExInfoStandard, &attrs)) {
		DWORD win_err = GetLastError();
		if (win_err == ERROR_FILE_NOT_FOUND || win_err == ERROR_PATH_NOT_FOUND) {
			// ok
		} else {
			warn("GetFileTime:");
		}
		return MTIME_MISSING;
	}
	return TimeStampFromFileTime(&attrs.ftLastWriteTime);
}

int
osclock_gettime_monotonic(struct ostimespec* ts)
{
	static LARGE_INTEGER frequency = {0};
	static BOOL initialized = FALSE;
	static BOOL qpf_available = FALSE;
	if (!initialized) {
		qpf_available = QueryPerformanceFrequency(&frequency);
		initialized = TRUE;
	}
	if (!qpf_available) {
		SetLastError(ERROR_NOT_CAPABLE);
		warn("QueryPerformanceFrequency:");
		return -1;
	}
	LARGE_INTEGER counter;
	if (!QueryPerformanceCounter(&counter)) {
		warn("QueryPerformanceCounter:");
		return -1;
	}
	uint64_t ticks = counter.QuadPart;
	uint64_t ticks_per_sec = frequency.QuadPart;

	ts->tv_sec = ticks / ticks_per_sec;
	uint64_t remaining_ticks = ticks % ticks_per_sec;
	ts->tv_nsec = (remaining_ticks * 1000000000ULL) / ticks_per_sec;

	return 0;
}

///////////////////////////// JOBS

static HANDLE 
win_create_nul()
{
	SECURITY_ATTRIBUTES sa = {sizeof(sa)};
	sa.bInheritHandle = TRUE;
	// Must be inheritable so subprocesses can dup to children.
	HANDLE nul = CreateFileA("NUL", GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		&sa, OPEN_EXISTING, 0, NULL);
	if (nul == INVALID_HANDLE_VALUE)
		fatal("couldn't open nul:");
	return nul;
}

static int
pipe_check_ready(HANDLE pipe)
{
	DWORD avail = 0;
	return PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0;
}


int
osjob_create(struct osjob_ctx *ctx, struct osjob *created, struct string *cmd, bool console)
{
	// taken and modified from ninja-build
	char pipe_name[100] = {0};
	snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\samu_pid%lu_sp%p", GetCurrentProcessId(), (void *)created);

	created->pipe = CreateNamedPipeA(pipe_name,
	                                   PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
	                                   PIPE_TYPE_BYTE,
	                                   PIPE_UNLIMITED_INSTANCES,
	                                   0, 0, INFINITE, NULL);

	if (created->pipe == INVALID_HANDLE_VALUE) {
		fatal("CreateNamedPipe: %s", pipe_name);
	}

	// here CreateIoCompletionPort() can be used to make subprocesses cancellable using centralized ioport

	HANDLE child_pipe;
	{
		HANDLE output_write_handle =
		    CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		HANDLE curr = GetCurrentProcess();
		if (!DuplicateHandle(curr, output_write_handle,
		                     curr, &child_pipe,
		                     0, TRUE, DUPLICATE_SAME_ACCESS)) {
			fatal("DuplicateHandle");
		}
		CloseHandle(output_write_handle);
	}

	HANDLE nul = win_create_nul();

	STARTUPINFOA sa = {sizeof(sa)};
	if (!console) {
		sa.dwFlags = STARTF_USESTDHANDLES;
		sa.hStdInput = nul;
		sa.hStdOutput = child_pipe;
		sa.hStdError = child_pipe;
	}

	PROCESS_INFORMATION process_info;
	memset(&process_info, 0, sizeof(process_info));
	{
		WORD process_flags = 0;
		if (!CreateProcessA(NULL, cmd->s, NULL, NULL,
		                    /* inherit handles */ TRUE, process_flags,
		                    NULL, NULL, &sa, &process_info)) {
			DWORD error = GetLastError();

			if (error == ERROR_FILE_NOT_FOUND) {
				if (child_pipe) {
					CloseHandle(child_pipe);
				}
				if (created->pipe) {
					CloseHandle(created->pipe);
					created->pipe = 0;
				}
				CloseHandle(nul);
				return -1;
			} else {
				fatal("CreateProcess: %s", cmd->s);
			}
		}
	}
	if (child_pipe)
		CloseHandle(child_pipe);
	CloseHandle(nul);
	created->proc = process_info.hProcess;
	CloseHandle(process_info.hThread);
	return 0;
}

/*ojobs is array of osjob*, entries may be NULL (invalid osjob).*/
int osjob_wait(struct osjob_ctx *ctx, struct osjob *ojobs, size_t jobs_count, int timeout);
/*read out into buffer*/
ssize_t osjob_work(struct osjob_ctx *ctx, struct osjob *ojob, void *buf, size_t buflen);
int osjob_close(struct osjob_ctx *ctx, struct osjob *ojob);
int osjob_done(struct osjob_ctx *ctx, struct osjob *ojob, struct string *cmd);
void osjob_ctx_init(struct osjob_ctx* ctx)
{

}

void osjob_ctx_close(struct osjob_ctx* ctx)
{

}



