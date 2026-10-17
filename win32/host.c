#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "host/platform.h"
#include "path.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <direct.h>

wchar_t *win32_wide(const char *text) {
	int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
	wchar_t *result = size ? malloc(size * sizeof(*result)) : NULL;
	if (result)
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, size);
	return result;
}
char *win32_utf8(const wchar_t *text) {
	int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
	char *result = size ? malloc(size) : NULL;
	if (result)
		WideCharToMultiByte(CP_UTF8, 0, text, -1, result, size, NULL, NULL);
	return result;
}
char *host_path(const char *path) {
	wchar_t *input = win32_wide(path);
	HANDLE file = input ? CreateFileW(input, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)
	                    : INVALID_HANDLE_VALUE;
	free(input);
	if (file == INVALID_HANDLE_VALUE) {
		errno = ENOENT;
		return NULL;
	}
	DWORD length = GetFinalPathNameByHandleW(file, NULL, 0, FILE_NAME_NORMALIZED);
	wchar_t *name = length ? calloc(length + 1, sizeof(*name)) : NULL;
	if (name && !GetFinalPathNameByHandleW(file, name, length + 1, FILE_NAME_NORMALIZED)) {
		free(name);
		name = NULL;
	}
	CloseHandle(file);
	if (!name)
		return NULL;
	wchar_t *start = name;
	if (!wcsncmp(name, L"\\\\?\\UNC\\", 8)) {
		start = name + 6;
		*start = L'\\';
	} else if (!wcsncmp(name, L"\\\\?\\", 4))
		start += 4;
	char *result = win32_utf8(start);
	if (result) {
		for (char *p = result; *p; ++p)
			if (*p == '\\')
				*p = '/';
	}
	free(name);
	return result;
}
char *host_parent(const char *path) {
	const char *slash = strrchr(path, '/');
	if (!slash)
		return NULL;
	size_t length = slash == path || (slash == path + 2 && path[1] == ':') ? slash - path + 1 : slash - path;
	char *result = malloc(length + 1);
	if (result) {
		memcpy(result, path, length);
		result[length] = 0;
	}
	return result;
}
void *library_open(const char *path) {
	char *resolved = host_path(path);
	wchar_t *name = resolved ? win32_wide(resolved) : NULL;
	free(resolved);
	HMODULE module =
	    name ? LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) : NULL;
	if (!module)
		fprintf(stderr, "[Loader] %s: Windows error %lu\n", path, GetLastError());
	free(name);
	return module;
}
void *library_symbol(void *library, const char *name) {
	return (void *)GetProcAddress(library, name);
}
void library_close(void *library) {
	if (library)
		FreeLibrary(library);
}
uint64_t host_clock(void) {
	return GetTickCount64();
}
int host_mkstemp(char *path) {
	size_t size = strlen(path) + 1;
	char *pattern = malloc(size);
	if (!pattern)
		return -1;
	memcpy(pattern, path, size);
	int fd = -1;
	for (int i = 0; i < 100; ++i) {
		memcpy(path, pattern, size);
		wchar_t *name = win32_wide(path);
		if (!name)
			break;
		if (_wmktemp_s(name, wcslen(name) + 1)) {
			free(name);
			break;
		}
		char *candidate = win32_utf8(name);
		if (!candidate) {
			free(name);
			break;
		}
		memcpy(path, candidate, size);
		free(candidate);
		fd = _wopen(name, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
		free(name);
		if (fd >= 0 || errno != EEXIST)
			break;
	}
	free(pattern);
	return fd;
}
int host_replace(const char *source, const char *target) {
	wchar_t *from = win32_wide(source), *to = win32_wide(target);
	int result = from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
	free(from);
	free(to);
	return result;
}
int host_mkdir(const char *path) {
	wchar_t *name = win32_wide(path);
	int result = name ? _wmkdir(name) : -1;
	free(name);
	return result;
}
void host_sleep(unsigned milliseconds) {
	Sleep(milliseconds);
}
