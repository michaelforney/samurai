#include <assert.h>
#include <stdlib.h>

#include "os.h"
#include "graph.h"
#include "util.h"

#define BUFFER_SIZE 4096

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

struct osjob_ctx {
	HANDLE iocp;
};

struct osjob_ctx*
osjob_ctx_create()
{
	struct osjob_ctx *result = xmalloc(sizeof(*result));
	result->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (result->iocp == INVALID_HANDLE_VALUE) {
		fatal("CreateIoCompletionPort:");
	}
	return result;
}

void
osjob_ctx_close(struct osjob_ctx *osctx)
{
	CloseHandle(osctx->iocp);
	free(osctx);
}

int
osjob_create(struct osjob_ctx *osctx, struct osjob *created, struct string *cmd, bool console)
{
	HANDLE stdoutRead, stdoutWrite;
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
	if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0))
		return -1;
	SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
	
	HANDLE nul = win_create_nul();
	
	STARTUPINFOA si = {sizeof(si)};
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = stdoutWrite;
	si.hStdError = stdoutWrite;
	si.hStdInput = nul;

	PROCESS_INFORMATION pi;
	BOOL inheritHandles = TRUE;
	DWORD flags = 0;

	if (!CreateProcessA(NULL, cmd->s, NULL, NULL, inheritHandles, flags, NULL, NULL, &si, &pi)) {
		CloseHandle(stdoutRead);
		CloseHandle(stdoutWrite);
		CloseHandle(nul);
		return -1;
	}

	CloseHandle(stdoutWrite);
	CloseHandle(nul);
	CloseHandle(pi.hThread);

	created->output = stdoutRead;
	created->hProcess = pi.hProcess;
	created->valid = true;
	created->has_data = false;
	created->overlapped = (OVERLAPPED){0};

	CreateIoCompletionPort(created->output, osctx->iocp, (ULONG_PTR)created, 0);

	if (!ReadFile(created->output, NULL, BUFFER_SIZE,
	              NULL, &created->overlapped) &&
	    GetLastError() != ERROR_IO_PENDING) {
		osjob_close(osctx, created);
		return -1;
	}

	return 0;
}

int osjob_wait(struct osjob_ctx* osctx, struct osjob ojobs[], size_t jobs_count, int timeout)
{
	OVERLAPPED_ENTRY entries[64];
	ULONG num_entries = 0;
	const DWORD timeout_ms = timeout == -1 ? INFINITE : timeout;

	if (!GetQueuedCompletionStatusEx(osctx->iocp, entries, 64, &num_entries,
	                                 timeout_ms, FALSE)) {
		return GetLastError() == WAIT_TIMEOUT ? 0 : -1;
	}

	for (ULONG i = 0; i < num_entries; i++) {
		struct osjob *job = (struct osjob *)entries[i].lpCompletionKey;
		const DWORD bytes = entries[i].dwNumberOfBytesTransferred;
		job->to_read = bytes;
		job->has_data = true;
	}
	return 0;
}

ssize_t osjob_work(struct osjob_ctx* osctx, struct osjob* ojob, void* buf, size_t buflen)
{
	assert(ojob->has_data);
	if (ojob->to_read == 0) { // EOF/process exit
		return 0;
	} else { // Data available
		DWORD read;
		if (!ReadFile(ojob->output, buf, buflen, &read, &ojob->overlapped) &&
		    GetLastError() != ERROR_IO_PENDING) {
			ojob->valid = false;
			return -1;
		}
		return read;
	}
}

int osjob_done(struct osjob_ctx* osctx, struct osjob* ojob, struct string* cmd)
{
	WaitForSingleObject(ojob->hProcess, INFINITE);
	int exit_code;
	GetExitCodeProcess(ojob->hProcess, &exit_code);
	if (exit_code != 0) {
		warn("job failed with status %d: %s", exit_code, cmd->s);
		osjob_close(osctx, ojob);
		return -1;
	}
	return osjob_close(osctx, ojob);
}

int osjob_close(struct osjob_ctx* osctx, struct osjob* ojob)
{
	CloseHandle(ojob->hProcess);
	CloseHandle(ojob->output);
	memset(ojob, 0, sizeof(*ojob));
	return 0;
}


