#include <stdbool.h>  /* for bool */
#include <stdint.h>
#include <stdio.h>    /* for _getc_nolock */
#include <windows.h>  /* for win32 API */
#include "graph.h"    /* for MTIME_MISSING */
#include "os.h"

bool
os_chdir(const char *dir)
{
	return SetCurrentDirectory(dir);
}

int
os_getc_unlocked(FILE *stream)
{
	return _getc_nolock(stream);
}

bool
os_getcwd(char *dir, size_t len)
{
	return GetCurrentDirectory((DWORD)len, dir) != 0;
}

bool
os_mkdir(const char *dir)
{
	return CreateDirectory(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

int64_t
os_query_mtime(const char *name)
{
	WIN32_FILE_ATTRIBUTE_DATA d;
	ULARGE_INTEGER t;

	if (!GetFileAttributesEx(name, GetFileExInfoStandard, &d)) {
		return MTIME_MISSING;
	}

	t.LowPart = d.ftLastWriteTime.dwLowDateTime;
	t.HighPart = d.ftLastWriteTime.dwHighDateTime;
	return t.QuadPart * 100;
}
