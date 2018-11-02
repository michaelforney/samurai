#include "platform.h"
#include "arg.h"
#include "build.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

struct implplatformprocess {
	HANDLE process;
	HANDLE pipe;
	HANDLE pipethread;
	HANDLE pipethreadready;
	HANDLE wakepipethread;
	HANDLE wfmo;
};

static void
vwarngle(const char *fmt, va_list ap)
{
	DWORD error = GetLastError();
        fprintf(stderr, "%s: ", argv0);
        vfprintf(stderr, fmt, ap);
        if (error) {
		char buf[1024];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL);
		fprintf(stderr, ": %s", buf);
	}
        fputc('\n', stderr);
}

static void
errgle(int status, const char *fmt, ...)
{
	va_list ap;

        va_start(ap, fmt);
        vwarngle(fmt, ap);
        va_end(ap);

	exit(status);
}

static void
warngle(const char *fmt, ...)
{
	va_list ap;

        va_start(ap, fmt);
        vwarngle(fmt, ap);
        va_end(ap);
}

static struct
implplatformprocess processtoimpl(struct platformprocess p)
{
	struct implplatformprocess impl;
	memcpy(&impl, &p, sizeof(impl));
	return impl;
}

static struct
platformprocess impltoprocess(struct implplatformprocess impl)
{
	struct platformprocess p;
	memcpy(&p, &impl, sizeof(impl));
	return p;
}

static DWORD
pipethread(void *data)
{
	struct implplatformprocess impl = *(struct implplatformprocess *)data;
	SetEvent(impl.pipethreadready);

	for (;;) {
		WaitForSingleObject(impl.wakepipethread, INFINITE);

		// block until data is ready on impl.pipe
		DWORD r;
		ReadFile(impl.pipe, NULL, 0, &r, NULL);
		DWORD err = GetLastError();

		SetEvent(impl.wfmo);

		if (err == ERROR_BROKEN_PIPE)
			break;
	}

	return 0;
}

bool
createprocess(struct string *command, struct platformprocess *p, bool captureoutput)
{
	SECURITY_ATTRIBUTES sa; 
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa); 
	sa.bInheritHandle = TRUE; 

	// TODO: clean up on errors
	HANDLE stdoutpipe[2] = { NULL, NULL };
	if (CreatePipe(&stdoutpipe[0], &stdoutpipe[1], &sa, 0) == 0) {
		warngle("CreatePipe %s", command->s);
		return false;
	}

	STARTUPINFO si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = NULL;
	si.hStdError = stdoutpipe[1];
	si.hStdOutput = stdoutpipe[1];
	si.dwFlags |= STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));

	if (CreateProcessA(NULL, command->s, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) == 0) {
		warngle("CreateProcessA %s", command->s);
		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(stdoutpipe[1]);

	struct implplatformprocess impl;
	impl.process = pi.hProcess;
	impl.pipe = stdoutpipe[0];

	impl.pipethreadready = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (impl.pipethreadready == NULL)
		errgle(1, "CreateEvent");

	impl.wakepipethread = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (impl.wakepipethread == NULL)
		errgle(1, "CreateEvent");

	impl.wfmo = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (impl.wfmo == NULL)
		errgle(1, "CreateEvent");

	DWORD id;
	impl.pipethread = CreateThread(0, 0, pipethread, &impl, 0, &id);
	if (impl.pipethread == NULL)
		errgle(1, "CreateThread");

	WaitForSingleObject(impl.pipethreadready, INFINITE);

	*p = impltoprocess(impl);

	return true;
}

bool
readprocessoutput(struct platformprocess p, char *buf, size_t buflen, size_t *n)
{
	struct implplatformprocess impl = processtoimpl(p);

	DWORD r;
	assert(buflen < UINT32_MAX);
	if (ReadFile(impl.pipe, buf, (DWORD)buflen, &r, NULL) == 0) {
		if (GetLastError() != ERROR_BROKEN_PIPE)
			return false;
		r = 0;
	}

	SetEvent(impl.wakepipethread);

	*n = (size_t)r;
	return true;
}

static void
cleanupprocess(struct implplatformprocess impl)
{
	CloseHandle(impl.process);
	CloseHandle(impl.pipe);

	SetEvent(impl.wakepipethread);
	WaitForSingleObject(impl.pipethread, INFINITE);

	CloseHandle(impl.pipethread);
	CloseHandle(impl.pipethreadready);
	CloseHandle(impl.wakepipethread);
	CloseHandle(impl.wfmo);
}

bool
waitexit(struct job *j)
{
	struct implplatformprocess impl = processtoimpl(j->process);
	bool ok = false;
	if (WaitForSingleObject(impl.process, INFINITE) == WAIT_FAILED) {
		warngle("WaitForSingleObject");
	} else {
		DWORD status;
		if (GetExitCodeProcess(impl.process, &status) == 0) {
			warngle("GetExitCodeProcess");
		} else {
			if (status != 0) {
				warnx("job failed: %s (%u)", j->cmd->s, status);
			} else {
				ok = true;
			}
		}
	}

	cleanupprocess(impl);

	return ok;
}

void
killprocess(struct platformprocess p)
{
	struct implplatformprocess impl = processtoimpl(p);
	TerminateProcess(impl.process, 1);
	cleanupprocess(impl);
}

static HANDLE *wfmohandles = NULL;

void
initplatform(size_t maxjobs)
{
	wfmohandles = malloc(maxjobs * sizeof(wfmohandles[0]));
}

void
shutdownplatform()
{
	free(wfmohandles);
}

size_t
waitforjobs(const struct job *jobs, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		struct implplatformprocess impl = processtoimpl(jobs[i].process);
		wfmohandles[i] = impl.wfmo;
	}

	DWORD r = WaitForMultipleObjects(n, wfmohandles, FALSE, INFINITE);
	if (r == WAIT_FAILED)
		errgle(1, "WaitForMultipleObjects");

	assert(r < WAIT_OBJECT_0 + n);

	return r - WAIT_OBJECT_0;
}
