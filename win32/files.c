#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "host/platform.h"
#include "path.h"
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

int host_file_info(const char *path, HostFileInfo *out) {
	wchar_t *name = win32_wide(path);
	WIN32_FILE_ATTRIBUTE_DATA info;
	int ok = name && GetFileAttributesExW(name, GetFileExInfoStandard, &info);
	free(name);
	if (!ok) {
		errno = ENOENT;
		return -1;
	}
	*out = (HostFileInfo){((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow,
	    ((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime,
	    !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)};
	return 0;
}
FILE *host_fopen(const char *path, const char *mode) {
	wchar_t *name = win32_wide(path), *flags = win32_wide(mode);
	FILE *file = name && flags ? _wfopen(name, flags) : NULL;
	free(name);
	free(flags);
	return file;
}
FILE *host_tempfile(char *path, const char *preserve_mode_from) {
	(void)preserve_mode_from;
	int fd = host_mkstemp(path);
	if (fd < 0)
		return NULL;
	FILE *file = _fdopen(fd, "w+b");
	if (!file) {
		_close(fd);
		host_remove(path);
	}
	return file;
}
int host_seek(FILE *file, int64_t offset, int origin) {
	return _fseeki64(file, offset, origin);
}
int host_remove(const char *path) {
	wchar_t *name = win32_wide(path);
	int result = name ? _wunlink(name) : -1;
	free(name);
	return result;
}
FILE *host_scan_output(void) {
	int fd = _dup(_fileno(stdout));
	if (fd < 0)
		return NULL;
	FILE *file = _dup2(_fileno(stderr), _fileno(stdout)) < 0 ? NULL : _fdopen(fd, "w");
	if (!file)
		_close(fd);
	return file;
}
int host_directory(const char *path, void (*visit)(void *, const char *, int), void *context) {
	size_t length = strlen(path);
	char *pattern = malloc(length + 3);
	if (!pattern)
		return -1;
	sprintf(pattern, "%s/*", path);
	wchar_t *wide = win32_wide(pattern);
	free(pattern);
	WIN32_FIND_DATAW info;
	HANDLE search = wide ? FindFirstFileW(wide, &info) : INVALID_HANDLE_VALUE;
	free(wide);
	if (search == INVALID_HANDLE_VALUE) {
		errno = ENOENT;
		return -1;
	}
	do {
		char *name = win32_utf8(info.cFileName);
		if (name) {
			visit(context, name, !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
			free(name);
		}
	} while (FindNextFileW(search, &info));
	DWORD error = GetLastError();
	FindClose(search);
	return error == ERROR_NO_MORE_FILES ? 0 : -1;
}
