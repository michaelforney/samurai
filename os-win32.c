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

pid_t
oswaitpid(pid_t hProcess, int *status, int options)
{

	DWORD dwExitCode = 0;

	if (hProcess == (HANDLE)-1) {
		return (pid_t)-1;
	}

	// Check if the process is still running
	if (!GetExitCodeProcess(hProcess, &dwExitCode)) {
		warn("GetExitCodeProcess:");
		return (pid_t)-1;
	}
	if (dwExitCode == STILL_ACTIVE) {
		if (options & WNOHANG) {
			// Process is still running and we don't want to wait
			return 0;
		} else {
			// Wait for the process to exit
			WaitForSingleObject(hProcess, INFINITE);
			GetExitCodeProcess(hProcess, &dwExitCode);
		}
	}
	if (status) {
		*status = (int)dwExitCode;
	}
	CloseHandle(hProcess);
	return hProcess;
}

ssize_t
osread(fd_t fd, void* buf, size_t buflen)
{
	if (buflen == 0) {
		// POSIX allows read with size 0 (may return 0 or error)
		return 0;
	}

	char *buffer = (char *)buf;
	ssize_t total_read = 0;
	const DWORD max_chunk = 0xFFFFFFFF; // MAXDWORD

	while (buflen > 0) {
		DWORD chunk = buflen > max_chunk ? max_chunk : (DWORD)buflen;
		DWORD bytes_read = 0;

		if (!ReadFile(fd, buffer, chunk, &bytes_read, NULL)) {
			const DWORD err = GetLastError();

			if (total_read > 0) {
				// Return partial read (POSIX allows this)
				return total_read;
			}

			switch (err) {
			case ERROR_BROKEN_PIPE:
				return 0; // Treat as EOF for closed pipe

			case ERROR_OPERATION_ABORTED:
				errno = EINTR;
				break;

			case ERROR_NO_DATA:
			case ERROR_IO_PENDING:
				errno = EAGAIN;
				break;

			case ERROR_INVALID_HANDLE:
			case ERROR_ACCESS_DENIED:
				errno = EBADF;
				break;

			case ERROR_NOT_ENOUGH_MEMORY:
				errno = ENOMEM;
				break;

			case ERROR_HANDLE_EOF:
				return 0; // Explicit EOF indication

			default:
				errno = EIO;
				break;
			}

			warn("ReadFile");
			return -1;
		}

		if (bytes_read == 0) {
			// Regular EOF condition
			break;
		}

		total_read += bytes_read;
		buffer += bytes_read;
		buflen -= bytes_read;
	}

	return total_read ? total_read : (ssize_t)0;
}

void
oskill(pid_t pid, int signal)
{
	if (!TerminateProcess(pid, signal)) {
		fatal("TerminateProcess:");
	}
}

static int
pipe_check_ready(HANDLE pipe)
{
	DWORD avail = 0;
	return PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0;
}

int
ospoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	while (nfds > MAXIMUM_WAIT_OBJECTS) {
		int subcnt = ospoll(fds, MAXIMUM_WAIT_OBJECTS, timeout); //increases timeout
		if (subcnt) {
			return subcnt;
		}
		fds += MAXIMUM_WAIT_OBJECTS;
		nfds -= MAXIMUM_WAIT_OBJECTS;
	}

	int cnt = 0, wait_cnt = 0;
	HANDLE h[MAXIMUM_WAIT_OBJECTS];

	for (int i = 0; i < nfds; i++) {
		assert(fds[i].fd);
		assert(GetFileType(fds[i].fd) == FILE_TYPE_PIPE);
		fds[i].revents = 0;
		if (pipe_check_ready(fds[i].fd)) {
			fds[i].revents = POLLIN;
			cnt++;
		} else if (wait_cnt < MAXIMUM_WAIT_OBJECTS) {
			h[wait_cnt++] = fds[i].fd;
		}
	}

	if (cnt) {
		return cnt;
	}

	if (!wait_cnt) {
		return 0;
	}

	DWORD res = WaitForMultipleObjects(wait_cnt, h, 0, timeout < 0 ? INFINITE : timeout);
	if (res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + wait_cnt) {
		for (int i = 0; i < nfds; i++) {
			struct pollfd *pfd = fds + i;
			if (pfd->fd == h[res - WAIT_OBJECT_0] && (pfd->revents = pipe_check_ready(pfd->fd) ? POLLIN : 0)) {
				cnt++;
			}
		}
	} else if (res == WAIT_FAILED) {
		errno = EINVAL;
		return -1;
	}
	return res == WAIT_TIMEOUT ? 0 : cnt;
}

// taken and modified from ninja-build
int
oscreate_job(struct osjob *created, struct string* cmd, bool console)
{
	char pipe_name[100] = {""};
	snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\samu_pid%lu_sp%p", GetCurrentProcessId(), cmd->s);

	created->output = CreateNamedPipeA(pipe_name,
	   PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
	   PIPE_TYPE_BYTE,
	   PIPE_UNLIMITED_INSTANCES,
	   0, 0, INFINITE, NULL);

	if (created->output == INVALID_HANDLE_VALUE) {
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

	HANDLE nul;
	{

		SECURITY_ATTRIBUTES security_attributes;
		memset(&security_attributes, 0, sizeof(SECURITY_ATTRIBUTES));
		security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
		security_attributes.bInheritHandle = TRUE;

		nul =
		    CreateFileA("NUL", GENERIC_READ,
		                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                &security_attributes, OPEN_EXISTING, 0, NULL);
	}


	STARTUPINFOA startup_info;
	memset(&startup_info, 0, sizeof(startup_info));
	startup_info.cb = sizeof(STARTUPINFO);
	if (!console) {
		startup_info.dwFlags = STARTF_USESTDHANDLES;
		startup_info.hStdInput = nul;
		startup_info.hStdOutput = child_pipe;
		startup_info.hStdError = child_pipe;
	}

	
	PROCESS_INFORMATION process_info;
	memset(&process_info, 0, sizeof(process_info));
	{
		WORD process_flags = 0;
		if (!CreateProcessA(NULL, cmd->s, NULL, NULL,
			/* inherit handles */ TRUE, process_flags,
		    NULL, NULL, &startup_info, &process_info)) 
		{
			DWORD error = GetLastError();

			if (error == ERROR_FILE_NOT_FOUND) {
				if (child_pipe) {
					CloseHandle(child_pipe);
				}
				if (created->output) {
					CloseHandle(created->output);
					created->output = 0;
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
	created->pid = process_info.hProcess;
	CloseHandle(process_info.hThread);
	return 0;
}

void
osclose_job(struct osjob *ojob)
{
	CloseHandle(ojob->output);
}