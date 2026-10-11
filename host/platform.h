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

#endif
