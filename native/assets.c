#include "assets.h"
#include "webui.h"
#include "host/platform.h"
#include "native/thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Mutex mutex = MUTEX_INITIALIZER;
static char *bundles[MAX_NODES];
static unsigned version;

void assets_set(const ScoreView *view, unsigned revision) {
	mutex_lock(&mutex);
	version = revision;
	for (int i = 0; i < MAX_NODES; ++i) {
		free(bundles[i]);
		bundles[i] = view && i < view->nnodes && view->nodes[i].bundle ? host_path(view->nodes[i].bundle) : NULL;
	}
	mutex_unlock(&mutex);
}

const void *assets_read(const char *url, int *length) {
	unsigned revision, id;
	int offset = 0;
	if (sscanf(url, "/perone/%u/%u/%n", &revision, &id, &offset) != 2 || !offset || id >= MAX_NODES)
		return NULL;
	void *data = NULL;
	char *path = NULL, *resolved = NULL;
	mutex_lock(&mutex);
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
	resolved = host_path(path);
	// Includes symlink resolution: a UI cannot fetch files outside its own bundle.
	if (!resolved || strncmp(resolved, bundle, base) || resolved[base] != '/')
		goto done;
	HostFileInfo info;
	if (host_file_info(resolved, &info) || info.directory || info.size > 64 * 1024 * 1024)
		goto done;
	FILE *file = host_fopen(resolved, "rb");
	if (file) {
		char header[512];
		int size = snprintf(header, sizeof(header),
		    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nCache-Control: no-store\r\n\r\n",
		    webui_get_mime_type(resolved), (long long)info.size);
		if (size > 0 && size < (int)sizeof(header))
			data = webui_malloc(size + info.size + 1);
		if (data && fread((char *)data + size, 1, info.size, file) == (size_t)info.size && !ferror(file)) {
			memcpy(data, header, size);
			*length = size + info.size;
			((char *)data)[*length] = 0;
		} else {
			webui_free(data);
			data = NULL;
		}
		fclose(file);
	}
done:
	free(path);
	free(resolved);
	mutex_unlock(&mutex);
	return data;
}
