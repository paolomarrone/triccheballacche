#include "host/platform.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

char *host_path(const char *path) {
	return realpath(path, NULL);
}
char *host_parent(const char *path) {
	const char *slash = strrchr(path, '/');
	if (!slash)
		return NULL;
	size_t size = slash == path ? 1 : (size_t)(slash - path);
	char *parent = malloc(size + 1);
	if (parent) {
		memcpy(parent, path, size);
		parent[size] = 0;
	}
	return parent;
}
void *library_open(const char *path) {
	void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!library)
		fprintf(stderr, "[Loader] %s: %s\n", path, dlerror());
	return library;
}
void *library_symbol(void *library, const char *name) {
	return dlsym(library, name);
}
void library_close(void *library) {
	if (library)
		dlclose(library);
}
uint64_t host_clock(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int state_file_open(StateFile *f, const char *path, int write) {
	*f = (StateFile){.path = path};
	if (write) {
		f->temporary = malloc(strlen(path) + sizeof(".XXXXXX"));
		if (!f->temporary)
			return -1;
		sprintf(f->temporary, "%s.XXXXXX", path);
		int fd = mkstemp(f->temporary);
		f->stream = fd >= 0 ? fdopen(fd, "wb") : NULL;
		if (!f->stream && fd >= 0)
			close(fd);
	} else
		f->stream = fopen(path, "rb");
	if (f->stream)
		return 0;
	state_file_close(f, 0);
	return -1;
}
int state_file_close(StateFile *f, int success) {
	if (f->stream && fclose(f->stream))
		success = 0;
	if (f->temporary) {
		if (success && rename(f->temporary, f->path))
			success = 0;
		if (!success)
			unlink(f->temporary);
		free(f->temporary);
	}
	*f = (StateFile){0};
	return success ? 0 : -1;
}
