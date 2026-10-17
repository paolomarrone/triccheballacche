#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "host/platform.h"
#include "path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Paths from host_path use forward slashes and cannot contain double quotes.
// No shell is involved. A blocked or crashing plugin cannot hang discovery.
char *host_scan(const char *path) {
	SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
	HANDLE reader = NULL, writer = NULL;
	if (strchr(path, '"') || !CreatePipe(&reader, &writer, &security, 0))
		return NULL;
	SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
	size_t size = strlen(path) + 64;
	char *command = malloc(size);
	if (!command) {
		CloseHandle(reader);
		CloseHandle(writer);
		return NULL;
	}
	snprintf(command, size, "\"build/tools/plugin-scan.exe\" \"%s\"", path);
	wchar_t *wide = win32_wide(command);
	free(command);
	STARTUPINFOW startup = {.cb = sizeof(startup),
	    .dwFlags = STARTF_USESTDHANDLES,
	    .hStdOutput = writer,
	    .hStdError = GetStdHandle(STD_ERROR_HANDLE),
	    .hStdInput = GetStdHandle(STD_INPUT_HANDLE)};
	PROCESS_INFORMATION process = {0};
	int started = wide &&
	    CreateProcessW(
	        L"build/tools/plugin-scan.exe", wide, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
	free(wide);
	CloseHandle(writer);
	if (!started) {
		CloseHandle(reader);
		fprintf(stderr, "[Scan] Cannot start plugin-scan.exe: %lu\n", GetLastError());
		return NULL;
	}
	char *data = NULL;
	size_t used = 0;
	ULONGLONG deadline = GetTickCount64() + 10000;
	int failed = 0;
	for (;;) {
		DWORD available = 0;
		if (!PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL)) {
			failed = GetLastError() != ERROR_BROKEN_PIPE;
			break;
		}
		if (GetTickCount64() >= deadline) {
			failed = 1;
			break;
		}
		if (!available) {
			if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) {
				// Drain any bytes written just before process exit.
				if (!PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL) || !available)
					break;
			} else
				continue;
		}
		if (used + available >= 32 * 1024 * 1024) {
			failed = 1;
			break;
		}
		char *next = realloc(data, used + available + 1);
		if (!next) {
			failed = 1;
			break;
		}
		data = next;
		DWORD received;
		if (!ReadFile(reader, data + used, available, &received, NULL)) {
			failed = 1;
			break;
		}
		used += received;
	}
	CloseHandle(reader);
	ULONGLONG now = GetTickCount64();
	DWORD remaining = now < deadline ? (DWORD)(deadline - now) : 0;
	if (failed || WaitForSingleObject(process.hProcess, remaining) != WAIT_OBJECT_0) {
		TerminateProcess(process.hProcess, 1);
		WaitForSingleObject(process.hProcess, INFINITE);
		failed = 1;
	}
	DWORD status = 1;
	GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	if (failed || status || !used) {
		fprintf(stderr, "[Scan] %s: failed, crashed or timed out\n", path);
		free(data);
		return NULL;
	}
	data[used] = 0;
	return data;
}
