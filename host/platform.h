#ifndef HOST_PLATFORM_H
#define HOST_PLATFORM_H
#include <stdint.h>
#include <stdio.h>

// Owned paths; the canonical path uses '/' as its directory separator.
char *host_path(const char *path);
char *host_parent(const char *path);
void *library_open(const char *path);
void *library_symbol(void *library, const char *name);
void library_close(void *library);
uint64_t host_clock(void); // Monotonic milliseconds.

typedef struct {
	FILE *stream;
	char *temporary;
	const char *path;
} StateFile;
// Writes replace the destination only after a successful close.
int state_file_open(StateFile *file, const char *path, int write);
int state_file_close(StateFile *file, int success);
#endif
