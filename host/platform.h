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
int host_mkstemp(char *path);
int host_replace(const char *source, const char *target);
int host_mkdir(const char *path);
void host_sleep(unsigned milliseconds);
typedef struct {
	uint64_t size, modified;
	int directory;
} HostFileInfo;
int host_file_info(const char *path, HostFileInfo *info);
FILE *host_fopen(const char *path, const char *mode);
FILE *host_tempfile(char *path, const char *preserve_mode_from);
FILE *host_scan_output(void);
int host_seek(FILE *file, int64_t offset, int origin);
int host_remove(const char *path);
// Visits regular files and directories; names are UTF-8, borrowed for the callback.
int host_directory(const char *path, void (*visit)(void *, const char *, int), void *context);
char *host_scan(const char *path);

#endif
