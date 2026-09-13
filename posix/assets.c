#include "assets.h"
#include "webui.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static char *bundles[MAX_NODES];
static unsigned version;

void assets_set(const ScoreView *view, unsigned revision) {
	pthread_mutex_lock(&mutex);
	version = revision;
	for (int i = 0; i < MAX_NODES; ++i) {
		free(bundles[i]);
		bundles[i] = view && i < view->nnodes && view->nodes[i].bundle ? realpath(view->nodes[i].bundle, NULL) : NULL;
	}
	pthread_mutex_unlock(&mutex);
}

const void *assets_read(const char *url, int *length) {
	unsigned revision, id;
	int offset = 0;
	if (sscanf(url, "/perone/%u/%u/%n", &revision, &id, &offset) != 2 || !offset || id >= MAX_NODES)
		return NULL;
	void *data = NULL;
	char *path = NULL, *resolved = NULL;
	pthread_mutex_lock(&mutex);
	const char *bundle = bundles[id];
	if (!bundle || revision != version)
		goto done;
	size_t base = strlen(bundle), tail = strlen(url + offset);
	path = malloc(base + tail + 2);
	if (!path)
		goto done;
	memcpy(path, bundle, base);
	path[base] = '/';
	char *out = path + base + 1;
	for (const char *p = url + offset; *p; ++p) {
		unsigned byte;
		if (*p == '%') {
			if (!p[1] || !p[2] || sscanf(p + 1, "%2x", &byte) != 1 || !byte)
				goto done;
			*out++ = byte;
			p += 2;
		} else
			*out++ = *p;
	}
	*out = 0;
	resolved = realpath(path, NULL);
	// Includes symlink resolution: a UI cannot fetch files outside its own bundle.
	if (!resolved || strncmp(resolved, bundle, base) || resolved[base] != '/')
		goto done;
	struct stat info;
	if (stat(resolved, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 64 * 1024 * 1024)
		goto done;
	FILE *file = fopen(resolved, "rb");
	if (file) {
		data = webui_malloc(info.st_size + 1);
		if (data && fread(data, 1, info.st_size, file) == (size_t)info.st_size && !ferror(file)) {
			((char *)data)[info.st_size] = 0;
			*length = info.st_size;
		} else {
			webui_free(data);
			data = NULL;
		}
		fclose(file);
	}
done:
	free(path);
	free(resolved);
	pthread_mutex_unlock(&mutex);
	return data;
}
