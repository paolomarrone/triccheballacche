#include "host/vst3.h"
#include "host/platform.h"
#include <stdio.h>
#include <stdlib.h>

// Windows uses COM GUID byte order and has no Linux IRunLoop interface.
const unsigned char vst_id_order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
int vst_loop_new(void **loop) {
	*loop = NULL;
	return 0;
}
void vst_loop_poll(void *loop) {
	(void)loop;
}

void *vst_library_open(const char *path) {
	HostFileInfo info;
	if (host_file_info(path, &info))
		return NULL;
	void *library;
	if (info.directory) {
		const char *name = strrchr(path, '/');
		name = name ? name + 1 : path;
		size_t size = strlen(path) + strlen(name) + 32;
		char *binary = malloc(size);
		if (!binary)
			return NULL;
		snprintf(binary, size, "%s/Contents/x86_64-win/%s", path, name);
		library = library_open(binary);
		free(binary);
	} else
		library = library_open(path);
	char (*entry)(void) = library ? library_symbol(library, "InitDll") : NULL;
	if (entry && !entry()) {
		library_close(library);
		return NULL;
	}
	return library;
}
void vst_library_close(void *library) {
	if (!library)
		return;
	char (*leave)(void) = library_symbol(library, "ExitDll");
	if (leave && !leave())
		fputs("[VST3] ExitDll reported failure\n", stderr);
	library_close(library);
}
